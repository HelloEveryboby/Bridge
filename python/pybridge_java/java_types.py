"""
Java 基本类型包装器

用于精确控制 Java 方法重载选择：
    p.print(42)         # 调用 print(long)
    p.print(jint(42))   # 调用 print(int)
    p.print(42.0)       # 调用 print(double)
    p.print(jfloat(42.0))  # 调用 print(float)
"""


class jboolean:
    """Java boolean 类型"""
    def __init__(self, value: bool):
        self.value = bool(value)
    def __repr__(self): return f"jboolean({self.value})"


class jbyte:
    """Java byte 类型 (-128 ~ 127)"""
    def __init__(self, value: int, truncate: bool = False):
        if truncate:
            value = value & 0xFF
        elif not (-128 <= value <= 127):
            raise OverflowError(f"Value {value} out of byte range")
        self.value = value
    def __repr__(self): return f"jbyte({self.value})"


class jshort:
    """Java short 类型 (-32768 ~ 32767)"""
    def __init__(self, value: int, truncate: bool = False):
        if truncate:
            value = value & 0xFFFF
        elif not (-32768 <= value <= 32767):
            raise OverflowError(f"Value {value} out of short range")
        self.value = value
    def __repr__(self): return f"jshort({self.value})"


class jint:
    """Java int 类型 (-2^31 ~ 2^31-1)"""
    def __init__(self, value: int, truncate: bool = False):
        if truncate:
            value = value & 0xFFFFFFFF
        elif not (-2**31 <= value <= 2**31 - 1):
            raise OverflowError(f"Value {value} out of int range")
        self.value = value
    def __repr__(self): return f"jint({self.value})"


class jlong:
    """Java long 类型 (-2^63 ~ 2^63-1)"""
    def __init__(self, value: int, truncate: bool = False):
        if truncate:
            value = value & 0xFFFFFFFFFFFFFFFF
        elif not (-2**63 <= value <= 2**63 - 1):
            raise OverflowError(f"Value {value} out of long range")
        self.value = value
    def __repr__(self): return f"jlong({self.value})"


class jfloat:
    """Java float 类型 (32位浮点)"""
    def __init__(self, value: float, truncate: bool = False):
        self.value = float(value)
    def __repr__(self): return f"jfloat({self.value})"


class jdouble:
    """Java double 类型 (64位浮点)"""
    def __init__(self, value: float):
        self.value = float(value)
    def __repr__(self): return f"jdouble({self.value})"


class jchar:
    """Java char 类型"""
    def __init__(self, value: str):
        if len(value) != 1:
            raise ValueError("jchar must be a single character")
        self.value = value
    def __repr__(self): return f"jchar({self.value!r})"


class jvoid:
    """
    Java void 类型

    不能实例化，仅用于 static proxy 的返回类型声明。
    """
    def __init__(self):
        raise TypeError("jvoid cannot be instantiated")

    @staticmethod
    def __repr__():
        return "jvoid"
