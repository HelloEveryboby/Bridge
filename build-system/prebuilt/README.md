# PyBridge 预编译原生库

将 NDK 交叉编译产物放入此目录，Gradle 插件会自动检测并打包。

## 目录结构

```
prebuilt/
├── arm64-v8a/
│   ├── libpython3.12.so
│   └── libpybridge.so
├── x86_64/
│   ├── libpython3.12.so
│   └── libpybridge.so
└── assets/
    └── python312_stdlib.zip
```

## 获取预编译库

### 方式 1: 自己编译

```bash
cd build-system
export ANDROID_NDK=/path/to/ndk
./build_android.sh --all
```

编译后将 `output/` 目录内容复制到 `prebuilt/`。

### 方式 2: 下载预编译版本 (未来提供)

```bash
# 从 PyBridge Releases 下载
curl -L https://github.com/caosbad/Bridge/releases/download/v1.0.0/pybridge-android-libs.tar.gz | tar xz
```