"""
pybridge_java — Python 端 Java 访问模块

允许 Python 代码直接调用 Java 类和方法。

使用方式：
    from pybridge_java import java

    # 访问 Java 类
    String = java.jclass("java.lang.String")
    s = String("Hello from Python")
    print(s.length())  # 16

    # 访问 Android API
    Log = java.jclass("android.util.Log")
    Log.d("MyTag", "Hello from Python!")

    # 数组操作
    Arrays = java.jclass("java.util.Arrays")
    arr = java.jarray("int")([1, 2, 3, 4, 5])
    print(Arrays.toString(arr))

类型映射：
    Java null      → Python None
    Java boolean   → Python bool
    Java int/long  → Python int
    Java float/double → Python float
    Java String    → Python str
    Java byte[]    → Python bytes
    Java Object[]  → Python tuple
    Java List      → Python list (代理)
    Java Map       → Python dict (代理)
"""

from .java_class import jclass, jarray, cast
from .java_types import (
    jboolean, jbyte, jshort, jint, jlong,
    jfloat, jdouble, jchar, jvoid
)
from .java_proxy import JavaProxy, JavaMethod, JavaField
from .type_mapping import auto_convert, java_to_python, python_to_java

__all__ = [
    'java',
    'jclass', 'jarray', 'cast',
    'jboolean', 'jbyte', 'jshort', 'jint', 'jlong',
    'jfloat', 'jdouble', 'jchar', 'jvoid',
    'JavaProxy',
]

# 导入 hook：允许 import java.lang.String 形式的访问
from . import import_hook
import_hook.install()
