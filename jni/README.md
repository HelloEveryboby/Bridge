# PyBridge JNI Bridge Layer

## 架构

```
Java (ART)
    │
    ▼
┌─────────────────────────────┐
│  JNI Native Method (C/C++) │
│  ┌─────────────────────┐   │
│  │  pybridge_jni.c     │   │
│  │  - Python 生命周期  │   │
│  │  - 类型转换引擎     │   │
│  │  - GIL 管理         │   │
│  │  - 异常桥接         │   │
│  └─────────┬───────────┘   │
│            │               │
│  ┌─────────▼───────────┐   │
│  │  CPython (libpython)│   │
│  │  - 解释器核心       │   │
│  │  - 标准库           │   │
│  │  - 用户模块         │   │
│  └─────────────────────┘   │
└─────────────────────────────┘
```

## 编译

需要 Android NDK r25+ 和预编译的 libpython。

```bash
cd jni
mkdir build && cd build
cmake -DANDROID_ABI=arm64-v8a \
      -DANDROID_NDK=$ANDROID_NDK \
      -DPYTHON_VERSION=3.11 \
      ..
make
```
