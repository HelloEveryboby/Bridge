package com.pybridge.javaapi

import android.content.Context
import com.pybridge.core.Python as KotlinPython
import com.pybridge.core.PyObject

/**
 * Java 友好的 Python 运行时入口
 *
 * Java 用法：
 * <pre>
 * // 初始化
 * JavaPython.initialize(this);
 *
 * // 导入模块
 * PyObject math = JavaPython.getModule("math");
 *
 * // 调用函数
 * double result = math.callAttr("sqrt", 16.0).toDouble();
 *
 * // 链式调用
 * String text = JavaPython.getModule("utils")
 *     .callAttr("process", input)
 *     .callAttr("upper")
 *     .toStr();
 *
 * // 执行代码
 * PyObject value = JavaPython.exec("2 ** 10");
 * int num = value.toInt();
 * </pre>
 */
object JavaPython {

    /**
     * 初始化 Python 运行时
     */
    @JvmStatic
    fun initialize(context: Context) {
        KotlinPython.initialize(context)
    }

    /**
     * 初始化 Python 运行时（自定义路径）
     */
    @JvmStatic
    fun initialize(context: Context, pythonHome: String) {
        KotlinPython.initialize(context, pythonHome)
    }

    /**
     * 初始化 Python 运行时（自定义路径 + 模块路径）
     */
    @JvmStatic
    fun initialize(context: Context, pythonHome: String?, modulePaths: Array<String>?) {
        KotlinPython.initialize(context, pythonHome, modulePaths)
    }

    /**
     * 导入 Python 模块
     *
     * @param name 模块名，如 "math" 或 "mypackage.submodule"
     * @return PyObject 模块对象
     * @throws com.pybridge.exceptions.PyException 如果模块不存在
     */
    @JvmStatic
    fun getModule(name: String): PyObject = KotlinPython.module(name)

    /**
     * 执行 Python 代码
     *
     * @param code Python 表达式或语句
     * @return 表达式结果，语句返回 null
     */
    @JvmStatic
    fun exec(code: String): PyObject? = KotlinPython.exec(code)

    /**
     * 获取 Python 版本
     */
    @JvmStatic
    fun getVersion(): String = KotlinPython.getVersion()

    /**
     * 是否已初始化
     */
    @JvmStatic
    fun isInitialized(): Boolean = KotlinPython.isInitialized()

    /**
     * 添加模块搜索路径
     */
    @JvmStatic
    fun addModulePath(path: String) = KotlinPython.addModulePath(path)

    /**
     * 包装 Java 对象为 PyObject
     */
    @JvmStatic
    fun wrap(value: Any?): PyObject = KotlinPython.wrap(value)

    /**
     * 关闭 Python 运行时
     */
    @JvmStatic
    fun shutdown() = KotlinPython.shutdown()
}
