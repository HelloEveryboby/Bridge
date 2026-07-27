package com.pybridge.javaapi;

import com.pybridge.core.PyObject;

/**
 * Java 纯净 API 包装器
 *
 * 提供纯 Java 风格的 API，完全隐藏 Kotlin 实现细节。
 * 适合不熟悉 Kotlin 的 Java 开发者。
 *
 * <pre>
 * // 初始化
 * PyBridge.initialize(this);
 *
 * // 使用 Builder 模式
 * double result = PyBridge.module("math")
 *     .call("sqrt", 16.0)
 *     .asDouble();
 *
 * // 或者用静态方法
 * String json = PyBridge.call("json.dumps", map).asString();
 * </pre>
 */
public final class PyBridge {

    private PyBridge() {} // 工具类，不可实例化

    // ─── 初始化 ───────────────────────────────────────────────────

    /**
     * 初始化 Python（自动检测路径）
     */
    public static void initialize(android.content.Context context) {
        JavaPython.initialize(context);
    }

    /**
     * 初始化 Python（自定义路径）
     */
    public static void initialize(android.content.Context context, String pythonHome) {
        JavaPython.initialize(context, pythonHome);
    }

    /**
     * 关闭 Python
     */
    public static void shutdown() {
        JavaPython.shutdown();
    }

    /**
     * 是否已初始化
     */
    public static boolean isInitialized() {
        return JavaPython.isInitialized();
    }

    /**
     * 获取 Python 版本
     */
    public static String getVersion() {
        return JavaPython.getVersion();
    }

    // ─── 模块/函数 ────────────────────────────────────────────────

    /**
     * 导入模块
     *
     * <pre>
     * PyObject math = PyBridge.module("math");
     * double pi = math.get("PI").asDouble();
     * </pre>
     */
    public static PyObject module(String name) {
        return JavaPython.getModule(name);
    }

    /**
     * 执行代码
     *
     * <pre>
     * PyObject result = PyBridge.exec("2 ** 10");
     * int value = result.asInt();  // 1024
     * </pre>
     */
    public static PyObject exec(String code) {
        return JavaPython.exec(code);
    }

    /**
     * 快捷调用模块函数
     *
     * <pre>
     * // 等价于 Python.module("json").callAttr("dumps", obj)
     * String json = PyBridge.call("json.dumps", myObject).asString();
     * </pre>
     */
    public static PyObject call(String moduleAndFunc, Object... args) {
        String[] parts = moduleAndFunc.split("\\.", 2);
        if (parts.length != 2) {
            throw new IllegalArgumentException("Expected 'module.function', got: " + moduleAndFunc);
        }
        PyObject mod = module(parts[0]);
        return mod.callAttr(parts[1], args);
    }

    /**
     * 包装 Java 对象为 PyObject
     */
    public static PyObject wrap(Object value) {
        return JavaPython.wrap(value);
    }

    /**
     * 添加模块路径
     */
    public static void addModulePath(String path) {
        JavaPython.addModulePath(path);
    }
}
