"""
Java 对象代理系统

在 Python 中表示 Java 对象，支持属性访问、方法调用、运算符重载。
"""

import ctypes
from typing import Any, Optional


class JavaObject:
    """
    Java 对象在 Python 中的代理

    每个 JavaObject 包装一个 JNI 引用，支持：
    - 属性访问 (obj.field)
    - 方法调用 (obj.method())
    - 运算符重载 (obj == other, hash(obj))
    - 类型检查 (isinstance)
    """

    def __init__(self, jni_ref: int, class_name: str = "java.lang.Object"):
        self._jni_ref = jni_ref
        self._class_name = class_name
        self._methods = {}  # 方法缓存
        self._fields = {}   # 字段缓存

    def __del__(self):
        """释放 JNI 引用"""
        if self._jni_ref:
            try:
                _decref_java_object(self._jni_ref)
            except Exception:
                pass

    def __getattr__(self, name: str) -> Any:
        """访问 Java 属性或方法"""
        if name.startswith('_'):
            return super().__getattribute__(name)

        # 尝试获取字段
        try:
            field_value = _get_java_field(self._jni_ref, name)
            return auto_convert(field_value)
        except Exception:
            pass

        # 尝试获取方法（返回可调用对象）
        try:
            method = JavaMethod(self._jni_ref, name)
            # 缓存方法
            self._methods[name] = method
            return method
        except Exception:
            raise AttributeError(f"'{self._class_name}' has no attribute '{name}'")

    def __setattr__(self, name: str, value: Any):
        """设置 Java 属性"""
        if name.startswith('_'):
            super().__setattr__(name, value)
        else:
            _set_java_field(self._jni_ref, name, value)

    def __str__(self) -> str:
        """调用 Java toString()"""
        try:
            return _call_java_method(self._jni_ref, "toString", [], {})
        except Exception:
            return f"<Java {self._class_name} at {hex(self._jni_ref)}>"

    def __repr__(self) -> str:
        return self.__str__()

    def __eq__(self, other) -> bool:
        """调用 Java equals()"""
        if isinstance(other, JavaObject):
            return _call_java_method(self._jni_ref, "equals", [other._jni_ref], {})
        return False

    def __hash__(self) -> int:
        """调用 Java hashCode()"""
        try:
            return _call_java_method(self._jni_ref, "hashCode", [], {})
        except Exception:
            return hash(self._jni_ref)

    def __bool__(self) -> bool:
        """非 null 对象为 True"""
        return self._jni_ref != 0

    def __len__(self) -> int:
        """支持 len()（如果 Java 对象有 size() 方法）"""
        try:
            return _call_java_method(self._jni_ref, "size", [], {})
        except Exception:
            raise TypeError(f"'{self._class_name}' has no len()")

    def __getitem__(self, key) -> Any:
        """支持 [] 访问（如果 Java 对象有 get() 方法）"""
        try:
            return auto_convert(_call_java_method(self._jni_ref, "get", [key], {}))
        except Exception:
            raise TypeError(f"'{self._class_name}' is not subscriptable")

    def __setitem__(self, key, value):
        """支持 [] 设置（如果 Java 对象有 put() 方法）"""
        try:
            _call_java_method(self._jni_ref, "put", [key, value], {})
        except Exception:
            raise TypeError(f"'{self._class_name}' does not support item assignment")

    def __contains__(self, item) -> bool:
        """支持 in 操作符（如果 Java 对象有 contains() 方法）"""
        try:
            return _call_java_method(self._jni_ref, "contains", [item], {})
        except Exception:
            return False

    def __iter__(self):
        """支持迭代（如果 Java 对象有 iterator() 方法）"""
        try:
            iterator = _call_java_method(self._jni_ref, "iterator", [], {})
            return JavaIterator(iterator)
        except Exception:
            raise TypeError(f"'{self._class_name}' is not iterable")

    def _cast(self, target_class):
        """类型转换"""
        return JavaObject(self._jni_ref, target_class.__name__)


class JavaMethod:
    """
    Java 方法代理

    支持方法重载解析：
    - 根据参数数量和类型选择正确的重载版本
    - 自动处理基本类型的装箱/拆箱
    """

    def __init__(self, obj_ref: int, method_name: str):
        self._obj_ref = obj_ref
        self._method_name = method_name

    def __call__(self, *args, **kwargs) -> Any:
        """调用 Java 方法"""
        # 转换参数
        converted_args = [python_to_java(arg) for arg in args]
        result = _call_java_method(
            self._obj_ref, self._method_name,
            converted_args, kwargs
        )
        return auto_convert(result)


