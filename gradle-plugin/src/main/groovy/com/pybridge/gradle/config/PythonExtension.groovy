package com.pybridge.gradle.config

import org.gradle.api.Project
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.MapProperty

/**
 * python {} 扩展配置块
 *
 * 用法:
 * python {
 *     version "3.11"
 *     buildPython "/usr/bin/python3"
 *     srcDirs = ["src/main/python"]
 *     optimizeLevel 1
 *     pip {
 *         indexUrl "https://pypi.org/simple"
 *         install "numpy==1.24.0"
 *         install "requests"
 *         install "pillow", "--no-binary", "pillow"
 *     }
 * }
 */
class PythonExtension {

    final Property<String> version
    final Property<String> buildPython
    final Property<Integer> optimizeLevel
    final Property<String> pythonHome
    final Property<String> ndkPath
    final ListProperty<String> srcDirs
    final ListProperty<String> abiFilters
    final PipConfig pip
    final MapProperty<String, String> environment

    private final Project project

    PythonExtension(Project project) {
        this.project = project
        def objects = project.objects

        version = objects.property(String).convention("3.11")
        buildPython = objects.property(String).convention("python3")
        optimizeLevel = objects.property(Integer).convention(1)
        pythonHome = objects.property(String).convention("")
        ndkPath = objects.property(String).convention("")
        srcDirs = objects.listProperty(String).convention(["src/main/python"])
        abiFilters = objects.listProperty(String).convention(["arm64-v8a", "x86_64"])
        environment = objects.mapProperty(String, String).convention([:])
        pip = new PipConfig(project)
    }

    // DSL: version "3.11"
    void version(String v) {
        version.set(v)
    }

    // DSL: buildPython "/path/to/python"
    void buildPython(String path) {
        buildPython.set(path)
    }

    // DSL: optimizeLevel 2
    void optimizeLevel(int level) {
        optimizeLevel.set(level)
    }

    // DSL: srcDirs = ["src/main/python"]
    void setSrcDirs(List<String> dirs) {
        srcDirs.set(dirs)
    }

    // DSL: abiFilters "arm64-v8a", "x86_64"
    void abiFilters(String... filters) {
        abiFilters.set(filters.toList())
    }

    // DSL: pythonHome "/path/to/python/home"
    void pythonHome(String home) {
        pythonHome.set(home)
    }

    // DSL: ndkPath "/path/to/ndk"
    void ndkPath(String path) {
        ndkPath.set(path)
    }

    // DSL: pip { ... }
    void pip(Closure<?> closure) {
        closure.delegate = pip
        closure.resolveStrategy = Closure.DELEGATE_FIRST
        closure.call()
    }

    // DSL: environment KEY: "value"
    void environment(Map<String, String> env) {
        environment.set(env)
    }
}

/**
 * pip {} 子配置块
 */
class PipConfig {
    final ListProperty<String> packages
    final Property<String> indexUrl
    final Property<String> targetAbi
    final ListProperty<String> extraArgs
    final Property<Boolean> onlyBinary

    PipConfig(Project project) {
        def objects = project.objects
        packages = objects.listProperty(String).convention([])
        indexUrl = objects.property(String).convention("https://pypi.org/simple")
        targetAbi = objects.property(String).convention("")
        extraArgs = objects.listProperty(String).convention([])
        onlyBinary = objects.property(Boolean).convention(true)
    }

    // DSL: install "numpy==1.24.0"
    void install(String... pkg) {
        packages.addAll(pkg)
    }

    // DSL: indexUrl "https://custom.pypi.org/simple"
    void indexUrl(String url) {
        indexUrl.set(url)
    }

    // DSL: onlyBinary false
    void onlyBinary(boolean value) {
        onlyBinary.set(value)
    }
}
