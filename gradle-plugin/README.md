# PyBridge Gradle Plugin

在 Android 构建流程中注入 Python 编译和打包步骤。

## 使用方式

```groovy
// build.gradle (app)
plugins {
    id 'com.android.application'
    id 'com.pybridge.python'
}

python {
    version "3.12"
    buildPython "/usr/bin/python3"
    optimizeLevel 1
    abiFilters "arm64-v8a", "x86_64"

    pip {
        install "numpy==1.24.0"
        install "requests"
    }
}
```

## 构建流程

```
buildNative  →  pipInstall  →  compilePython  →  packageRuntime
     │               │               │                 │
     ▼               ▼               ▼                 ▼
libpython.so    安装 pip 包     .py → .pyc      打包到 APK
libpybridge.so
```

## 任务说明

| 任务 | 说明 |
|------|------|
| `buildPyBridgeNative*` | NDK 交叉编译 libpython.so + libpybridge.so |
| `pyBridgePipInstall*` | 安装 pip 依赖到 build 目录 |
| `pyBridgeCompile*` | 编译 .py → .pyc |
| `pyBridgePackage*` | 打包 .so + stdlib 到 APK 资源目录 |
| `cleanPython` | 清理所有构建产物 |