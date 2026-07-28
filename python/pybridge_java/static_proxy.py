"""
PyBridge Static Proxy — @PyClass 装饰器

允许 Python 类实现 Java 接口，使 Java 可以回调 Python 方法。

使用方式：
    from pybridge_java import jclass, PyClass

    # 实现 Java 接口
    @PyClass("java.lang.Runnable")
    class MyRunnable:
        def run(self):
            print("Running in Python!")

    # 也可以实现多个接口
    @PyClass("android.view.View.OnClickListener")
    class MyClickListener:
        def onClick(self, view):
            print(f"Clicked: {view}")

    # 获取 Java 代理对象
    runnable = MyRunnable()  # 返回 Java 代理对象
    Thread = jclass("java.lang.Thread")
    t = Thread(runnable)
    t.start()

    # 无接口回调（约定方法）
    @PyClass()
    class MyCallback:
        def onResult(self, data):
            print(f"Result: {data}")

工作原理：
    1. Python 类被 @PyClass 装饰后，实例化时创建 Java 动态代理
    2. Java 调用代理方法时，通过 JNI 回调 Python 方法
    3. 返回值自动转换 Java ↔ Python
"""

import ctypes
import inspect
from typing import Any, Callable, Dict, List, Optional


class PyClassProxy:
    """
    PyClass 代理对象

    包装 Python 实例，提供 Java 动态代理所需的 InvocationHandler。
    实际与 Java 的交互通过 JNI bridge 完成。
    """

    _proxy_registry: Dict[int, 'PyClassProxy'] = {}
    _next_id: int = 1

    def __init__(self, python_instance: Any, interface_names: List[str]):
        self._python_instance = python_instance
        self._interface_names = interface_names
        self._proxy_id = PyClassProxy._next_id
        PyClassProxy._next_id += 1
        PyClassProxy._proxy_registry[self._proxy_id] = self

        # 创建 Java 动态代理
        self._java_proxy_ref = _create_java_proxy(
            self._proxy_id,
            interface_names,
            list(self._get_method_map().keys())
        )

    def _get_method_map(self) -> Dict[str, Callable]:
        """获取所有可调用的公共方法"""
        methods = {}
        for name, method in inspect.getmembers(self._python_instance, inspect.ismethod):
            if not name.startswith('_'):
                methods[name] = method
        return methods

    def invoke(self, method_name: str, args: List[Any]) -> Any:
        """
        Java 调用转发到 Python 方法

        此方法由 JNI 层调用（当 Java 动态代理方法被触发时）。
        """
        method = getattr(self._python_instance, method_name, None)
        if method is None:
            raise AttributeError(
                f"'{type(self._python_instance).__name__}' has no method '{method_name}'"
            )
        if not callable(method):
            raise TypeError(f"'{method_name}' is not callable")
        return method(*args)

    def get_java_proxy(self):
        """获取 Java 代理对象"""
        return _wrap_java_proxy(self._proxy_id)

    def __del__(self):
        """释放代理"""
        if hasattr(self, '_proxy_id'):
            PyClassProxy._proxy_registry.pop(self._proxy_id, None)
            _release_java_proxy(self._proxy_id)

    @classmethod
    def get_proxy(cls, proxy_id: int) -> Optional['PyClassProxy']:
        """通过 ID 查找代理（JNI 回调使用）"""
        return cls._proxy_registry.get(proxy_id)


def PyClass(*interface_names: str):
    """
    装饰器：标记 Python 类为 Java 接口实现

    Args:
        *interface_names: Java 接口全限定名，如 "java.lang.Runnable"
                          不传则表示无接口回调

    Returns:
        装饰后的类，实例化时创建 Java 代理对象

    Example:
        @PyClass("java.lang.Runnable")
        class MyRunnable:
            def run(self):
                print("Hello from Python!")

        runnable = MyRunnable()  # 返回 Java 代理
        Thread = jclass("java.lang.Thread")
        Thread(runnable).start()
    """
    interface_list = list(interface_names)

    def decorator(cls):
        original_init = getattr(cls, '__init__', None)

        def proxy_init(self, *args, **kwargs):
            if original_init:
                original_init(self, *args, **kwargs)
            self.__pybridge_proxy__ = PyClassProxy(self, interface_list)

        def proxy_new(cls, *args, **kwargs):
            return object.__new__(cls)

        cls.__new__ = staticmethod(proxy_new)
        cls.__init__ = proxy_init
        cls.__pyclass__ = True
        cls.__pyclass_interfaces__ = interface_list
        return cls

    return decorator


