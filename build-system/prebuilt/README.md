# Bridge / PyBridge 预编译原生库

Bridge 框架（PyBridge 为其 Python 子模块）的 NDK 交叉编译产物放入此目录，
Gradle 插件会自动检测并打包。

## 目录结构

```
prebuilt/
├── arm64-v8a/
│   ├── libpython3.13.so
│   └── libpybridge.so
├── x86_64/
│   ├── libpython3.13.so
│   └── libpybridge.so
└── assets/
    └── python313_stdlib.zip
```

## 获取预编译库

### 方式 1: 自己编译

```bash
cd build-system
export ANDROID_NDK=/path/to/ndk
./build_android.sh --all
```

编译后将 `output/` 目录内容复制到 `prebuilt/`。

> `libpybridge.so` 现包含 Bridge 通用层（`bridge_core.c`）+ Python 子模块
> （`pybridge_python.c`）+ 模块加载器（`pb_loader.c`）；运行时优先加载
> `libpython3.13`，回退 3.12/3.11。

### 方式 2: 下载预编译版本 (未来提供)

```bash
# 从 Bridge Releases 下载
curl -L https://github.com/caosbad/Bridge/releases/download/v2.0.0/bridge-android-libs.tar.gz | tar xz
```
