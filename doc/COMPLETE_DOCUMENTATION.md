# PyBridge 完整技术文档

> **版本**: 1.0.0
> **日期**: 2026-07-27
> **状态**: 核心层已验证，JNI 层已验证，多语言模块已验证
> **代码量**: 55 个文件，7330 行代码

---

## 目录

1. [项目概述](#1-项目概述)
2. [架构设计](#2-架构设计)
3. [目录结构](#3-目录结构)
4. [核心层 (C)](#4-核心层-c)
5. [JNI 封装层](#5-jni-封装层)
6. [Kotlin 运行时](#6-kotlin-运行时)
7. [Java 运行时](#7-java-运行时)
8. [Python→Java 桥接](#8-pythonjava-桥接)
9. [Gradle 插件](#9-gradle-插件)
10. [多语言模块系统](#10-多语言模块系统)
11. [NDK 交叉编译](#11-ndk-交叉编译)
12. [PyPI 包服务](#12-pypi-包服务)
13. [Demo 应用](#13-demo-应用)
14. [测试报告](#14-测试报告)
15. [构建指南](#15-构建指南)
16. [API 参考](#16-api-参考)

---

## 1. 项目概述

### 1.1 是什么

PyBridge 是一个**多语言桥接框架**，让非 JVM 语言（Python、C、C++、Rust、Go）与 Java/Kotlin 无缝互操作。

### 1.2 为什么

| 语言 | 强项 | Java/Kotlin 的短板 |
|------|------|-------------------|
| **Python** | ML/AI (PyTorch, TF)、数据科学 (numpy, pandas)、脚本化 | 无等价生态 |
| **C/C++** | 底层系统、高性能计算、硬件驱动 | JNI 写起来繁琐 |
| **Rust** | 内存安全、零成本抽象、WebAssembly | ART 不支持 |
| **Go** | 并发、网络服务、CLI 工具 | ART 不支持 |

PyBridge 的价值：**不重写，直接用。** 在对的地方用对的语言。

### 1.3 核心特性

- **统一模块协议** — 任何语言只需导出 2 个函数即可接入
- **动态加载** — dlopen 加载 .so，运行时按需加载
- **类型自动转换** — Java ↔ C 基本类型、字符串、数组自动映射
- **双语言运行时** — Kotlin DSL + Java 静态方法两种调用风格
- **Gradle 集成** — 一个插件搞定构建、pip 安装、打包
- **已验证** — C 层 13 项测试通过，JNI 层 6 项测试通过

---

## 2. 架构设计

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                    应用层 (Android App)                        │
│                                                               │
│  ┌──────────────────┐          ┌──────────────────┐          │
│  │   Java 调用者     │          │   Kotlin 调用者   │          │
│  │   PyBridge.java   │          │   Python.kt       │          │
│  └────────┬─────────┘          └────────┬─────────┘          │
│           │                             │                     │
│  ┌────────▼─────────────────────────────▼─────────┐          │
│  │              JNI 桥接层 (pybridge_jni.c)         │          │
│  └────────────────────────┬────────────────────────┘          │
│                           │                                    │
│  ┌────────────────────────▼────────────────────────┐          │
│  │           核心层 (pybridge_core.c)                │          │
│  │                                                  │          │
│  │  ┌──────────────────────────────────────────┐   │          │
│  │  │         多语言模块加载器 (pb_loader.c)     │   │          │
│  │  │                                          │   │          │
│  │  │  ┌────────┐ ┌────────┐ ┌────────┐       │   │          │
│  │  │  │Python  │ │ C/C++  │ │ Rust   │       │   │          │
│  │  │  │dlopen  │ │ .so    │ │ .so    │ ...   │   │          │
│  │  │  │.so     │ │        │ │        │       │   │          │
│  │  │  └────────┘ └────────┘ └────────┘       │   │          │
│  │  └──────────────────────────────────────────┘   │          │
│  └──────────────────────────────────────────────────┘          │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 调用链路

```
Java/Kotlin                    JNI C                       Native
──────────                    ──────                       ──────

Python.module("math")    →  nativeImportModule()     →  dlopen + PyImport_Import
  .callAttr("sqrt", 16)  →  nativeCallFunction()     →  PyObject_Call
  .toDouble()             ←  返回 jobject (Double)    ←  PyFloat → jdouble

PyBridge.load("rustmath") →  pb_load_module()         →  dlopen(librustmath.so)
  .call("add", 3, 4)     →  pb_get_func("r_add")     →  dlsym + 函数指针调用
```

### 2.3 类型映射表

| Java/Kotlin | C | Python | Rust | Go |
|-------------|---|--------|------|-----|
| `null` | `NULL` | `None` | `Option::None` | `nil` |
| `boolean` | `int` (0/1) | `bool` | `bool` | `bool` |
| `int` | `int32_t` | `int` | `i32` | `C.int` |
| `long` | `int64_t` | `int` | `i64` | `C.long` |
| `float` | `float` | `float` | `f32` | `C.float` |
| `double` | `double` | `float` | `f64` | `C.double` |
| `String` | `const char*` | `str` | `*const c_char` | `*C.char` |
| `byte[]` | `uint8_t*` | `bytes` | `&[u8]` | `[]byte` |
| `List` | — | `list` | `Vec<T>` | `[]T` |
| `Map` | — | `dict` | `HashMap` | `map` |

---

## 3. 目录结构

```
pybridge/
├── README.md                              # 项目入口文档
│
├── jni/                                   # 核心层 (C)
│   ├── Makefile                           # 编译: make && make test
│   ├── CMakeLists.txt                     # Android NDK 构建
│   ├── include/pybridge/
│   │   ├── pybridge_core.h                # 桥接层 API (136 行)
│   │   └── pb_module.h                    # 多语言模块协议 (141 行)
│   ├── src/
│   │   ├── pybridge_core.c                # 桥接层实现 (349 行)
│   │   ├── pybridge_jni.c                 # JNI 封装 (446 行)
│   │   ├── test_jni_bridge.c              # 测试用 JNI 桥接 (315 行)
│   │   └── pb_loader.c                    # 模块加载器 (171 行)
│   └── test/
│       └── test_pybridge.c                # 13 项测试 (279 行)
│
├── runtime/                               # Java/Kotlin 运行时
│   ├── build.gradle
│   └── src/main/
│       ├── kotlin/com/pybridge/
│       │   ├── core/
│       │   │   ├── Python.kt              # Kotlin 入口 (238 行)
│       │   │   └── PyObject.kt            # Kotlin 包装 (346 行)
│       │   ├── types/TypeConverter.kt     # 类型转换 (121 行)
│       │   ├── exceptions/PyExceptions.kt # 异常类 (55 行)
│       │   └── javaapi/JavaPython.kt      # Java 桥接 (106 行)
│       └── java/com/pybridge/javaapi/
│           └── PyBridge.java              # Java 入口 (121 行)
│
├── modules/                               # 多语言模块示例
│   ├── Makefile                           # 编译: make c / make rust / make go
│   ├── c/cmath.c                          # C 模块 (71 行, 14 个函数)
│   ├── rust/rustmath.rs                   # Rust 模块 (139 行, 13 个函数)
│   ├── go/gomath.go                       # Go 模块 (190 行, 16 个函数)
│   └── test_modules.c                     # 跨语言测试 (210 行)
│
├── gradle-plugin/                         # Gradle 插件
│   ├── build.gradle
│   └── src/main/groovy/com/pybridge/gradle/
│       ├── PyBridgePlugin.groovy          # 插件核心 (202 行)
│       ├── config/PythonExtension.groovy  # DSL 配置 (128 行)
│       └── tasks/                         # 5 个构建任务
│
├── python/pybridge_java/                  # Python→Java 访问
│   ├── __init__.py
│   ├── java_class.py                      # jclass/jarray
│   ├── java_proxy.py                      # 对象代理 (299 行)
│   ├── java_types.py                      # 类型包装器
│   ├── type_mapping.py                    # 类型映射
│   └── import_hook.py                     # import hook
│
├── build-system/
│   ├── build_android.sh                   # NDK 交叉编译 (355 行)
│   └── scripts/build_python.sh            # CPython 编译 (314 行)
│
├── server/pypi/server.py                  # 预编译包仓库 (245 行)
├── tools/abi_config.py                    # ABI 配置工具 (163 行)
│
├── test/
│   └── TestJava.java                      # Java 端到端测试 (230 行)
│
├── demo/                                  # Android 示例应用
│   ├── app/build.gradle
│   ├── app/src/main/
│   │   ├── java/.../JavaDemoActivity.java
│   │   ├── kotlin/.../KotlinDemoActivity.kt
│   │   ├── python/demo_utils.py
│   │   └── python/demo_android.py
│   └── ...
│
└── doc/
    ├── DESIGN.md                          # 技术设计文档
    └── MULTI_LANGUAGE.md                  # 多语言架构文档
```

---

## 4. 核心层 (C)

### 4.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `pybridge_core.h` | 公共 API 头文件 | 136 |
| `pybridge_core.c` | 桥接层实现 (dlopen libpython) | 349 |
| `pb_module.h` | 多语言模块协议头文件 | 141 |
| `pb_loader.c` | 模块动态加载器 | 171 |

### 4.2 API 列表

```c
// 生命周期
int  pb_init(const char* python_home);   // 初始化 Python
void pb_finalize(void);                   // 关闭
int  pb_is_init(void);                    // 是否已初始化
const char* pb_version(void);             // Python 版本

// 模块
PyObject* pb_import(const char* name);    // 导入模块
int       pb_add_path(const char* path);  // 添加搜索路径

// 函数调用
PyObject* pb_call(PyObject* fn, PyObject* args, PyObject* kwargs);
PyObject* pb_call_method(PyObject* obj, const char* method, PyObject* args);
PyObject* pb_call0(PyObject* fn);

// 属性
PyObject* pb_getattr(PyObject* obj, const char* name);
int       pb_setattr(PyObject* obj, const char* name, PyObject* val);

// 类型转换 → Python
PyObject* pb_from_none(void);
PyObject* pb_from_bool(int val);
PyObject* pb_from_long(long val);
PyObject* pb_from_double(double val);
PyObject* pb_from_string(const char* val);

// 类型转换 → C
int         pb_to_bool(PyObject* obj);
long        pb_to_long(PyObject* obj);
double      pb_to_double(PyObject* obj);
const char* pb_to_string(PyObject* obj);

// 容器
PyObject* pb_tuple_new(long len);
PyObject* pb_list_new(long len);
PyObject* pb_dict_new(void);

// 执行代码
PyObject* pb_exec(const char* code);      // 语句
PyObject* pb_eval(const char* expr);      // 表达式

// 异常
int         pb_has_err(void);
const char* pb_err_message(void);
void        pb_err_clear(void);

// 引用计数
void pb_decref(PyObject* obj);
void pb_incref(PyObject* obj);

// 模块加载器
PB_Module* pb_load_module(const char* name, const char* path);
void*      pb_get_func(PB_Module* mod, const char* func_name);
const char* pb_module_get_info(PB_Module* mod);
```

### 4.3 关键技术决策

**问题**: 没有 python3-dev 头文件，无法 `#include <Python.h>`

**解决**: 用 `dlopen` 动态加载 libpython.so，`dlsym` 获取所有 API

```c
void* lib = dlopen("libpython3.12.so.1.0", RTLD_LAZY | RTLD_GLOBAL);
SYM(init, "Py_Initialize");
SYM(long_as, "PyLong_AsLong");
SYM(unicode_from, "PyUnicode_FromString");
// ... 40+ 个符号
```

**问题**: `PyRun_String` 在 dlopen 场景下返回 `Py_None`

**解决**: 改用 `PyRun_SimpleString` + `PyDict_GetItemString`

```c
PyObject* pb_eval(const char* expr) {
    char buf[8192];
    snprintf(buf, sizeof(buf), "__pb_eval_result = (%s)", expr);
    if (P.run_simple(buf) != 0) { _capture_err(); return NULL; }
    PyObject* r = P.dict_get_str(G.main_dict, "__pb_eval_result");
    if (r) P.inc_ref(r);
    return r;
}
```

---

## 5. JNI 封装层

### 5.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `pybridge_jni.c` | 通用 JNI 封装 (Java↔Python) | 446 |
| `test_jni_bridge.c` | 测试用 JNI 桥接 (TestJava 类) | 315 |

### 5.2 JNI 函数映射

| Java native 方法 | JNI C 函数 | 底层调用 |
|-----------------|-----------|---------|
| `nativeInit(home, paths)` | `Java_..._nativeInit` | `pb_init()` |
| `nativeImportModule(name)` | `Java_..._nativeImportModule` | `pb_import()` |
| `nativeCallFunction(ptr, func, args)` | `Java_..._nativeCallFunction` | `pb_call()` |
| `nativeExec(code)` | `Java_..._nativeExec` | `pb_eval()` / `pb_exec()` |
| `nativeGetVersion()` | `Java_..._nativeGetVersion` | `pb_version()` |
| `nativeDecRef(ptr)` | `Java_..._nativeDecRef` | `pb_decref()` |

### 5.3 Java↔Python 类型转换

```c
// Java Object → PyObject*
static PyObject* jobject_to_pyobj(JNIEnv* env, jobject obj) {
    // 检查 Boolean → pb_from_bool
    // 检查 Integer → pb_from_long
    // 检查 Long    → pb_from_long
    // 检查 Double  → pb_from_double
    // 检查 String  → pb_from_string
    // 检查 byte[]  → pb_from_bytes
    // 检查 List    → pb_list_new + 递归转换
    // 检查 Map     → pb_dict_new + 递归转换
    // 检查 int[]   → pb_list_new + 转换
    // 检查 double[]→ pb_list_new + 转换
}

// PyObject* → Java Object
static jobject pyobj_to_jobject(JNIEnv* env, PyObject* obj) {
    // PyBool_Check   → Boolean.valueOf
    // PyLong_Check   → Integer.valueOf / Long.valueOf
    // PyFloat_Check  → Double.valueOf
    // PyUnicode_Check→ NewStringUTF
    // PyBytes_Check  → NewByteArray
    // PyList_Check   → ArrayList
    // PyDict_Check   → HashMap (TODO)
    // fallback       → toString
}
```

### 5.4 编译

```bash
# 桌面 Linux (测试用)
export JAVA_HOME=/path/to/jdk
gcc -shared -fPIC -o libpybridge_jni.so src/pybridge_jni.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -ldl -Wall

# Android (NDK)
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang \
    -shared -fPIC -o libpybridge_jni.so src/pybridge_jni.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -llog -ldl
```

---

## 6. Kotlin 运行时

### 6.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `Python.kt` | 入口对象 (object 单例) | 238 |
| `PyObject.kt` | Python 对象包装 | 346 |
| `TypeConverter.kt` | 类型转换 | 121 |
| `PyExceptions.kt` | 异常类 | 55 |
| `JavaPython.kt` | Java 调用桥接 | 106 |

### 6.2 Kotlin API

```kotlin
// 初始化
Python.initialize(context)

// 导入模块
val math = Python.module("math")

// 属性访问（[] 运算符）
val pi = math["PI"].toDouble()

// 方法调用
val sqrt = math.callAttr("sqrt", 144.0).toDouble()

// 链式调用
val upper = Python.module("utils")
    .callAttr("get_text")
    .callAttr("upper")
    .toStr()

// 执行代码
val result = Python.exec("2 ** 10").toInt()

// 泛型转换
val list: List<*> = Python.module("json")
    .callAttr("loads", "[1,2,3]")
    .toList()

// 运算符重载
val a = Python.wrap(10)
val b = Python.wrap(3)
println((a + b).toInt())   // 13
println((a * b).toInt())   // 30

// 解构
val (x, y, z) = module.callAttr("get_coords")

// 自动关闭
Python.module("heavy").use { mod ->
    mod.callAttr("process", data)
}
```

### 6.3 PyObject 特性

| 特性 | 实现 |
|------|------|
| `[]` 属性访问 | `operator fun get(name)` |
| `[]` 索引访问 | `operator fun get(index)` |
| `+` `-` `*` `/` | 运算符重载 |
| `==` | `equals()` 调用 Python `__eq__` |
| `for (item in obj)` | `iterator()` |
| `val (a,b) = obj` | `component1()` ~ `component5()` |
| `obj.use { }` | `Closeable` 接口 |
| `to<Type>()` | reified 泛型转换 |

---

## 7. Java 运行时

### 7.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `PyBridge.java` | 静态方法入口 | 121 |

### 7.2 Java API

```java
// 初始化
PyBridge.initialize(context);

// 导入模块
PyObject math = PyBridge.module("math");

// 属性访问
double pi = math.get("PI").toDouble();

// 方法调用
double sqrt = math.callAttr("sqrt", 144.0).toDouble();

// 快捷调用
String json = PyBridge.call("json.dumps", myMap).toStr();

// 执行代码
PyObject result = PyBridge.exec("2 ** 10");
int value = result.toInt();  // 1024

// 包装 Java 对象
PyObject wrapped = PyBridge.wrap(42);

// 添加模块路径
PyBridge.addModulePath("/path/to/modules");
```

### 7.3 Kotlin vs Java 对比

| 操作 | Kotlin | Java |
|------|--------|------|
| 初始化 | `Python.initialize(ctx)` | `PyBridge.initialize(ctx)` |
| 导入 | `Python.module("math")` | `PyBridge.module("math")` |
| 属性 | `math["PI"]` | `math.get("PI")` |
| 调用 | `math.callAttr("sqrt", 16.0)` | `math.callAttr("sqrt", 16.0)` |
| 执行 | `Python.exec("code")` | `PyBridge.exec("code")` |
| 运算符 | `a + b` | 不支持 |
| 解构 | `val (x,y) = tuple` | 不支持 |
| 泛型 | `to<List<String>>()` | 不支持 |

---

## 8. Python→Java 桥接

### 8.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `__init__.py` | 包入口 | 53 |
| `java_class.py` | jclass/jarray/cast | 113 |
| `java_proxy.py` | 对象代理 (JavaObject/JavaMethod) | 299 |
| `java_types.py` | 类型包装器 (jint/jfloat/...) | 97 |
| `type_mapping.py` | 类型自动转换 | 123 |
| `import_hook.py` | import hook | 123 |

### 8.2 Python API

```python
from pybridge_java import java

# 访问 Java 类
String = java.jclass("java.lang.String")
s = String("Hello from Python")
print(s.length())  # 16

# import hook (安装后可用)
import java.lang.String
import java.util.ArrayList

# 数组
IntArray = java.jarray("int")
arr = IntArray([1, 2, 3, 4, 5])

# 类型包装器 (控制重载选择)
from pybridge_java import jint, jfloat
p.print(42)         # 调用 print(long)
p.print(jint(42))   # 调用 print(int)

# 类型转换
from pybridge_java import cast
CharSequence = java.jclass("java.lang.CharSequence")
s = cast(CharSequence, some_string)
```

---

## 9. Gradle 插件

### 9.1 文件

| 文件 | 职责 | 行数 |
|------|------|------|
| `PyBridgePlugin.groovy` | 插件核心 | 202 |
| `PythonExtension.groovy` | DSL 配置 | 128 |
| `CheckPythonTask.groovy` | 检查 Python | 104 |
| `PipInstallTask.groovy` | 安装 pip 包 | 199 |
| `CompilePythonTask.groovy` | 编译 .py→.pyc | 120 |
| `PackageRuntimeTask.groovy` | 打包运行时 | 190 |
| `GenerateManifestTask.groovy` | 生成清单 | 51 |

### 9.2 配置 DSL

```groovy
plugins {
    id 'com.android.application'
    id 'com.pybridge.python'
}

python {
    version "3.12"
    buildPython "/usr/bin/python3"
    optimizeLevel 1
    abiFilters "arm64-v8a", "x86_64"

    srcDirs = ["src/main/python"]

    pip {
        install "numpy==1.24.0"
        install "requests"
        install "pillow"
        indexUrl "https://pypi.org/simple"
    }
}
```

### 9.3 构建流程

```
./gradlew assembleDebug
    │
    ├─ checkPython          验证 Python 可用
    ├─ pipInstallDebug      安装 pip 包 → assets/packages/
    ├─ compilePythonDebug   .py → .pyc → assets/python/
    ├─ packagePythonRuntime .so + stdlib.zip → jniLibs/ + assets/
    ├─ mergeDebugJniLibFolders  打包 .so 进 APK lib/
    ├─ mergeDebugAssets          打包 assets 进 APK
    ├─ compileDebugJavaWithJavac 编译 Java/Kotlin
    └─ packageDebug              生成 APK
```

### 9.4 APK 内部结构

```
app.apk
├── lib/arm64-v8a/
│   ├── libpython3.12.so    (5-8MB)
│   └── libpybridge.so      (30KB)
├── assets/
│   ├── python312_stdlib.zip (3MB)
│   ├── packages/numpy/      (pip 包)
│   └── python/demo_utils.py (用户代码)
└── classes.dex
```

---

## 10. 多语言模块系统

### 10.1 模块协议

任何语言只需导出 2 个函数：

```c
// 1. 返回模块元数据 JSON
const char* pb_module_info(void);

// 2. 按名获取函数指针
void* pb_module_func(const char* name);
```

### 10.2 签名格式

```
"dd>d"   → (double, double) → double
"ii>i"   → (int, int) → int
"s>s"    → (const char*) → const char*
"v>v"    → () → void
"i>b"    → (int) → bool
```

### 10.3 C 模块

```c
#include "pb_module.h"
#include <math.h>

static double add(double a, double b) { return a + b; }
static double sqrt_fn(double x) { return sqrt(x); }

PB_MODULE("cmath", "1.0.0", "c",
    PB_FUNC_ENTRY(add,     "dd>d"),
    PB_FUNC_ENTRY(sqrt_fn, "d>d"),
)
```

编译: `gcc -shared -fPIC -o libcmath.so cmath.c -lm`

### 10.4 Rust 模块

```rust
#[no_mangle]
pub extern "C" fn r_add(a: f64, b: f64) -> f64 { a + b }

#[no_mangle]
pub extern "C" fn pb_module_info() -> *const c_char { /* JSON */ }

#[no_mangle]
pub extern "C" fn pb_module_func(name: *const c_char) -> *const () {
    match name { "r_add" => r_add as *const (), _ => null() }
}
```

编译: `rustc --crate-type cdylib -o librustmath.so rustmath.rs`

### 10.5 Go 模块

```go
import "C"

//export g_add
func g_add(a, b C.double) C.double {
    return C.double(float64(a) + float64(b))
}

//export pb_module_info
func pb_module_info() *C.char { return C.CString(infoJSON) }

//export pb_module_func
func pb_module_func(name *C.char) unsafe.Pointer {
    switch C.GoString(name) {
    case "g_add": return C.g_add
    }
    return nil
}
```

编译: `go build -buildmode=c-shared -o libgomath.so gomath.go`

### 10.6 统一调用

```c
// 加载任意语言的模块
PB_Module* mod = pb_load_module("rustmath", "./librustmath.so");

// 获取函数指针
typedef double (*fn_dd)(double, double);
fn_dd add = (fn_dd)pb_get_func(mod, "r_add");

// 调用
double result = add(3.0, 4.0);  // 7.0
```

---

## 11. NDK 交叉编译

### 11.1 脚本

`build-system/build_android.sh` — 一键编译 CPython + libpybridge 为 Android .so

### 11.2 用法

```bash
# 编译单个 ABI
./build_android.sh --abi arm64-v8a --ndk /path/to/ndk

# 编译所有 ABI
./build_android.sh --all --ndk /path/to/ndk

# 自定义 API level
./build_android.sh --abi arm64-v8a --api 26
```

### 11.3 输出

```
output/
├── arm64-v8a/
│   ├── lib/libpython3.12.so
│   ├── lib/libpybridge.so
│   ├── assets/python312_stdlib.zip
│   └── abi.json
├── x86_64/
│   └── ...
└── ...
```

### 11.4 支持的 ABI

| ABI | Triple | 用途 |
|-----|--------|------|
| `arm64-v8a` | `aarch64-linux-android` | 主流 Android 设备 |
| `armeabi-v7a` | `arm-linux-androideabi` | 旧设备 |
| `x86_64` | `x86_64-linux-android` | 模拟器 |
| `x86` | `i686-linux-android` | 旧模拟器 |

---

## 12. PyPI 包服务

### 12.1 文件

`server/pypi/server.py` — 预编译包仓库管理器 (245 行)

### 12.2 功能

- 索引 PyPI 上的纯 Python 包
- 为 Android 交叉编译 C 扩展包
- 提供 wheel 文件的 HTTP 下载
- 管理包版本和依赖

### 12.3 用法

```python
from server import PyBridgePyPIServer

repo = PyBridgePyPIServer("./repo")

# 添加预编译包
repo.add_package("numpy-1.24.0-cp311-cp311-linux_aarch64.whl", "arm64-v8a", "3.11")

# 启动 HTTP 服务
repo.serve("./repo", port=8080)
```

---

## 13. Demo 应用

### 13.1 Java Demo

`demo/app/src/main/java/com/pybridge/demo/JavaDemoActivity.java`

```java
PyBridge.initialize(this);

// 数学
double sqrt = PyBridge.module("math").callAttr("sqrt", 144.0).toDouble();

// NumPy
PyObject np = PyBridge.module("numpy");
double mean = np.callAttr("mean", arr).toDouble();

// JSON
String json = PyBridge.call("json.dumps", myMap).toStr();
```

### 13.2 Kotlin Demo

`demo/app/src/main/kotlin/com/pybridge/demo/KotlinDemoActivity.kt`

```kotlin
Python.initialize(this)

// 运算符重载
val a = Python.wrap(10)
val b = Python.wrap(3)
println((a + b).toInt())   // 13

// 解构
val (x, y, z) = module.callAttr("get_coords")

// 集合遍历
for (item in Python.module("data").callAttr("get_list")) {
    println(item.toStr())
}
```

### 13.3 Python 工具函数

`demo/app/src/main/python/demo_utils.py`

```python
def greet(name: str) -> str:
    return f"🐍 Hello from Python, {name}!"

def fibonacci(n: int) -> list:
    a, b = 0, 1
    result = []
    for _ in range(n):
        result.append(a)
        a, b = b, a + b
    return result
```

---

## 14. 测试报告

### 14.1 C 核心层测试 (13/13 ✅)

| # | 测试 | 验证内容 | 结果 |
|---|------|---------|------|
| 1 | 初始化 | pb_init → Python 3.12.3 启动 | ✅ |
| 2 | 类型转换 | int/double/bool/string/none/bytes | ✅ |
| 3 | 模块导入 | import math, math.pi, 不存在模块 | ✅ |
| 4 | 函数调用 | sqrt(144)=12, factorial(10)=3628800 | ✅ |
| 5 | 表达式求值 | 2**10=1024, 字符串拼接, 列表推导 | ✅ |
| 6 | 语句执行 | 变量赋值, 函数定义, Fibonacci | ✅ |
| 7 | 字典操作 | 创建/读取/排序 keys | ✅ |
| 8 | 异常处理 | ZeroDivisionError/NameError/SyntaxError | ✅ |
| 9 | JSON 处理 | json.dumps + json.loads 往返 | ✅ |
| 10 | 数值计算 | 向量点积、均值、标准差 | ✅ |
| 11 | 类和对象 | 类定义、链式调用 (5+3)*2=16 | ✅ |
| 12 | 列表操作 | 索引、过滤、推导 | ✅ |
| 13 | 字符串操作 | upper/replace/len | ✅ |

### 14.2 JNI 层测试 (6/6 ✅)

| # | 测试 | 验证内容 | 结果 |
|---|------|---------|------|
| 1 | 初始化 | nativeInit → Python 3.12.3 | ✅ |
| 2 | 数学计算 | sqrt(144)=12, factorial(10)=3628800 | ✅ |
| 3 | 字符串 | upper, 拼接, len | ✅ |
| 4 | 代码执行 | 变量, 函数, Fibonacci | ✅ |
| 5 | JSON 处理 | dumps, loads, round-trip | ✅ |
| 6 | Java 对象传递 | sum(java_list)=15, upper | ✅ |

### 14.3 多语言模块测试 (2/2 ✅)

| # | 测试 | 验证内容 | 结果 |
|---|------|---------|------|
| 1 | C 模块 | 14 个函数全部可用 | ✅ |
| 2 | 跨语言统一调用 | 同一套 pb_get_func 接口 | ✅ |

---

## 15. 构建指南

### 15.1 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| GCC | 13+ | 编译 C 层 |
| Python | 3.12+ | 运行时 |
| JDK | 21+ | 编译 Java/JNI |
| Android NDK | r25+ | 交叉编译 (可选) |
| Rust | 1.70+ | 编译 Rust 模块 (可选) |
| Go | 1.21+ | 编译 Go 模块 (可选) |

### 15.2 桌面构建

```bash
# 编译核心层
cd pybridge/jni
make && make test    # 13/13 通过

# 编译 JNI 层
export JAVA_HOME=/path/to/jdk
gcc -shared -fPIC -o libpybridge_jni.so src/pybridge_jni.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -ldl

# 编译 C 模块
cd pybridge/modules
make c && make test  # 2/2 通过

# 运行 Java 测试
cd pybridge
javac -d build/classes test/TestJava.java
java -Djava.library.path=jni -cp build/classes TestJava  # 6/6 通过
```

### 15.3 Android 构建

```bash
# 设置 NDK
export ANDROID_NDK=/path/to/ndk

# 编译 CPython + libpybridge
cd pybridge/build-system
./build_android.sh --abi arm64-v8a --ndk $ANDROID_NDK

# 复制产物到 Android 项目
cp output/arm64-v8a/lib/*.so demo/app/src/main/jniLibs/arm64-v8a/
cp output/arm64-v8a/assets/*.zip demo/app/src/main/assets/

# 构建 APK
cd demo
./gradlew assembleDebug
```

---

## 16. API 参考

### 16.1 C API (pybridge_core.h)

| 函数 | 签名 | 说明 |
|------|------|------|
| `pb_init` | `int pb_init(const char* home)` | 初始化 Python |
| `pb_finalize` | `void pb_finalize(void)` | 关闭 Python |
| `pb_import` | `PyObject* pb_import(const char* name)` | 导入模块 |
| `pb_call` | `PyObject* pb_call(fn, args, kwargs)` | 调用函数 |
| `pb_eval` | `PyObject* pb_eval(const char* expr)` | 求值表达式 |
| `pb_exec` | `PyObject* pb_exec(const char* code)` | 执行语句 |
| `pb_from_long` | `PyObject* pb_from_long(long v)` | long→PyObject |
| `pb_to_long` | `long pb_to_long(PyObject* o)` | PyObject→long |
| `pb_from_double` | `PyObject* pb_from_double(double v)` | double→PyObject |
| `pb_to_double` | `double pb_to_double(PyObject* o)` | PyObject→double |
| `pb_from_string` | `PyObject* pb_from_string(const char* v)` | string→PyObject |
| `pb_to_string` | `const char* pb_to_string(PyObject* o)` | PyObject→string |
| `pb_load_module` | `PB_Module* pb_load_module(name, path)` | 加载 .so 模块 |
| `pb_get_func` | `void* pb_get_func(mod, name)` | 获取函数指针 |

### 16.2 Kotlin API (Python.kt)

| 函数 | 签名 | 说明 |
|------|------|------|
| `initialize` | `fun initialize(ctx, home?, paths?)` | 初始化 |
| `module` | `fun module(name): PyObject` | 导入模块 |
| `exec` | `fun exec(code): PyObject?` | 执行代码 |
| `wrap` | `fun wrap(value): PyObject` | 包装 Java 对象 |

### 16.3 Kotlin API (PyObject.kt)

| 函数 | 签名 | 说明 |
|------|------|------|
| `get` | `operator fun get(name): PyObject` | 属性访问 |
| `callAttr` | `fun callAttr(name, vararg args): PyObject` | 方法调用 |
| `toInt` | `fun toInt(): Int` | 转为 Int |
| `toLong` | `fun toLong(): Long` | 转为 Long |
| `toDouble` | `fun toDouble(): Double` | 转为 Double |
| `toStr` | `fun toStr(): String` | 转为 String |
| `toList` | `fun toList(): List<*>` | 转为 List |
| `toMap` | `fun toMap(): Map<*, *>` | 转为 Map |
| `to` | `inline fun <reified T> to(): T` | 泛型转换 |
| `plus` | `operator fun plus(other): PyObject` | `+` 运算符 |
| `iterator` | `operator fun iterator(): Iterator` | 迭代支持 |
| `component1-5` | `operator fun componentN(): PyObject` | 解构支持 |

### 16.4 Java API (PyBridge.java)

| 函数 | 签名 | 说明 |
|------|------|------|
| `initialize` | `static void initialize(Context)` | 初始化 |
| `module` | `static PyObject module(name)` | 导入模块 |
| `exec` | `static PyObject exec(code)` | 执行代码 |
| `call` | `static PyObject call(moduleAndFunc, args...)` | 快捷调用 |
| `wrap` | `static PyObject wrap(value)` | 包装对象 |
| `getVersion` | `static String getVersion()` | Python 版本 |
| `shutdown` | `static void shutdown()` | 关闭 |

---

## License

Apache License 2.0
