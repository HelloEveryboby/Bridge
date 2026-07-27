"""
Java 类访问核心模块

提供 jclass 和 jarray 用于在 Python 中访问 Java 类。
"""

from .java_proxy import JavaProxy, JavaObject, JavaMethod, JavaField


def jclass(class_name: str) -> type:
    """
    获取 Java 类的 Python 代理

    Args:
        class_name: 全限定类名，如 "java.lang.String" 或 "java.util.ArrayList"

    Returns:
        可调用的 Python 类对象

    Example:
        >>> String = jclass("java.lang.String")
        >>> s = String("Hello")
        >>> s.length()
        5
    """
    # 通过 JNI 获取 Java 类并创建代理
    return JavaProxy.get_class(class_name)


def jarray(element_type):
    """
    创建 Java 数组类型

    Args:
        element_type: 元素类型（可以是 jclass 或基本类型名）

    Returns:
        可调用的数组构造器

    Example:
        >>> IntArray = jarray("int")
        >>> arr = IntArray([1, 2, 3])
        >>> arr[0]
        1
    """
    if isinstance(element_type, str):
        # 基本类型
        type_map = {
            "boolean": "Z", "byte": "B", "short": "S", "int": "I",
            "long": "J", "float": "F", "double": "D", "char": "C"
        }
        if element_type in type_map:
            return JavaProxy.get_array_class(type_map[element_type])
        else:
            # 对象数组
            return JavaProxy.get_array_class("L" + element_type.replace(".", "/") + ";")
    else:
        # jclass 对象
        class_name = element_type.__name__ if hasattr(element_type, '__name__') else str(element_type)
        return JavaProxy.get_array_class("L" + class_name.replace(".", "/") + ";")


def cast(java_class, obj):
    """
    将 Java 对象强制转换为指定类型

    Args:
        java_class: 目标 Java 类（jclass 返回值）
        obj: 要转换的 Java 对象

    Returns:
        转换后的 Java 对象

    Example:
        >>> CharSequence = jclass("java.lang.CharSequence")
        >>> s = cast(CharSequence, some_string)
    """
    if obj is None:
        return None
    if isinstance(obj, JavaObject):
        return obj._cast(java_class)
    return obj


class JavaModule:
    """
    java 模块入口

    提供便捷的 Java 类访问：
        from pybridge_java import java
        System = java.System
        ArrayList = java.ArrayList
    """

    def __getattr__(self, name: str):
        # 尝试从 java.lang 获取
        try:
            return jclass(f"java.lang.{name}")
        except Exception:
            raise AttributeError(f"Java class not found: {name}")

    def jclass(self, name: str) -> type:
        return jclass(name)

    def jarray(self, element_type):
        return jarray(element_type)

    def cast(self, java_class, obj):
        return cast(java_class, obj)


# 全局 java 模块实例
java = JavaModule()
