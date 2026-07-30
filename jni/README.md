# Bridge JNI Bridge Layer

Bridge 框架的 C 核心层 + JNI 封装。PyBridge 是其中的 Python 桥接子模块。
子模块层级与符号约定见 [../doc/BRIDGE.md](../doc/BRIDGE.md)。

## 架构

```
Java (ART)
    │
    ▼
┌─────────────────────────────┐
│  JNI Native Method (C)      │
│  ┌─────────────────────┐    │
│  │  bridge_core.c      │    │  Bridge 通用核心 (br_* API)
│  ├─────────────────────┤    │
│  │  pybridge_python.c  │    │  PyBridge Python 子模块 (pb_*)
│  │  - dlopen libpython │    │
│  │  - 解释器生命周期    │    │
│  │  - 类型转换引擎      │    │
│  │  - GIL 管理         │    │
│  │  - 异常桥接         │    │
│  └─────────┬───────────┘    │
│            │                │
│  ┌─────────▼───────────┐    │
│  │  CPython (libpython)│    │
│  │  - 解释器核心       │    │
│  │  - 标准库           │    │
│  │  - 用户模块         │    │
│  └─────────────────────┘    │
└─────────────────────────────┘
```

## 源文件

| 文件 | 职责 |
|------|------|
| `src/bridge_core.c` | Bridge 通用核心：版本、组件状态、模块加载包装（`br_*`） |
| `src/pybridge_python.c` | PyBridge Python 子模块：dlopen libpython + `pb_*` API |
| `src/pybridge_jni.c` | JNI 封装（`com.bridge.py.core.Python`） |
| `src/pybridge_static_proxy.c` | Python→Java 动态代理（`com.bridge.py.proxy`） |
| `src/pb_loader.c` | 多语言模块动态加载器 |

## 桌面编译与测试

无需 python-dev，使用 dlopen 动态加载 libpython（优先 3.13，回退 3.12/3.11）。

```bash
cd jni
make clean && make all && make test
```

预期：**13/13 通过** ✅

## Android NDK 构建

需要 Android NDK r25+ 和预编译的 libpython。

```bash
cd jni
mkdir build && cd build
cmake -DANDROID_ABI=arm64-v8a \
      -DANDROID_NDK=$ANDROID_NDK \
      -DPYTHON_VERSION=3.13 \
      ..
make
```

或使用交叉编译脚本（自动编译 CPython + libpybridge）：

```bash
cd ../build-system
./build_android.sh --abi arm64-v8a
```
