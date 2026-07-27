package com.pybridge.types

import com.pybridge.core.PyObject
import com.pybridge.exceptions.TypeConversionException

/**
 * Kotlin 类型转换器（内联函数 + reified 泛型）
 *
 * 提供 Kotlin 惯用的类型转换 API：
 * ```kotlin
 * val value = converter.to<String>(pyObj)
 * val list = converter.to<List<*>>(pyObj)
 * ```
 */
object TypeConverter {

    // ─── JNI native 方法 ──────────────────────────────────────────

    @JvmStatic private external fun nativeJavaToPython(value: Any?): Long

    // ─── Java → Python ────────────────────────────────────────────

    /**
     * 将 Java/Kotlin 对象转为 Python 指针
     */
    fun javaToPythonPtr(value: Any?): Long {
        if (value == null) return 0L

        // 已经是 PyObject，返回其指针
        if (value is PyObject) return value.rawPointer()

        // Kotlin 基本类型 → JNI 自动处理
        return nativeJavaToPython(value)
    }

    // ─── Python → Java ────────────────────────────────────────────

    /**
     * Python 对象 → 指定 Java/Kotlin 类型
     */
    fun <T> pythonToJava(pyObj: PyObject, clazz: Class<T>): Any? {
        if (pyObj.isNone) return null

        return when (clazz) {
            // 基本类型
            String::class.java, CharSequence::class.java -> pyObj.toStr()
            Int::class.java, Int::class.javaPrimitiveType -> pyObj.toInt()
            Long::class.java, Long::class.javaPrimitiveType -> pyObj.toLong()
            Double::class.java, Double::class.javaPrimitiveType -> pyObj.toDouble()
            Float::class.java, Float::class.javaPrimitiveType -> pyObj.toFloat()
            Boolean::class.java, Boolean::class.javaPrimitiveType -> pyObj.toBool()
            Byte::class.java, Byte::class.javaPrimitiveType -> pyObj.toLong().toByte()
            Short::class.java, Short::class.javaPrimitiveType -> pyObj.toLong().toShort()

            // 字节数组
            ByteArray::class.java -> pyObj.toBytes()

            // 集合
            List::class.java,
            java.util.ArrayList::class.java -> pyObj.toList()
            Map::class.java,
            java.util.LinkedHashMap::class.java -> pyObj.toMap()
            Set::class.java,
            java.util.LinkedHashSet::class.java -> pyObj.toList().toSet()

            // 嵌套泛型（通过 reified 处理）
            else -> {
                // 检查是否是数组类型
                if (clazz.isArray) {
                    val componentType = clazz.componentType
                    val list = pyObj.toList()
                    val array = java.lang.reflect.Array.newInstance(componentType, list.size)
                    list.forEachIndexed { i, item ->
                        java.lang.reflect.Array.set(array, i, item)
                    }
                    return array
                }

                // 回退：返回 PyObject
                pyObj
            }
        }
    }

    /**
     * Python 对象 → 自动推断的 Java 类型
     */
    fun pythonToAutoJava(pyObj: PyObject): Any? {
        if (pyObj.isNone) return null

        return when (pyObj.typeName) {
            "bool" -> pyObj.toBool()
            "int" -> {
                val v = pyObj.toLong()
                if (v in Int.MIN_VALUE..Int.MAX_VALUE) v.toInt() else v
            }
            "float" -> pyObj.toDouble()
            "str" -> pyObj.toStr()
            "bytes", "bytearray" -> pyObj.toBytes()
            "list", "tuple" -> pyObj.toList()
            "dict" -> pyObj.toMap()
            "set" -> pyObj.toList().toSet()
            else -> pyObj // 保持 PyObject 包装
        }
    }
}

// ─── Kotlin 内联扩展 ──────────────────────────────────────────────

/**
 * 泛型内联转换（避免类型擦除）
 *
 * ```kotlin
 * val names: List<String> = result.to<List<String>>()
 * ```
 */
inline fun <reified T> PyObject.toKt(): T {
    val result = TypeConverter.pythonToJava(this, T::class.java)
    @Suppress("UNCHECKED_CAST")
    return result as T
}
