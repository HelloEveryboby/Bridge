package com.pybridge.gradle.tasks

import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.*

/**
 * 打包 Python 运行时（libpython + 标准库）到 JNI 库目录
 *
 * 负责：
 * 1. 从预编译仓库下载对应架构的 libpython.so
 * 2. 提取 Python 标准库（去除非必要模块减小体积）
 * 3. 打包为 .zip 用于 APK assets
 * 4. 配置 JNI 库路径
 */
abstract class PackageRuntimeTask extends DefaultTask {

    @Input
    abstract Property<String> getPythonVersion()

    @Input
    abstract ListProperty<String> getAbiFilters()

    @OutputDirectory
    abstract DirectoryProperty getOutputDir()

    @Input
    abstract Property<String> getPythonHome()

    @TaskAction
    void packageRuntime() {
        def version = pythonVersion.get()
        def abis = abiFilters.get()
        def outputDirectory = outputDir.get().asFile

        logger.lifecycle("📦 Packaging Python ${version} runtime for: ${abis}")

        outputDirectory.mkdirs()

        for (abi in abis) {
            def abiDir = new File(outputDirectory, abi)
            abiDir.mkdirs()

            logger.lifecycle("   Processing ABI: ${abi}")

            // 1. 下载/定位 libpython.so
            def libPython = findOrDownloadLibPython(version, abi)
            project.copy { copy ->
                copy.from(libPython)
                copy.into(abiDir)
            }

            // 2. 打包 Python 标准库
            def stdlibZip = packageStdLib(version, abi, abiDir)
            project.copy { copy ->
                copy.from(stdlibZip)
                copy.into(new File(abiDir, "assets"))
            }

            // 3. 复制 JNI 桥接库
            def bridgeLib = findBridgeLib(abi)
            if (bridgeLib) {
                project.copy { copy ->
                    copy.from(bridgeLib)
                    copy.into(abiDir)
                }
            }
        }

        // 生成运行时配置
        generateRuntimeConfig(outputDirectory, version, abis)

        logger.lifecycle("✅ Runtime packaged to ${outputDirectory.absolutePath}")
    }

    private File findOrDownloadLibPython(String version, String abi) {
        // 尝试本地缓存
        def cacheDir = new File(project.gradle.gradleUserHomeDir, "pybridge/python-cache")
        def cachedLib = new File(cacheDir, "${version}/${abi}/libpython${version}.so")

        if (cachedLib.exists()) {
            logger.lifecycle("     Using cached libpython: ${cachedLib}")
            return cachedLib
        }

        // 下载预编译版本
        logger.lifecycle("     Downloading libpython ${version} for ${abi}...")
        cachedLib.parentFile.mkdirs()

        def archMap = [
            "arm64-v8a"   : "aarch64-linux-android",
            "armeabi-v7a"  : "arm-linux-androideabi",
            "x86_64"      : "x86_64-linux-android",
            "x86"         : "i686-linux-android"
        ]

        def triple = archMap[abi]
        if (!triple) {
            throw new RuntimeException("Unsupported ABI: ${abi}")
        }

        def url = "https://pybridge.dev/prebuilt/python/${version}/${abi}/libpython${version}.so"

        // 使用 Gradle 下载
        project.ant.get(src: url, dest: cachedLib)

        return cachedLib
    }

    private File packageStdLib(String version, String abi, File abiDir) {
        def stdlibZip = new File(abiDir, "python${version.replace('.', '')}_stdlib.zip")

        // 定位本地 Python 安装的标准库
        def pythonHome = pythonHome.getOrNull()
        def stdlibDir = null

        if (pythonHome) {
            stdlibDir = new File(pythonHome, "lib/python${version}")
        }

        if (!stdlibDir?.exists()) {
            // 尝试常见路径
            def candidates = [
                "/usr/lib/python${version}",
                "/usr/local/lib/python${version}",
                "/opt/homebrew/lib/python${version}"
            ]
            stdlibDir = candidates.collect { new File(it) }.find { it.exists() }
        }

        if (!stdlibDir?.exists()) {
            throw new RuntimeException("Cannot find Python ${version} standard library")
        }

        // 创建精简版标准库 zip
        project.ant.zip(destfile: stdlibZip) {
            fileset(dir: stdlibDir) {
                // 排除不需要的模块（减小 APK 体积）
                exclude(name: "**/test/**")
                exclude(name: "**/tests/**")
                exclude(name: "**/idle*/**")
                exclude(name: "**/tkinter/**")
                exclude(name: "**/turtle*")
                exclude(name: "**/lib2to3/**")
                exclude(name: "**/ensurepip/**")
                exclude(name: "**/distutils/**")
                exclude(name: "**/__pycache__/**")
                exclude(name: "**/*.pyc")
                exclude(name: "**/*.pyo")
                exclude(name: "**/plat-*/**")
            }
        }

        logger.lifecycle("     StdLib zip: ${stdlibZip.length() / 1024}KB")
        return stdlibZip
    }

    private File findBridgeLib(String abi) {
        // 查找编译好的 JNI 桥接库
        def bridgeLib = project.file("jni/libs/${abi}/libpybridge.so")
        if (bridgeLib.exists()) {
            return bridgeLib
        }

        // 尝试从构建目录查找
        def buildLib = new File(project.buildDir, "pybridge/jni/${abi}/libpybridge.so")
        if (buildLib.exists()) {
            return buildLib
        }

        logger.warn("   ⚠ Bridge library not found for ${abi}, will build from source")
        return null
    }

    private void generateRuntimeConfig(File outputDir, String version, List<String> abis) {
        def config = [
            pythonVersion: version,
            abis: abis,
            stdlibPath: "python${version.replace('.', '')}_stdlib.zip",
            libPython: "libpython${version}.so",
            bridgeLib: "libpybridge.so",
            packagedAt: new Date().toString()
        ]

        new File(outputDir, "runtime_config.json").text =
            new groovy.json.JsonBuilder(config).toPrettyString()
    }
}
