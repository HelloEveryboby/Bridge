package com.pybridge.exceptions

/**
 * Python 运行时异常
 *
 * Kotlin 用法：
 * ```kotlin
 * try {
 *     Python.exec("1/0")
 * } catch (e: PyException) {
 *     println(e.pythonType)   // "ZeroDivisionError"
 *     println(e.traceback)    // 完整 Python 堆栈
 * }
 * ```
 */
open class PyException @JvmOverloads constructor(
    message: String,
    val pythonType: String? = null,
    val pythonTraceback: String? = null,
    cause: Throwable? = null
) : RuntimeException(message, cause) {

    override fun toString(): String = buildString {
        if (pythonType != null) append("Python $pythonType: ")
        append(message)
        if (pythonTraceback != null) {
            append("\n\nPython traceback:\n")
            append(pythonTraceback)
        }
    }
}

/**
 * 模块未找到异常
 */
class ModuleNotFoundException(val moduleName: String) :
    PyException("No module named '$moduleName'")

/**
 * 类型转换异常
 */
class TypeConversionException(
    val sourceType: String,
    val targetType: String,
    message: String = "Cannot convert $sourceType to $targetType"
) : PyException(message)

/**
 * 函数不存在或不可调用
 */
class FunctionNotFoundException(val funcName: String, val moduleName: String? = null) :
    PyException(
        if (moduleName != null) "Function '$funcName' not found in module '$moduleName'"
        else "Function '$funcName' not found or not callable"
    )
