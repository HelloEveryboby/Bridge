# PyBridge 技术设计文档

## 1. 项目概述

PyBridge 是一个多语言桥接框架，核心目标是将 Python 运行时嵌入 Android 应用，并提供双向互操作能力。

### 1.1 设计目标

| 目标 | 描述 |
|------|------|
| **零摩擦集成** | 一个 Gradle 插件搞定所有配置 |
| **双向互操作** | Java/Kotlin 调用 Python，Python 调用 Java/Android API |
| **类型自动转换** | 基本类型、集合、自定义对象自动映射 |
| **pip 生态复用** | 直接使用 Python 包管理生态 |
| **多架构支持** | arm64-v8a, armeabi-v7a, x86_64, x86 |
| **多语言扩展** | DLC 协议支持 C/C++/Rust/Go/JS/TS/C#/SQL |

### 1.2 与 Chaquopy 对比

| 特性 | Chaquopy | PyBridge |
|------|----------|----------|
| 构建系统 | Gradle Plugin | Gradle Plugin |
| Python 版本 | 3.8-3.12 | 3.12 |
| JNI 桥接 | 闭源 | 开源 (C) |
| 类型转换 | 自动 | 自动 + 可扩展 |
| Static Proxy | 内置 | @PyClass 装饰器 |
| pip 支持 | 是 | 是 |
| 价格 | 商业 | 开源 (Apache 2.0) |

## 2. 系统架构

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                        Android App                            │
│  ┌──────────────────────┐   ┌────────────────────────────┐   │
│  │  Java/Kotlin         │   │  Python Runtime             │   │
│  │  Python.kt / PyBridge│   │  user .py scripts           │   │
│  │  PyObject.kt         │◄──│  pybridge_java (→Java API)  │   │
│  │  PyBridgeProxyHandler│JNI│  pip packages               │   │
│  └──────────────────────┘   └────────────────────────────┘   │
├──────────────────────────────────────────────────────────────┤
│  Gradle Plugin: buildNative → pipInstall → compilePy → pkg  │
├──────────────────────────────────────────────────────────────┤
│  libpybridge.so (C 核心)                                     │
│  ├── pybridge_core.c    dlopen libpython + dlsym API        │
│  ├── pybridge_jni.c     Java/Kotlin ↔ Python 类型转换       │
│  ├── pybridge_static_proxy.c  Python→Java 动态代理          │
│  └── pb_loader.c        多语言模块动态加载                   │
│  libpython3.12.so (CPython 解释器)                           │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 数据流

```
Java 调用 Python:
  Python.module("mymod") → nativeImportModule() → pb_import()
  mod.callAttr("func", args) → nativeCallAttr() → pb_call_method()
  TypeConverter 自动转换参数和返回值

Python 调用 Java:
  jclass("android.util.Log") → ctypes → libpybridge.so → JNI
  Log.d("TAG", "msg") → _call_java_method() → JNI → Java
```

## 3. 核心模块

### 3.1 C 核心层 (`jni/`)

| 文件 | 职责 |
|------|------|
| `pybridge_core.c` | 动态加载 libpython，dlsym 获取全部 CPython API |
| `pybridge_jni.c` | JNI 入口：解释器生命周期、模块管理、类型转换 |
| `pybridge_static_proxy.c` | Python→Java 动态代理 JNI 实现 |
| `pb_loader.c` | 多语言模块动态加载器 |

**设计要点**：`pybridge_core.h` 不依赖 `Python.h`，所有 Python API 通过 `pb_*` 前缀的函数间接调用，例如 `pb_import()`、`pb_call()`、`pb_from_string()`。

### 3.2 Kotlin 运行时 (`runtime/`)

| 文件 | 职责 |
|------|------|
| `Python.kt` | 入口：`initialize()`、`module()`、`exec()`、`shutdown()` |
| `PyObject.kt` | Python 对象包装器：运算符重载、类型转换、Closeable |
| `PyBridgeProxyHandler.kt` | Java 动态代理 → Python 回调转发 |
| `TypeConverter.kt` | Kotlin ↔ Python 类型自动转换 |
| `PyBridge.java` | 纯 Java API：Builder 模式封装 |

### 3.3 Python 端 Java 访问 (`python/pybridge_java/`)

| 文件 | 职责 |
|------|------|
| `java_class.py` | `jclass()` 和 `jarray()` 核心实现 |
| `java_proxy.py` | JavaObject、JavaMethod、JavaField 代理 + ctypes JNI 调用 |
| `java_types.py` | Java 类型映射（jboolean、jint、jlong 等） |
| `type_mapping.py` | 自动类型转换：`auto_convert`、`java_to_python`、`python_to_java` |
| `import_hook.py` | Python import hook：允许 `import java.lang.String` 语法 |
| `static_proxy.py` | `@PyClass` 装饰器：Python 类实现 Java 接口 |
| `pybridge_logcat.py` | stdout/stderr → Android Logcat 重定向 |

### 3.4 Gradle 插件 (`gradle-plugin/`)

**构建流程**：`buildNative → pipInstall → compilePython → packageRuntime`

**配置 DSL**：
```groovy
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

### 3.5 DLC 多语言连接器 (`dlc/`)

统一协议 `DLC_Value` + `DLC_Connector` 接口，支持 C/C++/Rust/Go/JavaScript/TypeScript/C#/SQL 共 8 种语言通过统一协议与宿主互操作。

## 4. 类型转换

### Java → Python

| Java | Python |
|------|--------|
| null | None |
| Boolean | bool |
| Integer/Long | int |
| Float/Double | float |
| String | str |
| byte[] | bytes |
| List | list |
| Map | dict |

### Python → Java

| Python | Java |
|--------|------|
| None | null |
| bool | Boolean |
| int | Integer/Long (自动选择) |
| float | Double |
| str | String |
| bytes | byte[] |
| list/tuple | ArrayList |
| dict | HashMap |

## 5. Static Proxy 机制

`@PyClass` 装饰器允许 Python 类实现 Java 接口：

```python
@PyClass("java.lang.Runnable")
class MyRunnable:
    def run(self):
        print("Hello from Python!")

runnable = MyRunnable()
Thread = jclass("java.lang.Thread")
Thread(runnable).start()
```

工作流程：
1. Python 实例化时创建 `PyClassProxy`，调用 JNI 创建 Java 动态代理
2. Java 调用代理方法时，`PyBridgeProxyHandler` 通过 JNI 回调 Python
3. 返回值自动转换

## 6. CPython 交叉编译

通过 `build-system/build_android.sh` 脚本，使用 Android NDK 交叉编译 CPython 为各 ABI 的 `.so` 文件，并精简标准库打包为 `.zip`。

## 7. 内存管理

- Java→Python：`pb_incref`/`pb_decref` 管理引用计数
- Python→Java：JNI 全局引用（`NewGlobalRef`/`DeleteGlobalRef`）
- `PyObject` 实现 `Closeable`，支持 `use {}` 自动释放

## 8. 线程安全

- Python 调用必须在持有 GIL 的情况下进行
- Android 主线程不应直接调用 Python（避免阻塞 UI）
- 建议在后台线程中执行 Python 代码