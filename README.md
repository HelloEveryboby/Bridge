# PyBridge — 多语言桥接框架

让 Python / C / C++ / Java / JavaScript / C# / SQL / TypeScript / Rust 无缝互操作。

---

## 快速开始（3 步跑通）

### 前置依赖

```bash
# 必须
sudo apt install gcc python3

# JNI 层需要 JDK（可选，没有也能测 C 层）
# 下载: https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-x64_bin.tar.gz
# 解压到 ~/jdk/jdk-21.0.2/
```

### 步骤 1: 编译并测试 C 核心层

```bash
cd jni
make clean && make all && make test
```

预期输出：**13/13 通过** ✅

验证内容：Python 初始化、类型转换（int/double/bool/string）、模块导入、函数调用（sqrt/factorial）、表达式求值、语句执行、字典操作、异常处理、JSON 处理、数值计算、类和对象、列表操作、字符串操作。

### 步骤 2: 编译并测试 C 模块（多语言模块系统）

```bash
cd ../modules
make clean && make all && make test
```

预期输出：**2/2 通过** ✅

验证内容：C 模块 14 个数学函数、跨语言统一调用接口。

### 步骤 3: 编译 JNI 层并运行 Java 测试（需要 JDK）

```bash
# 设置 JDK
export JAVA_HOME=~/jdk/jdk-21.0.2
export PATH=$JAVA_HOME/bin:$PATH

# 编译 JNI 桥接
cd ../jni
gcc -shared -fPIC -O2 -o libtest_jni.so src/test_jni_bridge.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -ldl -Wall

# 编译并运行 Java 测试
cd ..
mkdir -p build/classes
javac -d build/classes test/TestJava.java
LD_LIBRARY_PATH=jni java -Djava.library.path=jni -cp build/classes TestJava
```

预期输出：**6/6 通过** ✅

验证内容：Java→Python 初始化、数学计算、字符串操作、代码执行、JSON 处理、Java 对象传递给 Python。

---

## 项目结构

```
pybridge/
├── jni/                           ← C 核心层 + JNI 封装
│   ├── include/pybridge/
│   │   ├── pybridge_core.h        ← 桥接层 API 头文件
│   │   └── pb_module.h            ← 多语言模块协议头文件
│   ├── src/
│   │   ├── pybridge_core.c        ← 桥接层实现 (dlopen libpython)
│   │   ├── pybridge_jni.c         ← JNI 封装 (Java↔Python 类型转换)
│   │   ├── test_jni_bridge.c      ← 测试用 JNI 桥接
│   │   └── pb_loader.c            ← 模块动态加载器
│   ├── test/test_pybridge.c       ← 13 项 C 层测试
│   ├── Makefile                   ← 编译: make && make test
│   └── CMakeLists.txt             ← Android NDK 构建
│
├── modules/                       ← 多语言模块示例
│   ├── c/cmath.c                  ← C 模块 (14 个数学函数)
│   ├── rust/rustmath.rs           ← Rust 模块 (13 个函数)
│   ├── go/gomath.go               ← Go 模块 (16 个函数)
│   ├── test_modules.c             ← 跨语言测试
│   └── Makefile                   ← 编译: make c / make rust / make go
│
├── runtime/                       ← Java/Kotlin 运行时
│   ├── src/main/kotlin/           ← Kotlin API (Python.kt, PyObject.kt)
│   ├── src/main/java/             ← Java API (PyBridge.java)
│   └── build.gradle
│
├── gradle-plugin/                 ← Gradle 插件
│   ├── PyBridgePlugin.groovy      ← 插件核心
│   ├── config/PythonExtension.groovy
│   └── tasks/                     ← 5 个构建任务
│
├── python/pybridge_java/          ← Python→Java 访问模块
│
├── build-system/
│   └── build_android.sh           ← NDK 交叉编译脚本
│
├── demo/                          ← Android 示例应用
│
├── test/TestJava.java             ← Java 端到端测试
│
└── doc/
    ├── BLUEPRINT.md               ← 完整复现蓝图 (给 AI 用)
    ├── COMPLETE_DOCUMENTATION.md  ← 完整技术文档
    └── DESIGN.md                  ← 架构设计
```

---

## 架构

```
Java/Kotlin  →  JNI (pybridge_jni.c)  →  C 桥接层 (pybridge_core.c)
                                              │
                                              ├─ dlopen(libpython.so) → Python
                                              ├─ dlopen(libcmath.so)  → C 模块
                                              ├─ dlopen(librustmath.so) → Rust 模块
                                              └─ dlopen(libgomath.so)  → Go 模块
```