class JavaField:
    """Java 字段代理"""

    def __init__(self, obj_ref: int, field_name: str):
        self._obj_ref = obj_ref
        self._field_name = field_name

    def get(self) -> Any:
        return auto_convert(_get_java_field(self._obj_ref, self._field_name))

    def set(self, value):
        _set_java_field(self._obj_ref, self._field_name, value)


class JavaIterator:
    """Java Iterator 的 Python 适配器"""

    def __init__(self, java_iterator):
        self._iterator = java_iterator

    def __iter__(self):
        return self

    def __next__(self):
        if not _call_java_method(self._iterator._jni_ref, "hasNext", [], {}):
            raise StopIteration
        item = _call_java_method(self._iterator._jni_ref, "next", [], {})
        return auto_convert(item)


class JavaProxy:
    """
    Java 类代理管理器

    管理 Java 类的 Python 代理，支持：
    - 类缓存
    - 构造器调用
    - 静态方法/字段访问
    """

    _class_cache = {}

    @classmethod
    def get_class(cls, class_name: str) -> type:
        """获取 Java 类的 Python 代理类"""
        if class_name in cls._class_cache:
            return cls._class_cache[class_name]

        # 创建代理类
        proxy_class = type(class_name, (JavaObject,), {
            '__init__': lambda self, *args: _init_java_object(self, class_name, args),
            '__name__': class_name.split('.')[-1],
            '__qualname__': class_name,
        })

        cls._class_cache[class_name] = proxy_class
        return proxy_class

    @classmethod
    def get_array_class(cls, type_signature: str):
        """获取 Java 数组的 Python 代理类"""
        cache_key = f"array:{type_signature}"
        if cache_key in cls._class_cache:
            return cls._class_cache[cache_key]

        def array_init(self, python_list=None):
            if python_list:
                self._jni_ref = _create_java_array(type_signature, python_list)
            else:
                self._jni_ref = _create_java_array(type_signature, [])

        array_class = type(f"Array[{type_signature}]", (JavaObject,), {
            '__init__': array_init,
            '__getitem__': lambda self, idx: auto_convert(
                _get_array_element(self._jni_ref, idx)
            ),
            '__setitem__': lambda self, idx, val: _set_array_element(
                self._jni_ref, idx, val
            ),
            '__len__': lambda self: _get_array_length(self._jni_ref),
        })

        cls._class_cache[cache_key] = array_class
        return array_class


# JNI 底层调用接口（通过 ctypes 调用 libpybridge.so）

def _init_java_object(py_obj, class_name, args):
    """初始化 Java 对象"""
    # 通过 JNI 创建 Java 对象
    ref = _jni_create_object(class_name, args)
    py_obj._jni_ref = ref
    py_obj._class_name = class_name


def _jni_create_object(class_name, args):
    """JNI 调用：创建 Java 对象"""
    # 实际实现通过 C 扩展模块
    return 0  # placeholder


def _call_java_method(obj_ref, method_name, args, kwargs):
    """JNI 调用：调用 Java 方法"""
    return None  # placeholder


def _get_java_field(obj_ref, field_name):
    """JNI 调用：获取 Java 字段"""
    return None  # placeholder


def _set_java_field(obj_ref, field_name, value):
    """JNI 调用：设置 Java 字段"""
    pass  # placeholder


def _decref_java_object(obj_ref):
    """JNI 调用：释放 Java 对象引用"""
    pass  # placeholder


def _create_java_array(type_sig, elements):
    """JNI 调用：创建 Java 数组"""
    return 0  # placeholder


def _get_array_element(arr_ref, index):
    """JNI 调用：获取数组元素"""
    return None  # placeholder


def _set_array_element(arr_ref, index, value):
    """JNI 调用：设置数组元素"""
    pass  # placeholder


def _get_array_length(arr_ref):
    """JNI 调用：获取数组长度"""
    return 0  # placeholder


# 类型转换辅助（从 type_mapping 导入）
from .type_mapping import auto_convert, python_to_java
