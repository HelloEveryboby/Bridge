package com.pybridge.core

import com.pybridge.exceptions.PyException
import com.pybridge.types.TypeConverter
import java.io.Closeable

/**
 * Python 对象的 Kotlin 包装器
 *
 * 提供惯用的 Kotlin API：
 * ```kotlin
 * val math = Python.module("math")
 *
 * // 属性访问（运算符重载）
 * val pi: Double = math["PI"].toDouble()
 *
 * // 方法调用
 * val result = math.callAttr("sqrt", 144.0)
 *
 * // 解构
 * val (x, y, z) = Python.module("mylib").callAttr("get_coords")
 *
 * // 转换
 * val list: List<*> = result.toList()
 * val map: Map<*, *> = result.toMap()
 * val str: String = result.toStr()
 *
 * // use 自动关闭
 * Python.module("heavy").use { mod ->
 *     mod.callAttr("process", data)
 * }
 * ```
 */
class PyObject internal constructor(
    private var pointer: Long,
    private val name: String = "<unknown>"
) : Closeable, AutoCloseable {

    private var closed = false

    companion object {
        /**
         * 从 Java 对象创建 Python 对象（内部）
         */
        internal fun fromJava(value: Any?): PyObject {
            if (value == null) return PyObject(0, "None")
            val ptr = TypeConverter.javaToPythonPtr(value)
            return PyObject(ptr, value.javaClass.simpleName)
        }

        /**
         * 从原始指针创建（内部）
         */
        internal fun fromPointer(ptr: Long, name: String = "<raw>"): PyObject {
            return PyObject(ptr, name)
        }
    }

    // ─── 属性访问 ─────────────────────────────────────────────────

    /**
     * 获取属性：obj["name"]
     *
     * ```kotlin
     * val pi = math["PI"].toDouble()
     * ```
     */
    operator fun get(attrName: String): PyObject {
        checkNotClosed()
        val result = Python.getAttrRaw(pointer, attrName)
            ?: throw PyException("Attribute not found: $attrName")
        return fromJava(result)
    }

    /**
     * 设置属性：obj["name"] = value
     */
    operator fun set(attrName: String, value: Any?) {
        checkNotClosed()
        Python.setAttrRaw(pointer, attrName, value)
    }

    // ─── 方法调用 ─────────────────────────────────────────────────

    /**
     * 调用属性方法（Kotlin 推荐）
     *
     * ```kotlin
     * val result = math.callAttr("sqrt", 16.0)
     * val text = module.callAttr("process", input, retries = 3)
     * ```
     */
    fun callAttr(method: String, vararg args: Any?): PyObject {
        checkNotClosed()
        val convertedArgs = args.map { unwrap(it) }.toTypedArray()
        val result = Python.callFunctionRaw(pointer, method, convertedArgs)
            ?: return PyObject(0, "None")  // void 返回
        return fromJava(result)
    }

    /**
     * 调用可调用对象本身
     *
     * ```kotlin
     * val func = module["my_func"]
     * val result = func(arg1, arg2)
     * ```
     */
    operator fun invoke(vararg args: Any?): PyObject {
        checkNotClosed()
        val convertedArgs = args.map { unwrap(it) }.toTypedArray()
        val result = Python.callFunctionRaw(pointer, "__call__", convertedArgs)
            ?: return PyObject(0, "None")
        return fromJava(result)
    }

    // ─── 类型转换 ─────────────────────────────────────────────────

    /**
     * 转为 String
     */
    fun toStr(): String {
        checkNotClosed()
        if (pointer == 0L) return "None"
        return TypeConverter.pythonToJava(this, String::class.java) as String
    }

    /**
     * 转为 Int
     */
    fun toInt(): Int {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, Int::class.java) as Int
    }

    /**
     * 转为 Long
     */
    fun toLong(): Long {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, Long::class.java) as Long
    }

    /**
     * 转为 Double
     */
    fun toDouble(): Double {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, Double::class.java) as Double
    }

    /**
     * 转为 Float
     */
    fun toFloat(): Float {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, Float::class.java) as Float
    }

    /**
     * 转为 Boolean
     */
    fun toBool(): Boolean {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, Boolean::class.java) as Boolean
    }

    /**
     * 转为 List
     */
    fun toList(): List<*> {
        checkNotClosed()
        @Suppress("UNCHECKED_CAST")
        return TypeConverter.pythonToJava(this, List::class.java) as List<*>
    }

    /**
     * 转为 Map
     */
    fun toMap(): Map<*, *> {
        checkNotClosed()
        @Suppress("UNCHECKED_CAST")
        return TypeConverter.pythonToJava(this, Map::class.java) as Map<*, *>
    }

    /**
     * 转为 ByteArray
     */
    fun toBytes(): ByteArray {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, ByteArray::class.java) as ByteArray
    }

    /**
     * 通用类型转换
     *
     * ```kotlin
     * val data = result.to<List<Map<String, Any>>>()
     * ```
     */
    @Suppress("UNCHECKED_CAST")
    fun <T> to(clazz: Class<T>): T {
        checkNotClosed()
        return TypeConverter.pythonToJava(this, clazz) as T
    }

    /**
     * 泛型内联版
     */
    inline fun <reified T> to(): T = to(T::class.java)

    // ─── 集合支持 ─────────────────────────────────────────────────

    /**
     * 长度（支持 list, dict, str, bytes 等）
     */
    val size: Int
        get() = callAttr("__len__").toInt()

    /**
     * 是否为空
     */
    val isEmpty: Boolean
        get() = size == 0

    /**
     * 迭代支持
     */
    operator fun iterator(): Iterator<PyObject> {
        val iter = callAttr("__iter__")
        return object : Iterator<PyObject> {
            override fun hasNext(): Boolean {
                return try {
                    iter.callAttr("__next__")
                    true
                } catch (_: PyException) {
                    false
                }
            }
            override fun next(): PyObject {
                return iter.callAttr("__next__")
            }
        }
    }

    /**
     * 索引访问：obj[0], obj["key"]
     */
    operator fun get(index: Int): PyObject = callAttr("__getitem__", index)
    operator fun get(key: String): PyObject = callAttr("__getitem__", key)

    /**
     * 索引设置
     */
    operator fun set(index: Int, value: Any?) = callAttr("__setitem__", index, value)
    operator fun set(key: String, value: Any?) = callAttr("__setitem__", key, value)

    // ─── 运算符重载 ───────────────────────────────────────────────

    operator fun plus(other: Any?): PyObject = callAttr("__add__", other)
    operator fun minus(other: Any?): PyObject = callAttr("__sub__", other)
    operator fun times(other: Any?): PyObject = callAttr("__mul__", other)
    operator fun div(other: Any?): PyObject = callAttr("__truediv__", other)
    operator fun rem(other: Any?): PyObject = callAttr("__mod__", other)
    operator fun unaryMinus(): PyObject = callAttr("__neg__")

    // ─── 比较 ─────────────────────────────────────────────────────

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is PyObject) return false
        if (pointer == other.pointer) return true
        return try {
            callAttr("__eq__", other).toBool()
        } catch (_: Exception) {
            false
        }
    }

    override fun hashCode(): Int = try {
        callAttr("__hash__").toInt()
    } catch (_: Exception) {
        pointer.hashCode()
    }

    // ─── 生命周期 ─────────────────────────────────────────────────

    val isNone: Boolean get() = pointer == 0L

    val typeName: String
        get() = if (closed) "closed"
                else try { this["__class__"]["__name__"].toStr() } catch (_: Exception) { name }

    override fun close() {
        if (!closed && pointer != 0L) {
            Python.decRefRaw(pointer)
            pointer = 0L
            closed = true
        }
    }

    protected fun finalize() {
        close()
    }

    override fun toString(): String {
        if (closed) return "<closed>"
        if (pointer == 0L) return "None"
        return try { toStr() } catch (_: Exception) { "<PyObject: $name>" }
    }

    // ─── 解构支持 ─────────────────────────────────────────────────

    /**
     * 支持 val (a, b, c) = tupleObj
     */
    operator fun component1(): PyObject = get(0)
    operator fun component2(): PyObject = get(1)
    operator fun component3(): PyObject = get(2)
    operator fun component4(): PyObject = get(3)
    operator fun component5(): PyObject = get(4)

    // ─── 内部工具 ─────────────────────────────────────────────────

    private fun checkNotClosed() {
        check(!closed) { "PyObject has been closed" }
    }

    private fun unwrap(value: Any?): Any? {
        if (value is PyObject) return value.toAutoJava()
        return value
    }

    /**
     * 自动推断最佳 Java 类型（用于传递给 JNI）
     */
    internal fun toAutoJava(): Any? {
        if (pointer == 0L) return null
        return TypeConverter.pythonToAutoJava(this)
    }

    /**
     * 获取原始指针（内部）
     */
    internal fun rawPointer(): Long = pointer
}
