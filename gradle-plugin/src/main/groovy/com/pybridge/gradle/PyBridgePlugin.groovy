package com.pybridge.gradle

import com.pybridge.gradle.config.PythonExtension
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.file.CopySpec

/**
 * PyBridge Gradle Plugin
 *
 * 在 Android 构建流程中注入 Python 编译和打包步骤。
 *
 * 应用方式:
 *   plugins {
 *       id 'com.android.application'
 *       id 'com.pybridge.python'    ← 这个插件
 *   }
 *
 * 构建流程:
 *   buildNative (NDK 交叉编译) → pipInstall → compilePython → packageRuntime → mergeAssets
 *        │                              │                │            │
 *        ▼                              ▼                ▼            ▼
 *   libpython.so + libpybridge.so   pip 包安装      .py → .pyc    APK 打包
 */
class PyBridgePlugin implements Plugin<Project> {

    @Override
    void apply(Project project) {
        // 验证 Android 插件
        def android = project.extensions.findByName("android")
        if (!android) {
            throw new IllegalStateException("PyBridge requires Android plugin")
        }

        // 创建 python {} 扩展
        def ext = project.extensions.create("python", PythonExtension, project)

        // 注册清理任务
        project.tasks.register("cleanPython") {
            group = "pybridge"
            description = "Clean all PyBridge build artifacts"
            doLast {
                project.delete(project.layout.buildDirectory.dir("pybridge"))
                project.logger.lifecycle("🧹 PyBridge build artifacts cleaned")
            }
        }

        // 在 Android 构建完成后注入
        project.afterEvaluate {
            configureAndroid(project, ext)
        }
    }

    private void configureAndroid(Project project, PythonExtension ext) {
        def android = project.extensions.getByName("android")

        // ── 1. 添加运行时依赖 ──────────────────────────────────────
        // 本地项目依赖（开发时）或远程 Maven 依赖（发布后）
        def runtimeProject = project.rootProject.findProject(":runtime")
        if (runtimeProject) {
            project.dependencies.add("implementation", runtimeProject)
        } else {
            project.dependencies.add("implementation", "com.pybridge:runtime:1.0.0")
        }

        // ── 2. 配置 JNI 库打包 ─────────────────────────────────────
        android.sourceSets.main.jniLibs.srcDirs += [
            "${project.buildDir}/pybridge/jniLibs"
        ]

        // ── 3. 配置 assets 打包 ────────────────────────────────────
        android.sourceSets.main.assets.srcDirs += [
            "${project.buildDir}/pybridge/assets"
        ]

        // ── 4. 注入构建任务到 Android 流程 ─────────────────────────
        def variants = android.hasProperty("applicationVariants") ?
            android.applicationVariants : android.libraryVariants

        variants.all { variant ->
            def name = variant.name.capitalize()

            // 4a. NDK 交叉编译 (libpython.so + libpybridge.so)
            def buildNative = project.tasks.register("buildPyBridgeNative${name}") {
                group = "pybridge"
                description = "Cross-compile libpython.so and libpybridge.so for Android"
                doLast { buildNativeLibraries(project, ext) }
            }

            // 4b. 安装 pip 包
            def pipTask = project.tasks.register("pyBridgePipInstall${name}") {
                group = "pybridge"
                description = "Install Python pip packages for Android"
                doLast { installPipPackages(project, ext) }
            }

            // 4c. 编译 Python 代码
            def compileTask = project.tasks.register("pyBridgeCompile${name}") {
                group = "pybridge"
                description = "Compile .py → .pyc for Android"
                doLast { compilePythonCode(project, ext) }
            }

            // 4d. 打包运行时
            def packageTask = project.tasks.register("pyBridgePackage${name}") {
                group = "pybridge"
                description = "Package .so + stdlib into APK asset directories"
                doLast { packageRuntime(project, ext) }
            }

            // Hook 到 Android 构建流程
            def mergeAssets = project.tasks.findByName("merge${name}Assets")
            def mergeJni = project.tasks.findByName("merge${name}JniLibFolders")
            def javac = project.tasks.findByName("compile${name}JavaWithJavac")

            // 依赖链: buildNative → pipInstall → javac
            //          buildNative → compilePython → mergeAssets
            //          buildNative → packageRuntime → mergeJni
            if (javac) {
                javac.dependsOn(pipTask)
                pipTask.configure { dependsOn(buildNative) }
            }
            if (mergeAssets) {
                mergeAssets.dependsOn(compileTask)
                compileTask.configure { dependsOn(buildNative) }
            }
            if (mergeJni) {
                mergeJni.dependsOn(packageTask)
                packageTask.configure { dependsOn(buildNative) }
            }
        }
    }

