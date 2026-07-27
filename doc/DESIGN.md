# PyBridge 技术设计文档

## 1. 项目概述

PyBridge 是一个类似 Chaquopy 的 Android-Python 混合编程框架，允许在 Android 应用中无缝嵌入和运行 Python 代码。

### 1.1 设计目标

| 目标 | 描述 |
|------|------|
| **零摩擦集成** | 一个 Gradle 插件搞定所有配置 |
| **双向互操作** | Java 调用 Python，Python 调用 Java |
| **类型自动转换** | 基本类型、集合、自定义对象自动映射 |
| **pip 生态复用** | 直接使用 Python 包管理生态 |
| **多架构支持** | arm64, armv7, x86_64, x86 |
| **生产就绪** | 线程安全、异常传播、内存管理 |

### 1.2 与 Chaquopy 对比

| 特性 | Chaquopy | PyBridge |
|------|----------|----------|
| 构建系统 | Gradle Plugin | Gradle Plugin |
| Python 版本 | 3.8-3.12 | 3.11+ |
| JNI 桥接 | 闭源 | 开源 (C) |
| 类型转换 | 自动 | 自动 + 可扩展 |
| pip 支持 | ✅ | ✅ |
| 价格 | 商业 | 开源 |

## 2. 系统架构

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                        Android App                            │
│                                                               │
│  ┌────────────────┐         ┌────────────────────────────┐   │
│  │  Java/Kotlin   │         │  Python Runtime             │   │
│  │  Application   │         │  ┌──────────────────────┐  │   │
│  │                │         │  │  User Python Code     │  │   │
│  │  Python.java   │◄───────►│  │  (compiled .pyc)      │  │   │
│  │  PyObject.java │  JNI    │  ├──────────────────────┤  │   │
│  │                │ Bridge  │  │  Pip Packages         │  │   │
│  │  TypeConverter │         │  │  (numpy, requests...) │  │   │
│  └────────────────┘         │  ├──────────────────────┤  │   │
│                             │  │  pybridge_java        │  │   │
│                             │  │  (Python→Java bridge) │  │   │
│                             │  └──────────────────────┘  │   │
│                             └────────────────────────────┘   │
├──────────────────────────────────────────────────────────────┤
│                    Gradle Build System                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ Python   │  │ pip      │  │ Compile  │  │ Package  │    │
│  │ Check    │  │ Install  │  │ .py→.pyc │  │ Runtime  │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
├──────────────────────────────────────────────────────────────┤
│                    Native Layer (C/C++)                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  libpybridge.so                                       │   │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────────────┐ │   │
│  │  │ JNI Bridge │ │ Type       │ │ GIL Manager      │ │   │
│  │  │ (入口)     │ │ Converter  │ │ (线程安全)       │ │   │
│  │  └────────────┘ └────────────┘ └──────────────────┘ │   │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────────────┐ │   │
│  │  │ Module     │ │ Exception  │ │ Object Wrapper   │ │   │
│  │  │ Manager    │ │ Handler    │ │ (Java↔Py引用)    │ │   │
│  │  └────────────┘ └────────────┘ └──────────────────┘ │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  libpython3.11.so (CPython 解释器)                   │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 数据流

```
Java 调用 Python:
  Java Code
    → Python.callAttr("func", args)
    → JNI nativeCallFunction()
    → GIL acquire
    → TypeConverter.java_to_python(args)
    → PyObject_Call(func, py_args)
    → TypeConverter.python_to_java(result)
    → GIL release
    → Java result

Python 调用 Java:
  Python Code
    → java.String("hello")
    → pybridge_java.import_hook
    → JNI create_java_object()
    → Java constructor
    → PyObject wrapper
```

## 3. 核心模块设计

### 3.1 Gradle 插件 (`gradle-plugin/`)

**职责：**
- 检测/配置 Python 环境
- 安装 pip 依赖到 APK
- 编译 .py → .pyc
- 打包运行时（libpython + stdlib）

**配置 DSL：**
```groovy
python {
    version "3.11"
    buildPython "/usr/bin/python3"
    optimizeLevel 1
    abiFilters "arm64-v8a", "x86_64"

    pip {
        install "numpy==1.24.0"
        install "requests"
        indexUrl "https://pypi.org/simple"
    }
}
```

**构建流程：**
```
checkPython → pipInstall → compilePython → packagePythonRuntime → generateManifest
     │              │              │                  │                    │
     ▼              ▼              ▼                  ▼                    ▼
  验证Python    安装pip包     编译.pyc         打包libpython        生成清单
```

### 3.2 JNI 桥接层 (`jni/`)

**核心文件：**

| 文件 | 职责 |
|------|------|
| `pybridge_jni.c` | 解释器生命周期、模块管理、函数调用 |
| `pybridge_types.c` | 类型转换引擎、Java 对象包装 |
| `pybridge_gil.c` | GIL 管理、线程安全 |
| `pybridge_exception.c` | 异常传播（Python→Java） |
| `pybridge_python.c` | JNI native 方法实现 |

**GIL 管理策略：**
```c
// 推荐使用方式：with_gil 回调模式
PyObject* result = pybridge_with_gil(my_callback, my_data);

// 或手动管理
pybridge_gil_acquire();
// ... Python 操作 ...
pybridge_gil_release();
```

