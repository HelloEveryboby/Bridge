# Bridge Runtime — Java/Kotlin 运行时

Android 端的 Java/Kotlin API（包名 `com.bridge.py.*`），提供 Python 解释器初始化、
模块加载、对象操作和类型转换。PyBridge 是 Bridge 框架的 Python 桥接子模块。

## 快速开始

```kotlin
// 初始化 Python 解释器
Python.initialize(
    context = applicationContext,
    pythonHome = "/data/data/${packageName}/files/python",
    modulePaths = listOf("/data/data/${packageName}/files/python")
)

// 加载模块
val mymod = Python.module("mymod")

// 调用函数
val result = mymod.callAttr("my_function", "arg1", 42)

// 自动类型转换
val str: String = result.toString()
val num: Int = result.toInt()

// 使用 use 自动释放
mymod.use { mod ->
    mod.callAttr("process", data)
}

// 关闭解释器
Python.shutdown()
```

## Java 调用

```java
Python python = new Python.Builder()
    .context(context)
    .pythonHome("/data/data/.../files/python")
    .build();

PyObject mod = python.module("mymod");
PyObject result = mod.callAttr("my_function", "arg1", 42);
result.close();
mod.close();
python.close();
```

## 模块结构

| 文件 | 说明 |
|------|------|
| `Python.kt` | 入口：`initialize()`、`module()`、`exec()`、`shutdown()` |
| `PyObject.kt` | Python 对象包装：运算符重载、类型转换、Closeable |
| `PyBridgeProxyHandler.kt` | Java 动态代理 → Python 回调 |
| `TypeConverter.kt` | Kotlin ↔ Python 类型自动转换 |
| `PyExceptions.kt` | 异常类型定义 |
| `PyBridge.java` | 纯 Java API：Builder 模式封装 |