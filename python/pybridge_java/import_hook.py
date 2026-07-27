"""
Python Import Hook — 允许 import java.lang.String 形式

安装此 hook 后，可以使用标准 Python import 语法访问 Java 类：

    import java.lang.String
    s = java.lang.String("Hello")

    from java.util import ArrayList
    list = ArrayList()
    list.add("item")
"""

import sys
import importlib
from importlib.abc import MetaPathFinder, Loader
from importlib.machinery import ModuleSpec
from types import ModuleType
from typing import Optional, Sequence


class JavaModuleFinder(MetaPathFinder):
    """
    Python import hook，拦截 Java 包导入

    当 import 的模块名以 'java' 开头时，
    将其路由到 Java 类代理系统。
    """

    JAVA_PACKAGES = {'java', 'javax', 'android', 'androidx'}

    def find_module(self, fullname: str, path: Optional[str] = None) -> Optional['JavaModuleLoader']:
        # 检查是否是 Java 包
        parts = fullname.split('.')
        if parts[0] in self.JAVA_PACKAGES:
            return JavaModuleLoader(fullname)
        return None

    def find_spec(self, fullname: str, path: Optional[Sequence[str]],
                  target: Optional[ModuleType] = None) -> Optional[ModuleSpec]:
        parts = fullname.split('.')
        if parts[0] in self.JAVA_PACKAGES:
            return ModuleSpec(fullname, JavaModuleLoader(fullname))
        return None


class JavaModuleLoader(Loader):
    """
    Java 模块加载器

    将 Java 包/类映射为 Python 模块/对象。
    """

    def __init__(self, fullname: str):
        self.fullname = fullname

    def create_module(self, spec: ModuleSpec) -> Optional[ModuleType]:
        return None  # 使用默认模块

    def exec_module(self, module: ModuleType) -> None:
        from .java_class import jclass

        parts = self.fullname.split('.')

        # 尝试作为类加载
        class_name = '.'.join(parts)
        try:
            cls = jclass(class_name)
            # 设置模块的类引用
            module.__dict__[parts[-1]] = cls
            module.__class__ = JavaClassModule
            module._java_class = cls
            return
        except Exception:
            pass

        # 作为包处理（创建子模块代理）
        module.__path__ = []
        module.__package__ = self.fullname

        # 设置 __getattr__ 以延迟加载子类
        def module_getattr(name: str):
            full_class = f"{self.fullname}.{name}"
            try:
                return jclass(full_class)
            except Exception:
                raise AttributeError(f"module '{self.fullname}' has no attribute '{name}'")

        module.__getattr__ = module_getattr


class JavaClassModule(ModuleType):
    """特殊模块类型，支持直接作为类使用"""
    _java_class = None

    def __call__(self, *args, **kwargs):
        if self._java_class:
            return self._java_class(*args, **kwargs)
        raise TypeError(f"module '{self.__name__}' is not callable")


# 安装/卸载 hook
_finder_installed = False


def install():
    """安装 Java import hook"""
    global _finder_installed
    if not _finder_installed:
        # 确保不重复安装
        for finder in sys.meta_path:
            if isinstance(finder, JavaModuleFinder):
                return

        sys.meta_path.insert(0, JavaModuleFinder())
        _finder_installed = True


def uninstall():
    """卸载 Java import hook"""
    global _finder_installed
    sys.meta_path[:] = [f for f in sys.meta_path if not isinstance(f, JavaModuleFinder)]
    _finder_installed = False
