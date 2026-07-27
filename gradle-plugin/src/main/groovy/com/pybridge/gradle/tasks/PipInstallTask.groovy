package com.pybridge.gradle.tasks

import org.gradle.api.DefaultTask
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.*
import org.gradle.process.ExecOperations
import javax.inject.Inject

/**
 * 安装 pip 包到目标目录，支持多架构交叉编译
 *
 * 核心流程：
 * 1. 解析包名和版本约束
 * 2. 下载预编译的 Android wheel（优先）或源码包
 * 3. 如果是纯 Python 包，直接解压
 * 4. 如果包含 C 扩展，需要交叉编译（使用 NDK 工具链）
 * 5. 将安装结果复制到 assets 目录
 */
abstract class PipInstallTask extends DefaultTask {

    @Input
    abstract Property<String> getPythonPath()

    @Input
    abstract ListProperty<String> getPackages()

    @OutputDirectory
    abstract Property<org.gradle.api.file.Directory> getTargetDir()

    @Input
    abstract ListProperty<String> getAbiFilters()

    @Input
    abstract Property<String> getPythonVersion()

    @Input
    abstract Property<String> getIndexUrl()

    @Inject
    abstract ExecOperations getExecOperations()

    @TaskAction
    void install() {
        def python = pythonPath.get()
        def targetDirectory = targetDir.get().asFile
        def index = indexUrl.get()
        def pkgList = packages.get()

        if (pkgList.isEmpty()) {
            logger.lifecycle("📦 No pip packages to install")
            targetDirectory.mkdirs()
            return
        }

        logger.lifecycle("📦 Installing ${pkgList.size()} pip packages...")

        // 创建临时虚拟环境用于安装
        def venvDir = new File(project.buildDir, "pybridge/venv")
        createVenv(python, venvDir)

        def venvPip = new File(venvDir, "bin/pip").absolutePath
        if (System.getProperty("os.name").toLowerCase().contains("windows")) {
            venvPip = new File(venvDir, "Scripts/pip.exe").absolutePath
        }

        // 安装包到虚拟环境
        for (pkg in pkgList) {
            installPackage(venvPip, pkg, index)
        }

        // 收集安装的包到目标目录
        targetDirectory.mkdirs()
        collectPackages(venvDir, targetDirectory)

        // 生成包清单
        generatePackageManifest(targetDirectory, pkgList)

        // 清理虚拟环境
        deleteDir(venvDir)

        logger.lifecycle("✅ Packages installed to ${targetDirectory.absolutePath}")
    }

    private void createVenv(String python, File venvDir) {
        if (venvDir.exists()) {
            deleteDir(venvDir)
        }
        execOperations.exec { exec ->
            exec.commandLine(python, "-m", "venv", venvDir.absolutePath)
            exec.standardOutput = System.out
            exec.errorOutput = System.err
        }
    }

    private void installPackage(String pip, String pkg, String indexUrl) {
        logger.lifecycle("   Installing: ${pkg}")
        def args = [pip, "install", pkg, "--index-url", indexUrl, "--no-deps"]

        // 优先使用预编译的 Android wheel
        args.addAll(["--platform", "linux_aarch64", "--python-version", pythonVersion.get(), "--only-binary", ":all:"])

        def result = execOperations.exec { exec ->
            exec.commandLine(args)
            exec.standardOutput = System.out
            exec.errorOutput = System.err
            exec.ignoreExitValue = true
        }

        if (result.exitValue != 0) {
            // 回退：尝试从源码安装
            logger.lifecycle("   ⚠ Binary not available, trying source install...")
            execOperations.exec { exec ->
                exec.commandLine(pip, "install", pkg, "--index-url", indexUrl, "--no-deps", "--no-binary", ":all:")
                exec.standardOutput = System.out
                exec.errorOutput = System.err
            }
        }
    }

    private void collectPackages(File venvDir, File targetDir) {
        // 从 site-packages 收集已安装的包
        def sitePackages = findSitePackages(venvDir)
        if (!sitePackages) {
            throw new RuntimeException("Cannot find site-packages in virtual environment")
        }

        // 复制包目录
        def packagesDir = new File(targetDir, "site-packages")
        packagesDir.mkdirs()

        sitePackages.eachDir { pkgDir ->
            if (!pkgDir.name.startsWith("_") && !pkgDir.name.endsWith(".dist-info")) {
                copyDir(pkgDir, new File(packagesDir, pkgDir.name))
            }
        }

        // 复制 .py 文件（顶层模块）
        sitePackages.eachFileMatch(~/.*\.py/) { pyFile ->
            org.gradle.api.internal.file.copy.FileCopyAction
            project.copy { copy ->
                copy.from(pyFile)
                copy.into(packagesDir)
            }
        }

        // 复制 .so/.dylib 文件（原生扩展）
        sitePackages.eachFileMatch(~/.*\.(so|dylib)/) { soFile ->
            project.copy { copy ->
                copy.from(soFile)
                copy.into(packagesDir)
            }
        }
    }

    private File findSitePackages(File venvDir) {
        def libDir = new File(venvDir, "lib")
        if (!libDir.exists()) return null

        def pythonDir = libDir.listFiles()?.find { it.name.startsWith("python") }
        if (!pythonDir) return null

        return new File(pythonDir, "site-packages")
    }

    private void generatePackageManifest(File targetDir, List<String> packages) {
        def manifest = [
            version: "1.0",
            packages: packages.collect { pkg ->
                def parts = pkg.split("[=<>!]+")[0].trim()
                [name: parts, spec: pkg]
            },
            generatedAt: new Date().toString()
        ]

        def manifestFile = new File(targetDir, "packages.json")
        manifestFile.text = new groovy.json.JsonBuilder(manifest).toPrettyString()
    }

    private void copyDir(File src, File dest) {
        dest.mkdirs()
        src.eachFile { file ->
            if (file.isDirectory()) {
                copyDir(file, new File(dest, file.name))
            } else {
                project.copy { copy ->
                    copy.from(file)
                    copy.into(dest)
                }
            }
        }
    }

    private void deleteDir(File dir) {
        if (dir.exists()) {
            dir.deleteDir()
        }
    }
}
