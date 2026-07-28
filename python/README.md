# pybridge_java — Python 端 Java 访问模块

在 Android 应用中，允许 Python 代码直接调用 Java 类和方法。

## 快速开始

```python
from pybridge_java import jclass, jarray, PyClass, install_logcat

# 重定向 print 输出到 Logcat
install_logcat(tag="MyApp")

# 调用 Android API
Log = jclass("android.util.Log")
Log.d("MyTag", "Hello from Python!")

# 使用 Java 类
String = jclass("java.lang.String")
s = String("Hello from Python")
print(s.length())  # 16

# 实现 Java 接口回调
@PyClass("java.lang.Runnable")
class MyRunnable:
    def run(self):
        Log.i("MyTag", "Running in Python!")

Thread = jclass("java.lang.Thread")
Thread(MyRunnable()).start()
```

## 模块结构

| 文件 | 说明 |
|------|------|
| `__init__.py` | 模块入口，导出所有公共 API |
| `java_class.py` | `jclass()` 和 `jarray()` 核心实现 |
| `java_proxy.py` | JavaObject、JavaMethod、JavaField 代理 + ctypes JNI 调用 |
| `java_types.py` | Java 类型映射（jboolean、jint、jlong 等） |
| `type_mapping.py` | 自动类型转换 |
| `import_hook.py` | Python import hook：`import java.lang.String` |
| `static_proxy.py` | `@PyClass` 装饰器：Python 类实现 Java 接口 |
| `pybridge_logcat.py` | stdout/stderr → Logcat 重定向 |

## 类型映射

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