所有非 JVM 语言编译为 .so 共享库，通过 dlopen 动态加载。模块只需导出 `pb_module_info()` 和 `pb_module_func()` 两个函数即可接入。

---

## API 速查

### C 层

```c
pb_init(NULL);                          // 初始化 Python
PyObject* m = pb_import("math");        // 导入模块
PyObject* r = pb_eval("2 ** 10");       // 求值表达式
pb_exec("x = 100");                     // 执行语句
long v = pb_to_long(r);                 // 转为 C long
double d = pb_to_double(r);             // 转为 C double
const char* s = pb_to_string(r);        // 转为 C string
pb_decref(r);                           // 释放引用
```

### Kotlin 层

```kotlin
Python.initialize(context)
val math = Python.module("math")
val pi = math["PI"].toDouble()           // 属性访问
val r = math.callAttr("sqrt", 16.0)     // 方法调用
val (x, y, z) = tuple                   // 解构
val sum = Python.exec("sum([1,2,3])").toInt()
```

### Java 层

```java
PyBridge.initialize(context);
PyObject math = PyBridge.module("math");
double sqrt = math.callAttr("sqrt", 144.0).toDouble();
String json = PyBridge.call("json.dumps", myMap).toStr();
```

### 多语言模块

```c
// 定义模块 (C)
#include "pb_module.h"
static double add(double a, double b) { return a + b; }
PB_MODULE("mymod", "1.0.0", "c", PB_FUNC_ENTRY(add, "dd>d"))

// 加载模块
PB_Module* mod = pb_load_module("mymod", "./libmymod.so");
typedef double (*fn_dd)(double, double);
fn_dd add_fn = (fn_dd)pb_get_func(mod, "add");
double r = add_fn(3.0, 4.0);  // 7.0
```

---

## 编译 Rust/Go 模块（可选）

```bash
# Rust（需要 rustc）
cd modules
rustc --crate-type cdylib -o librustmath.so rust/rustmath.rs
make test  # 会自动加载并测试

# Go（需要 go）
cd modules/go
go build -buildmode=c-shared -o ../libgomath.so gomath.go
cd .. && make test
```

---

## Android 构建（需要 NDK）

```bash
export ANDROID_NDK=/path/to/ndk
cd build-system
./build_android.sh --abi arm64-v8a --ndk $ANDROID_NDK
# 输出: output/arm64-v8a/lib/libpython3.12.so
#       output/arm64-v8a/lib/libpybridge.so
#       output/arm64-v8a/assets/python312_stdlib.zip
```

---

## 文档

| 文件 | 用途 |
|------|------|
| `doc/BLUEPRINT.md` | **给 AI 用**：完整源码 + 构建命令，可从零复现 |
| `doc/COMPLETE_DOCUMENTATION.md` | 完整技术文档（16 章，1000+ 行） |
| `doc/DESIGN.md` | 架构设计与技术决策 |
| `doc/MULTI_LANGUAGE.md` | 多语言模块系统设计 |

---

## 测试状态

| 层级 | 测试数 | 状态 |
|------|--------|------|
| C 核心层 | 13 | ✅ 全部通过 |
| JNI 层 | 6 | ✅ 全部通过 |
| 多语言模块 | 2 | ✅ 全部通过 |
| DLC 跨语言桥接 | 5 | ✅ 全部通过 |

---

## DLC 多语言连接器

`dlc/` 目录提供统一的跨语言桥接协议，支持 9 种语言互操作：

| 语言 | 连接器 | 实现方式 |
|------|--------|---------|
| Python | `pybridge_core.c` | dlopen libpython |
| C | `pb_module.h` | 原生编译 |
| C++ | `dlc/cpp/dlc_cpp.cpp` | g++ JIT 编译 |
| Java | `pybridge_jni.c` | JNI 桥接 |
| JavaScript | `dlc/javascript/dlc_js.c` | QuickJS/Node.js |
| C# | `dlc/csharp/dlc_cs.c` | .NET 管道 |
| SQL | `dlc/sql/dlc_sql.c` | SQLite 嵌入 |
| TypeScript | `dlc/typescript/dlc_ts.c` | TS→JS 转译 |
| Rust | `modules/rust/` | C ABI 导出 |

```c
// 跨语言调用示例
DLC_Value* result = dlc_bridge_eval("sql",
    "SELECT name, age FROM users WHERE age > 25");
DLC_Value* answer = dlc_bridge_call("javascript", "Math.sqrt", args, 1);
```

详见 [dlc/README.md](dlc/README.md)

---

## Android 集成

PyBridge 可以像 Chaquopy 一样在 Android 中运行 Python。详见 [Android 快速上手](doc/ANDROID_SETUP.md)

---

## License

Apache License 2.0
