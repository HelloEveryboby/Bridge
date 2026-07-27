"""
类型自动转换系统

Python ↔ Java 双向类型映射。
"""

from typing import Any, Optional


def auto_convert(value: Any) -> Any:
    """
    自动转换 Java/Python 类型

    根据值的类型选择最合适的转换方式。
    """
    if value is None:
        return None

    # 如果已经是 Python 原生类型，直接返回
    if isinstance(value, (bool, int, float, str, bytes)):
        return value

    # Java 对象包装
    from .java_proxy import JavaObject
    if isinstance(value, JavaObject):
        return value

    # 尝试检测 Java 类型并转换
    type_name = getattr(value, '__class__', type(value)).__name__

    # 基本类型映射
    if type_name in ('Boolean', 'jboolean'):
        return bool(value)
    if type_name in ('Integer', 'Long', 'Short', 'Byte', 'jint', 'jlong', 'jshort', 'jbyte'):
        return int(value)
    if type_name in ('Float', 'Double', 'jfloat', 'jdouble'):
        return float(value)
    if type_name in ('String', 'jchar'):
        return str(value)

    return value


def python_to_java(value: Any) -> Any:
    """
    Python 对象 → Java 对象

    用于方法调用时的参数转换。
    """
    if value is None:
        return None

    # 基本类型直接传递（JNI 层会处理）
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return value
    if isinstance(value, str):
        return value
    if isinstance(value, bytes):
        return value

    # 集合类型
    if isinstance(value, (list, tuple)):
        return list(value)
    if isinstance(value, dict):
        return dict(value)
    if isinstance(value, set):
        return list(value)

    # Java 类型包装器
    from .java_types import (
        jboolean, jbyte, jshort, jint, jlong,
        jfloat, jdouble, jchar
    )
    if isinstance(value, (jboolean, jbyte, jshort, jint, jlong, jfloat, jdouble, jchar)):
        return value.value

    # Java 对象
    from .java_proxy import JavaObject
    if isinstance(value, JavaObject):
        return value

    return value


def java_to_python(value: Any, target_type: Optional[type] = None) -> Any:
    """
    Java 对象 → Python 对象

    用于返回值转换。
    """
    if value is None:
        return None

    if target_type is None:
        return auto_convert(value)

    # 指定目标类型
    if target_type == bool:
        return bool(value)
    if target_type == int:
        return int(value)
    if target_type == float:
        return float(value)
    if target_type == str:
        return str(value)
    if target_type == bytes:
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        return str(value).encode('utf-8')
    if target_type == list:
        if isinstance(value, (list, tuple)):
            return list(value)
        return [value]
    if target_type == dict:
        if isinstance(value, dict):
            return dict(value)
        return {}

    return value
