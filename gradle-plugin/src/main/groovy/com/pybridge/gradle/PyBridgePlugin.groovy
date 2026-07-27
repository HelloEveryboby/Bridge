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
 *   checkPython → pipInstall → compilePython → packageRuntime → mergeAssets
 *                                    │                │
 *                                    ▼                ▼
 *                              .py → .pyc      libpython + stdlib → assets/
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
            doLast { project.delete(project.layout.buildDirectory.dir("pybridge")) }
        }

        // 在 Android 构建完成后注入
        project.afterEvaluate {
            configureAndroid(project, ext)
        }
    }

    private void configureAndroid(Project project, PythonExtension ext) {
        def android = project.extensions.getByName("android")

        // ── 1. 添加运行时依赖 ──────────────────────────────────────
        project.dependencies.add("implementation", "com.pybridge:runtime:1.0.0")

        // ── 2. 配置 JNI 库打包 ─────────────────────────────────────
        // 将编译好的 .so 添加到 jniLibs 目录
        android.sourceSets.main.jniLibs.srcDirs += ["${project.buildDir}/pybridge/jniLibs"]

        // ── 3. 配置 assets 打包 ────────────────────────────────────
        android.sourceSets.main.assets.srcDirs += ["${project.buildDir}/pybridge/assets"]

        // ── 4. 注入构建任务到 Android 流程 ─────────────────────────
        def variants = android.hasProperty("applicationVariants") ?
            android.applicationVariants : android.libraryVariants

        variants.all { variant ->
            def name = variant.name.capitalize()

            // 安装 pip 包
            def pipTask = project.tasks.register("pipInstall${name}") {
                group = "pybridge"
                doLast { installPipPackages(project, ext) }
            }

            // 编译 Python 代码
            def compileTask = project.tasks.register("compilePython${name}") {
                group = "pybridge"
                doLast { compilePythonCode(project, ext) }
            }

            // 打包运行时 (.so + stdlib)
            def packageTask = project.tasks.register("packagePythonRuntime${name}") {
                group = "pybridge"
                doLast { packageRuntime(project, ext) }
            }

            // Hook 到 Android 构建流程
            def mergeAssets = project.tasks.findByName("merge${name}Assets")
            def mergeJni = project.tasks.findByName("merge${name}JniLibFolders")
            def javac = project.tasks.findByName("compile${name}JavaWithJavac")

            if (javac) javac.dependsOn(pipTask)
            if (mergeAssets) mergeAssets.dependsOn(compileTask)
            if (mergeJni) mergeJni.dependsOn(packageTask)
        }
    }

    /**
     * 安装 pip 包到 build/pybridge/assets/packages/
     */
    private void installPipPackages(Project project, PythonExtension ext) {
        def packages = ext.pip.packages.get()
        if (packages.isEmpty()) return

        def targetDir = project.file("${project.buildDir}/pybridge/assets/packages")
        targetDir.mkdirs()

        def python = ext.buildPython.get()

        project.exec { exec ->
            exec.commandLine = [
                python, "-m", "pip", "install",
                "--target", targetDir.absolutePath,
                "--index-url", ext.pip.indexUrl.get(),
                "--no-deps",
                "--quiet",
                *packages
            ]
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
     * 编译 .py → .pyc
     */
    private void compilePythonCode(Project project, PythonExtension ext) {
        def srcDirs = ext.srcDirs.get()
        if (srcDirs.isEmpty()) return

        def outputDir = project.file("${project.buildDir}/pybridge/assets/python")
        outputDir.mkdirs()

        def python = ext.buildPython.get()
        def optLevel = ext.optimizeLevel.get()

        for (srcDir in srcDirs) {
            def src = project.file(srcDir)
            if (!src.exists()) continue

            project.exec { exec ->
                exec.commandLine = [
                    python, "-m", "compileall",
                    "-b",                           # 直接写 .pyc
                    "-d", outputDir.absolutePath,
                    "-o", optLevel.toString(),
                    "-q",
                    src.absolutePath
                ]
            }

            // 复制源码（也放进去，运行时可选加载）
            project.copy { CopySpec copy ->
                copy.from(src)
                copy.into(outputDir)
                copy.include("**/*.py")
            }
        }
    }

    /**
     * 打包运行时: .so 文件 + stdlib.zip
     */
    private void packageRuntime(Project project, PythonExtension ext) {
        def abis = ext.abiFilters.get()
        def pyVersion = ext.version.get()
        def jniDir = project.file("${project.buildDir}/pybridge/jniLibs")
        def assetsDir = project.file("${project.buildDir}/pybridge/assets")

        for (abi in abis) {
            def abiDir = project.file("${jniDir}/${abi}")
            abiDir.mkdirs()

            // 从预编译仓库复制 .so
            def prebuiltDir = project.file("prebuilt/${abi}")
            if (prebuiltDir.exists()) {
                project.copy { CopySpec copy ->
                    copy.from(prebuiltDir)
                    copy.into(abiDir)
                    copy.include("*.so")
                }
            }
        }

        // 复制 stdlib.zip
        def stdlibDir = project.file("prebuilt/assets")
        if (stdlibDir.exists()) {
            project.copy { CopySpec copy ->
                copy.from(stdlibDir)
                copy.into(assetsDir)
                copy.include("*.zip")
            }
        }
    }
}