### 3.3 类型转换系统

**Java → Python 映射表：**

```
Java 类型          Python 类型     转换方式
─────────────────────────────────────────
null               None            直接映射
boolean            bool            PyBool_FromLong
byte               int             PyLong_FromLong
short              int             PyLong_FromLong
int                int             PyLong_FromLong
long               int             PyLong_FromLongLong
float              float           PyFloat_FromDouble
double             float           PyFloat_FromDouble
char               str(1)          PyUnicode_FromString
String             str             PyUnicode_FromString
byte[]             bytes           PyBytes_FromStringAndSize
List               list            PyList_New + 转换元素
Map                dict            PyDict_New + 转换键值
Object[]           tuple           PyTuple_New + 转换元素
其他对象           PyJavaObject    JNI 全局引用包装
```

**Python → Java 映射表：**

```
Python 类型        Java 类型       转换方式
─────────────────────────────────────────
None               null            NULL
bool               boolean         PyObject_IsTrue
int (小)           int             PyLong_AsLong
int (大)           long            PyLong_AsLongLong
float              double          PyFloat_AsDouble
str                String          PyUnicode_AsUTF8 + NewStringUTF
bytes              byte[]          PyBytes_AsString + NewByteArray
list               ArrayList       迭代转换
tuple              ArrayList       迭代转换
dict               LinkedHashMap   迭代键值转换
set                LinkedHashSet   迭代转换
```

### 3.4 Java 对象包装 (PyJavaObject)

在 Python 中表示 Java 对象的核心类型：

```c
typedef struct {
    PyObject_HEAD
    jobject java_ref;    // JNI 全局引用
    char* class_name;    // Java 类名
} PyJavaObject;
```

**生命周期管理：**
- 创建：Java 对象传入 Python 时，创建全局 JNI 引用
- 访问：通过 `__getattr__` 调用 Java 方法/字段
- 销毁：Python GC 回收时，删除 JNI 全局引用

### 3.5 Python 端 Java 访问 (`python/pybridge_java/`)

**Import Hook 机制：**
```python
# 安装 hook 后可以直接 import
import java.lang.String
s = java.lang.String("Hello")

from java.util import ArrayList
list = ArrayList()
list.add("item")
```

**核心类：**
- `JavaProxy` — Java 类代理管理器
- `JavaObject` — Java 对象包装
- `JavaMethod` — 方法调用代理（支持重载）
- `JavaField` — 字段访问代理
- `JavaIterator` — 迭代器适配器

## 4. CPython 交叉编译

### 4.1 编译流程

```
下载 CPython 源码
    ↓
配置交叉编译（--host=aarch64-linux-android）
    ↓
设置 NDK 工具链（CC, CXX, AR, RANLIB）
    ↓
编译（make -j$(nproc)）
    ↓
精简标准库（排除 test, tkinter, idle 等）
    ↓
打包为 .zip
```

### 4.2 关键配置

```bash
# 跳过运行时检测（交叉编译无法运行目标程序）
ac_cv_file__dev_ptmx=no
ac_cv_file__dev_ptc=no
ac_cv_buggy_getaddrinfo=no

# 启用共享库
--enable-shared
--enable-optimizations
--with-lto

# 禁用不需要的模块
--without-ensurepip
--disable-test-modules
--disable-ipv6
```

## 5. 内存管理

### 5.1 引用计数

- Java→Python：`Py_INCREF` 传递时增加引用
- Python→Java：使用 JNI 全局引用（`NewGlobalRef`）
- 清理：`PyObject.close()` 或 GC 时自动释放

### 5.2 循环引用

- Java GC 和 Python GC 独立运行
- 通过弱引用（`weakref` / `WeakGlobalRef`）打破循环
- 提供 `close()` 方法手动释放

## 6. 线程安全

### 6.1 GIL 管理

```
线程 A                    线程 B
    │                         │
    ├─ gil_acquire()          │
    │  (获取成功)              │
    │  执行 Python 代码        ├─ gil_acquire()
    │                         │  (阻塞等待)
    ├─ gil_release()          │
    │                         │  (获取成功)
    │                         │  执行 Python 代码
    │                         ├─ gil_release()
```

### 6.2 Android 主线程保护

```java
// Python 调用必须在后台线程
new Thread(() -> {
    Python py = Python.getInstance();
    PyObject result = py.getModule("heavy_computation").callAttr("run");
    runOnUiThread(() -> updateUI(result));
}).start();
```

## 7. 安全考虑

- Python 代码打包在 APK 中，运行在应用沙箱内
- 不允许运行时动态下载执行代码
- pip 包在构建时验证签名
- JNI 层防止空指针和类型混淆

## 8. 性能优化

- **编译优化**：`-O2` + LTO
- **标准库精简**：排除不需要的模块（减小 40% 体积）
- **延迟加载**：模块按需导入
- **类型缓存**：已解析的类/方法缓存复用
- **零拷贝**：基本类型直接转换，无需序列化

## 9. 未来规划

- [ ] Kotlin DSL 支持
- [ ] Python 3.12/3.13 支持
- [ ] 多 Python 版本共存
- [ ] 远程调试支持
- [ ] 性能分析工具
- [ ] CI/CD 集成模板