# ═══════════════════════════════════════════════════════════════════
# JNI Bridge — 通过 ctypes 调用 libpybridge.so
# ═══════════════════════════════════════════════════════════════════

def _load_lib():
    """加载 libpybridge.so"""
    import os

    try:
        import android  # noqa: F401
        return ctypes.CDLL("libpybridge.so")
    except (ImportError, OSError):
        pass

    for path in ["libpybridge.so", os.path.join(os.path.dirname(__file__), "libpybridge.so")]:
        try:
            return ctypes.CDLL(path)
        except OSError:
            continue
    return None


def _configure_lib(lib):
    """配置 ctypes 函数签名"""
    if lib is None:
        return

    lib.pybridge_jni_create_proxy.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p),
        ctypes.c_int, ctypes.POINTER(ctypes.c_char_p),
    ]
    lib.pybridge_jni_create_proxy.restype = ctypes.c_int

    lib.pybridge_jni_release_proxy.argtypes = [ctypes.c_int]
    lib.pybridge_jni_release_proxy.restype = None

    lib.pybridge_jni_proxy_invoke.argtypes = [
        ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.py_object),
    ]
    lib.pybridge_jni_proxy_invoke.restype = ctypes.py_object


_lib = _load_lib()
if _lib is not None:
    try:
        _configure_lib(_lib)
    except Exception:
        pass


def _create_java_proxy(proxy_id: int, interface_names: List[str], method_names: List[str]) -> int:
    """通过 JNI 创建 Java 动态代理"""
    if _lib is None:
        return -1

    iface_count = len(interface_names)
    method_count = len(method_names)

    iface_arr = (ctypes.c_char_p * iface_count)()
    for i, name in enumerate(interface_names):
        iface_arr[i] = name.encode('utf-8')

    method_arr = (ctypes.c_char_p * method_count)()
    for i, name in enumerate(method_names):
        method_arr[i] = name.encode('utf-8')

    return _lib.pybridge_jni_create_proxy(
        proxy_id, iface_count, iface_arr, method_count, method_arr
    )


def _release_java_proxy(proxy_id: int):
    """释放 Java 代理"""
    if _lib is None:
        return
    _lib.pybridge_jni_release_proxy(proxy_id)


def _wrap_java_proxy(proxy_id: int):
    """包装 Java 代理为 Python 对象"""
    from .java_proxy import JavaObject
    return JavaObject(proxy_id, "java.lang.reflect.Proxy")


# ═══════════════════════════════════════════════════════════════════
# C 回调 — 由 JNI 层调用
# ═══════════════════════════════════════════════════════════════════

_invoke_callback_type = ctypes.CFUNCTYPE(
    ctypes.py_object, ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.py_object
)


def _proxy_invoke_callback(proxy_id: int, method_name: bytes, arg_count: int, args: list) -> Any:
    """JNI 回调：代理方法调用"""
    proxy = PyClassProxy.get_proxy(proxy_id)
    if proxy is None:
        raise RuntimeError(f"Proxy not found: {proxy_id}")
    method = method_name.decode('utf-8') if isinstance(method_name, bytes) else method_name
    return proxy.invoke(method, args if args else [])


# 注册回调到 C 层
try:
    if _lib is not None:
        _callback = _invoke_callback_type(_proxy_invoke_callback)
        _lib.pybridge_jni_set_proxy_callback(_callback)
except Exception:
    pass