package com.pybridge.proxy

import java.lang.reflect.InvocationHandler
import java.lang.reflect.Method
import java.lang.reflect.Proxy

/**
 * PyBridge 动态代理 InvocationHandler
 *
 * 将 Java 接口方法调用转发到 Python 端。
 * 每个 Handler 实例对应一个 Python 端的 PyClassProxy。
 *
 * 工作流程：
 *   1. Python 端创建 PyClassProxy，调用 JNI 创建 Java 动态代理
 *   2. Java 动态代理的所有方法调用进入此 Handler
 *   3. Handler 通过 nativeInvoke() 将调用转发到 Python 端
 *   4. Python 端执行实际方法并返回结果
 */
class PyBridgeProxyHandler(private val proxyId: Int) : InvocationHandler {

    companion object {
        init {
            System.loadLibrary("pybridge")
        }
    }

    private external fun nativeInvoke(
        proxyId: Int,
        method: Method,
        args: Array<Any?>?
    ): Any?

    override fun invoke(proxy: Any?, method: Method, args: Array<Any?>?): Any? {
        return when (method.name) {
            "toString" -> "PyBridgeProxy#$proxyId"
            "hashCode" -> proxyId.hashCode()
            "equals" -> {
                if (args != null && args.size == 1) {
                    proxy === args[0]
                } else {
                    false
                }
            }
            else -> {
                try {
                    nativeInvoke(proxyId, method, args)
                } catch (e: Exception) {
                    throw RuntimeException(
                        "PyBridge proxy invoke failed: ${method.name}", e
                    )
                }
            }
        }
    }
}

/**
 * PyProxy — 从 Python 对象创建 Java 代理的工具类
 */
object PyProxy {

    @JvmStatic
    fun <T> create(interfaceClass: Class<T>, pythonObject: com.pybridge.core.PyObject): T {
        val proxyId = createProxyInternal(interfaceClass.name, pythonObject)
        val handler = PyBridgeProxyHandler(proxyId)
        return Proxy.newProxyInstance(
            interfaceClass.classLoader,
            arrayOf<Class<*>>(interfaceClass),
            handler
        ) as T
    }

    @JvmStatic
    fun create(interfaces: Array<Class<*>>, pythonObject: com.pybridge.core.PyObject): Any {
        val proxyId = createProxyInternal(interfaces[0].name, pythonObject)
        val handler = PyBridgeProxyHandler(proxyId)
        return Proxy.newProxyInstance(
            interfaces[0].classLoader,
            interfaces,
            handler
        )
    }

    private fun createProxyInternal(interfaceName: String, pythonObject: com.pybridge.core.PyObject): Int {
        val mod = com.pybridge.core.Python.module("pybridge_java.static_proxy")
        return mod.callAttr("_create_proxy_for_object", interfaceName, pythonObject).toInt()
    }
}