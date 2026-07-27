package com.pybridge.core

import android.content.Context
import android.util.Log
import com.pybridge.exceptions.PyException
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * PyBridge Python 运行时入口（Kotlin 原生 API）
 *
 * Kotlin 用法：
 * ```kotlin
 * // 初始化（Application.onCreate）
 * Python.initialize(context)
 *
 * // 导入模块并调用
 * val math = Python.module("math")
 * val result = math.callAttr("sqrt", 16.0).toDouble()
 *
 * // 链式调用
 * val upper = Python.module("utils")
 *     .callAttr("get_text")
 *     .callAttr("upper")
 *     .toStr()
 *
 * // 执行表达式
 * val value = Python.exec("2 ** 10").toInt()
 *
 * // 类型安全转换
 * val list: List<*> = Python.module("json")
 *     .callAttr("loads", """[1,2,3]""")
 *     .toList()
 * ```
 */
object Python {

    private const val TAG = "PyBridge"

    @Volatile private var initialized = AtomicBoolean(false)
    @Volatile private var instance: PythonInstance? = null

    // ─── Native 方法 ──────────────────────────────────────────────

    @JvmStatic private external fun nativeInit(pythonHome: String?, modulePaths: Array<String>?): Int
    @JvmStatic private external fun nativeFinalize()
    @JvmStatic private external fun nativeImportModule(name: String): Long
    @JvmStatic private external fun nativeCallFunction(modulePtr: Long, funcName: String, args: Array<Any?>?): Any?
    @JvmStatic private external fun nativeExec(code: String): Any?
    @JvmStatic private external fun nativeGetAttr(objPtr: Long, name: String): Any?
    @JvmStatic private external fun nativeSetAttr(objPtr: Long, name: String, value: Any?)
    @JvmStatic private external fun nativeGetVersion(): String
    @JvmStatic private external fun nativeDecRef(ptr: Long)

    init {
        System.loadLibrary("pybridge")
    }

    // ─── 初始化 ───────────────────────────────────────────────────

    /**
     * 初始化 Python 运行时
     *
     * @param context Android Context
     * @param pythonHome Python 标准库路径（null=自动检测）
     * @param modulePaths 额外模块搜索路径
     */
    @JvmStatic
    @JvmOverloads
    fun initialize(
        context: Context,
        pythonHome: String? = null,
        modulePaths: Array<String>? = null
    ) {
        if (initialized.get()) {
            Log.w(TAG, "Python already initialized")
            return
        }

        synchronized(this) {
            if (initialized.get()) return

            Log.i(TAG, "Initializing PyBridge...")

            val home = pythonHome ?: defaultPythonHome(context)

            val paths = mutableListOf<String>().apply {
                add("$home/lib/python${getVersion()}")

                val userPython = File(context.filesDir, "python")
                if (userPython.exists()) add(userPython.absolutePath)

                val assetsPython = File(context.filesDir, "assets/python")
                if (assetsPython.exists()) add(assetsPython.absolutePath)

                val sitePackages = File(context.filesDir, "python/site-packages")
                if (sitePackages.exists()) add(sitePackages.absolutePath)

                modulePaths?.let { addAll(it) }
            }

            val result = nativeInit(home, paths.toTypedArray())
            if (result != 0) {
                throw PyException("Failed to initialize Python (error: $result)")
            }

            instance = PythonInstance()
            initialized.set(true)
            Log.i(TAG, "PyBridge initialized, Python ${getVersion()}")
        }
    }

    /**
     * 获取 Python 版本
     */
    @JvmStatic
    fun getVersion(): String = try {
        nativeGetVersion()
    } catch (_: UnsatisfiedLinkError) {
        "unknown"
    }

    /**
     * 是否已初始化
     */
    @JvmStatic
    fun isInitialized(): Boolean = initialized.get()

    // ─── Kotlin DSL 入口 ──────────────────────────────────────────

    /**
     * 导入模块（Kotlin 推荐用法）
     *
     * ```kotlin
     * val np = Python.module("numpy")
     * val arr = np.callAttr("array", listOf(1, 2, 3))
     * ```
     */
    @JvmStatic
    fun module(name: String): PyObject {
        checkInit()
        val ptr = nativeImportModule(name)
        if (ptr == 0L) throw PyException("Failed to import module: $name")
        return PyObject(ptr, name)
    }

    /**
     * 执行 Python 代码
     *
     * ```kotlin
     * val result = Python.exec("2 ** 10")  // 表达式
     * Python.exec("import os")             // 语句返回 null
     * ```
     */
    @JvmStatic
    fun exec(code: String): PyObject? {
        checkInit()
        val result = nativeExec(code)
        return result?.let { PyObject.fromJava(it) }
    }

    /**
     * Python 对象 → PyObject 包装
     */
    @JvmStatic
    fun wrap(value: Any?): PyObject {
        checkInit()
        return PyObject.fromJava(value)
    }

    /**
     * 添加模块搜索路径
     */
    @JvmStatic
    fun addModulePath(path: String) {
        module("sys").getAttr("path").callAttr("append", path)
    }

    /**
     * 关闭 Python 运行时
     */
    @JvmStatic
    fun shutdown() {
        if (initialized.compareAndSet(true, false)) {
            Log.i(TAG, "Shutting down PyBridge...")
            nativeFinalize()
            instance = null
        }
    }

    // ─── Java 友好的静态入口（JavaPyBridge 类会委托到这里） ────────

    internal fun checkInit() {
        check(initialized.get()) { "Python not initialized. Call Python.initialize(context) first." }
    }

    internal fun callFunctionRaw(modulePtr: Long, funcName: String, args: Array<Any?>?): Any? {
        checkInit()
        return nativeCallFunction(modulePtr, funcName, args)
    }

    internal fun importModuleRaw(name: String): Long {
        checkInit()
        return nativeImportModule(name)
    }

    internal fun getAttrRaw(ptr: Long, name: String): Any? {
        checkInit()
        return nativeGetAttr(ptr, name)
    }

    internal fun setAttrRaw(ptr: Long, name: String, value: Any?) {
        checkInit()
        nativeSetAttr(ptr, name, value)
    }

    internal fun execRaw(code: String): Any? {
        checkInit()
        return nativeExec(code)
    }

    internal fun decRefRaw(ptr: Long) {
        nativeDecRef(ptr)
    }

    // ─── 内部工具 ─────────────────────────────────────────────────

    private fun defaultPythonHome(context: Context): String {
        val internal = File(context.filesDir, "python")
        if (internal.exists()) return internal.absolutePath
        return File(context.filesDir, "assets/python").absolutePath
    }

    /**
     * 内部持有实例引用，防止过早 GC
     */
    private class PythonInstance
}