    /**
     * 步骤 1: NDK 交叉编译
     *
     * 运行 build_android.sh 编译 libpython.so + libpybridge.so
     * 或者从预编译仓库下载
     */
    private void buildNativeLibraries(Project project, PythonExtension ext) {
        def abis = ext.abiFilters.get()
        def outputDir = project.file("${project.buildDir}/pybridge/output")

        // 检查是否有预编译的 .so 文件
        def prebuiltDir = project.file("prebuilt")
        if (prebuiltDir.exists()) {
            project.logger.lifecycle("📦 Using prebuilt native libraries from ${prebuiltDir}")
            return
        }

        // 尝试运行 NDK 交叉编译脚本
        def buildScript = project.rootProject.file("build-system/build_android.sh")
        if (!buildScript.exists()) {
            project.logger.warn("⚠ build_android.sh not found, skipping native build")
            project.logger.warn("  Download prebuilt .so from PyBridge releases, or run:")
            project.logger.warn("  cd build-system && ./build_android.sh --all")
            return
        }

        def ndkPath = ext.ndkPath.getOrElse("")
        def pythonVersion = ext.version.get()

        project.logger.lifecycle("🔨 Building native libraries for: ${abis}")

        for (abi in abis) {
            project.logger.lifecycle("  Building for ${abi}...")

            def cmd = ["bash", buildScript.absolutePath,
                       "--abi", abi,
                       "--jobs", Runtime.runtime.availableProcessors().toString()]
            if (ndkPath) {
                cmd += ["--ndk", ndkPath]
            }

            def result = project.exec { exec ->
                exec.commandLine = cmd
                exec.workingDir = buildScript.parentFile
                exec.ignoreExitValue = true
            }

            if (result.exitValue != 0) {
                project.logger.warn("  ⚠ Native build failed for ${abi}")
                project.logger.warn("  Please run manually: cd build-system && ./build_android.sh --abi ${abi}")
            }
        }
    }

    /**
     * 步骤 2: 安装 pip 包到 build/pybridge/assets/packages/
     */
    private void installPipPackages(Project project, PythonExtension ext) {
        def packages = ext.pip.packages.get()
        if (packages.isEmpty()) {
            project.logger.lifecycle("📦 No pip packages to install")
            return
        }

        def targetDir = project.file("${project.buildDir}/pybridge/assets/packages")
        targetDir.mkdirs()

        def python = ext.buildPython.get()

        project.logger.lifecycle("📦 Installing pip packages: ${packages}")
        project.exec { exec ->
            exec.commandLine = [
                python, "-m", "pip", "install",
                "--target", targetDir.absolutePath,
                "--index-url", ext.pip.indexUrl.get(),
                "--no-deps",
                "--quiet",
                *packages
            ]
            exec.ignoreExitValue = true
        }

        // 生成清单
        def manifest = [
            packages: packages.collect { [name: it.split("==")[0], spec: it] }
        ]
        new File(targetDir, "packages.json").text = groovy.json.JsonOutput.prettyPrint(
            groovy.json.JsonOutput.toJson(manifest)
        )
    }

    /**
     * 步骤 3: 编译 .py → .pyc
     */
    private void compilePythonCode(Project project, PythonExtension ext) {
        def srcDirs = ext.srcDirs.get()
        if (srcDirs.isEmpty()) {
            project.logger.lifecycle("🐍 No Python source directories configured")
            return
        }

        def outputDir = project.file("${project.buildDir}/pybridge/assets/python")
        outputDir.mkdirs()

        def python = ext.buildPython.get()
        def optLevel = ext.optimizeLevel.get()

        for (srcDir in srcDirs) {
            def src = project.file(srcDir)
            if (!src.exists()) {
                project.logger.warn("  ⚠ Python source dir not found: ${srcDir}")
                continue
            }

            project.logger.lifecycle("🐍 Compiling Python: ${srcDir}")

            project.exec { exec ->
                exec.commandLine = [
                    python, "-m", "compileall",
                    "-b",                           # 直接写 .pyc
                    "-d", outputDir.absolutePath,
                    "-o", optLevel.toString(),
                    "-q",
                    src.absolutePath
                ]
                exec.ignoreExitValue = true
            }

            // 复制源码（运行时可选加载）
            project.copy { CopySpec copy ->
                copy.from(src)
                copy.into(outputDir)
                copy.include("**/*.py")
            }
        }
    }

    /**
     * 步骤 4: 打包运行时 — .so 文件 + stdlib.zip 复制到 APK 目录
     */
    private void packageRuntime(Project project, PythonExtension ext) {
        def abis = ext.abiFilters.get()
        def jniDir = project.file("${project.buildDir}/pybridge/jniLibs")
        def assetsDir = project.file("${project.buildDir}/pybridge/assets")

        // 来源1: build_android.sh 的输出目录
        def buildOutput = project.rootProject.file("build-system/output")

        // 来源2: 预编译目录
        def prebuiltDir = project.file("prebuilt")

        for (abi in abis) {
            def abiJniDir = project.file("${jniDir}/${abi}")
            abiJniDir.mkdirs()

            def sourceDir = buildOutput.exists() ?
                new File(buildOutput, abi) : new File(prebuiltDir, abi)

            if (sourceDir.exists()) {
                project.logger.lifecycle("📦 Packaging ${abi} from ${sourceDir}")

                // 复制 libpython.so 和 libpybridge.so
                project.copy { CopySpec copy ->
                    copy.from(sourceDir)
                    copy.into(abiJniDir)
                    copy.include("lib/**/*.so")
                    copy.eachFile { it.relativePath = new org.gradle.api.file.RelativePath(true, it.name) }
                }

                // 复制 stdlib.zip 到 assets
                def assetsSource = new File(sourceDir, "assets")
                if (assetsSource.exists()) {
                    project.copy { CopySpec copy ->
                        copy.from(assetsSource)
                        copy.into(assetsDir)
                        copy.include("*.zip")
                    }
                }
            } else {
                project.logger.warn("⚠ No native libraries for ${abi} — run build_android.sh first")
            }
        }

        // 复制 pip 安装的包到 assets
        def packagesDir = project.file("${project.buildDir}/pybridge/assets/packages")
        if (packagesDir.exists() && packagesDir.listFiles()) {
            def finalAssetsDir = project.file("${project.buildDir}/pybridge/assets")
            project.copy { CopySpec copy ->
                copy.from(packagesDir)
                copy.into("${finalAssetsDir}/packages")
            }
            project.logger.lifecycle("📦 Pip packages packaged")
        }
    }
}