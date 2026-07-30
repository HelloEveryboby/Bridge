"""
PyBridge Demo — Python 工具模块（Java/Kotlin 共用）

PyBridge 是 Bridge 框架的 Python 桥接子模块；本模块提供演示用的 Python 工具函数。
"""


def greet(name: str) -> str:
    """打招呼"""
    return f"🐍 Hello from Python, {name}!"


def get_coords() -> tuple:
    """返回坐标（演示解构）"""
    return (10, 20, 30)


def fibonacci(n: int) -> list:
    """斐波那契数列"""
    if n <= 0:
        return []
    fib = [0, 1]
    for i in range(2, n):
        fib.append(fib[-1] + fib[-2])
    return fib[:n]


def factorial(n: int) -> int:
    """阶乘"""
    return 1 if n <= 1 else n * factorial(n - 1)


def is_prime(n: int) -> bool:
    """判断素数"""
    if n < 2:
        return False
    for i in range(2, int(n**0.5) + 1):
        if n % i == 0:
            return False
    return True


def matrix_multiply(a: list, b: list) -> list:
    """矩阵乘法"""
    import numpy as np
    return np.dot(np.array(a), np.array(b)).tolist()


def text_stats(text: str) -> dict:
    """文本统计"""
    words = text.split()
    return {
        "characters": len(text),
        "words": len(words),
        "lines": text.count("\n") + 1,
        "avg_word_length": sum(len(w) for w in words) / max(len(words), 1),
    }


def process_data(data: list, multiplier: float = 1.0) -> dict:
    """数据处理（演示复杂返回值）"""
    import numpy as np
    arr = np.array(data) * multiplier
    return {
        "original": data,
        "processed": arr.tolist(),
        "mean": float(arr.mean()),
        "std": float(arr.std()),
        "min": float(arr.min()),
        "max": float(arr.max()),
    }
