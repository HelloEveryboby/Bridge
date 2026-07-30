/**
 * PyBridge Static Proxy — JNI 实现（Bridge 框架 Python 子模块）
 *
 * 实现 Java 动态代理 → Python 方法调用的桥接。
 * 使用 pybridge_core.h API (pb_* 函数) 而非原始 Python.h。
 *
 * 对应 Java 类: com.bridge.py.proxy.PyBridgeProxyHandler
 *
 * 导出函数（供 Python 端 ctypes 调用）：
 *   pybridge_jni_create_proxy(proxy_id, iface_count, iface_names, method_count, method_names)
 *   pybridge_jni_release_proxy(proxy_id)
 *   pybridge_jni_proxy_invoke(proxy_id, method_name, arg_count, args)
 *   pybridge_jni_set_proxy_callback(callback)
 */

#include "pybridge/pybridge_core.h"
#include <jni.h>
#include <string.h>
#include <stdlib.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Bridge/PyBridge-Proxy", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Bridge/PyBridge-Proxy", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "Bridge/PyBridge-Proxy", __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) fprintf(stderr, "[Bridge/PyBridge-Proxy] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[Bridge/PyBridge-Proxy ERR] " __VA_ARGS__)
#define LOGW(...) fprintf(stderr, "[Bridge/PyBridge-Proxy WARN] " __VA_ARGS__)
#endif

/* ─── 全局 JVM 句柄 ──────────────────────────────────────────────── */

static JavaVM *g_jvm = NULL;  /* JVM 句柄，用于从任意线程获取 JNIEnv */

/* ─── 全局回调 ───────────────────────────────────────────────────── */

static PyObject *(*g_proxy_invoke_callback)(int, const char *, int, PyObject *) = NULL;

/* ─── 代理映射表 ─────────────────────────────────────────────────── */

#define MAX_PROXIES 512

typedef struct {
    int     proxy_id;
    jobject java_proxy;  /* global ref */
    int     active;
} ProxyMapping;

static ProxyMapping g_proxy_mappings[MAX_PROXIES];
static int g_proxy_count = 0;

/* ─── JNI 工具 ───────────────────────────────────────────────────── */

static JNIEnv *get_jni_env(void) {
    JNIEnv *env = NULL;
    if (!g_jvm) return NULL;
    jint result = (*g_jvm)->GetEnv(g_jvm, (void **)&env, JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        result = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (result != JNI_OK) return NULL;
    }
    return env;
}

static int proxy_mapping_find_free(void) {
    for (int i = 0; i < MAX_PROXIES; i++) {
        if (!g_proxy_mappings[i].active) return i;
    }
    return -1;
}

static int proxy_mapping_find_by_id(int proxy_id) {
    for (int i = 0; i < MAX_PROXIES; i++) {
        if (g_proxy_mappings[i].active && g_proxy_mappings[i].proxy_id == proxy_id) {
            return i;
        }
    }
    return -1;
}

/* ─── Java → Python 类型转换 ──────────────────────────────────────── */

static PyObject *jobject_to_pyobj(JNIEnv *env, jobject obj) {
    if (!obj) return pb_from_none();

    jclass cls = (*env)->GetObjectClass(env, obj);

    /* Boolean */
    jclass bool_cls = (*env)->FindClass(env, "java/lang/Boolean");
    if ((*env)->IsInstanceOf(env, obj, bool_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, bool_cls, "booleanValue", "()Z");
        jboolean v = (*env)->CallBooleanMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, bool_cls);
        (*env)->DeleteLocalRef(env, cls);
        return pb_from_bool(v);
    }
    (*env)->DeleteLocalRef(env, bool_cls);

    /* Integer */
    jclass int_cls = (*env)->FindClass(env, "java/lang/Integer");
    if ((*env)->IsInstanceOf(env, obj, int_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, int_cls, "intValue", "()I");
        jint v = (*env)->CallIntMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, int_cls);
        (*env)->DeleteLocalRef(env, cls);
        return pb_from_long(v);
    }
    (*env)->DeleteLocalRef(env, int_cls);

    /* Long */
    jclass long_cls = (*env)->FindClass(env, "java/lang/Long");
    if ((*env)->IsInstanceOf(env, obj, long_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, long_cls, "longValue", "()J");
        jlong v = (*env)->CallLongMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, long_cls);
        (*env)->DeleteLocalRef(env, cls);
        return pb_from_long(v);
    }
    (*env)->DeleteLocalRef(env, long_cls);

    /* Float/Double */
    jclass dbl_cls = (*env)->FindClass(env, "java/lang/Double");
    if ((*env)->IsInstanceOf(env, obj, dbl_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, dbl_cls, "doubleValue", "()D");
        jdouble v = (*env)->CallDoubleMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, dbl_cls);
        (*env)->DeleteLocalRef(env, cls);
        return pb_from_double(v);
    }
    (*env)->DeleteLocalRef(env, dbl_cls);

    /* String */
    jclass str_cls = (*env)->FindClass(env, "java/lang/String");
    if ((*env)->IsInstanceOf(env, obj, str_cls)) {
        const char *s = (*env)->GetStringUTFChars(env, (jstring)obj, NULL);
        PyObject *r = pb_from_string(s);
        (*env)->ReleaseStringUTFChars(env, (jstring)obj, s);
        (*env)->DeleteLocalRef(env, str_cls);
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }
    (*env)->DeleteLocalRef(env, str_cls);

    (*env)->DeleteLocalRef(env, cls);
    return pb_from_none();
}

/* ─── InvocationHandler ──────────────────────────────────────────── */

static jobject proxy_invoke_handler(
    JNIEnv *env, jobject proxy, jmethodID method, jobjectArray jargs)
{
    (void)method;

    /* 获取方法名 */
    jclass methodClass = (*env)->GetObjectClass(env, method);
    jmethodID getName = (*env)->GetMethodID(env, methodClass, "getName", "()Ljava/lang/String;");
    jstring jmethodName = (jstring)(*env)->CallObjectMethod(env, method, getName);

    const char *methodName = (*env)->GetStringUTFChars(env, jmethodName, NULL);
    if (!methodName) {
        (*env)->DeleteLocalRef(env, methodClass);
        return NULL;
    }

    /* 跳过 Object 方法 */
    if (!strcmp(methodName, "toString") || !strcmp(methodName, "hashCode") ||
        !strcmp(methodName, "equals") || !strcmp(methodName, "getClass")) {
        (*env)->ReleaseStringUTFChars(env, jmethodName, methodName);
        (*env)->DeleteLocalRef(env, methodClass);
        (*env)->DeleteLocalRef(env, jmethodName);

        if (!strcmp(methodName, "toString")) {
            return (*env)->NewStringUTF(env, "PyBridgeProxy");
        }
        if (!strcmp(methodName, "hashCode")) {
            jclass intClass = (*env)->FindClass(env, "java/lang/Integer");
            jmethodID valueOf = (*env)->GetStaticMethodID(env, intClass, "valueOf",
                "(I)Ljava/lang/Integer;");
            jobject result = (*env)->CallStaticObjectMethod(env, intClass, valueOf,
                (jint)(intptr_t)proxy);
            (*env)->DeleteLocalRef(env, intClass);
            return result;
        }
        return NULL;
    }

    /* 构建参数 */
    jsize argCount = jargs ? (*env)->GetArrayLength(env, jargs) : 0;
    PyObject *pyArgs = pb_list_new(argCount);

    for (jsize i = 0; i < argCount; i++) {
        jobject jarg = (*env)->GetObjectArrayElement(env, jargs, i);
        PyObject *pyArg = jobject_to_pyobj(env, jarg);
        pb_list_set(pyArgs, i, pyArg ? pyArg : pb_from_none());
        if (jarg) (*env)->DeleteLocalRef(env, jarg);
    }

    /* 查找 proxy_id */
    int proxy_id = -1;
    for (int i = 0; i < MAX_PROXIES; i++) {
        if (g_proxy_mappings[i].active &&
            (*env)->IsSameObject(env, proxy, g_proxy_mappings[i].java_proxy)) {
            proxy_id = g_proxy_mappings[i].proxy_id;
            break;
        }
    }

    jobject result = NULL;
    if (g_proxy_invoke_callback && proxy_id >= 0) {
        PyObject *pyResult = g_proxy_invoke_callback(proxy_id, methodName, argCount, pyArgs);
        if (pyResult) {
            /* 转换回 Java */
            if (pb_is_none(pyResult)) {
                result = NULL;
            } else if (PyBool_Check(pyResult)) {
                jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
                jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf",
                    "(Z)Ljava/lang/Boolean;");
                result = (*env)->CallStaticObjectMethod(env, cls, mid,
                    (jboolean)pb_to_bool(pyResult));
                (*env)->DeleteLocalRef(env, cls);
            } else if (PyLong_Check(pyResult)) {
                long v = pb_to_long(pyResult);
                jclass cls = (*env)->FindClass(env, "java/lang/Long");
                jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf",
                    "(J)Ljava/lang/Long;");
                result = (*env)->CallStaticObjectMethod(env, cls, mid, (jlong)v);
                (*env)->DeleteLocalRef(env, cls);
            } else if (PyFloat_Check(pyResult)) {
                jclass cls = (*env)->FindClass(env, "java/lang/Double");
                jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf",
                    "(D)Ljava/lang/Double;");
                result = (*env)->CallStaticObjectMethod(env, cls, mid,
                    (jdouble)pb_to_double(pyResult));
                (*env)->DeleteLocalRef(env, cls);
            } else if (PyUnicode_Check(pyResult)) {
                const char *s = pb_to_string(pyResult);
                result = s ? (*env)->NewStringUTF(env, s) : NULL;
            } else {
                const char *s = pb_to_string(pyResult);
                result = s ? (*env)->NewStringUTF(env, s) : NULL;
            }
            pb_decref(pyResult);
        }
    }

    pb_decref(pyArgs);
    (*env)->ReleaseStringUTFChars(env, jmethodName, methodName);
    (*env)->DeleteLocalRef(env, methodClass);
    (*env)->DeleteLocalRef(env, jmethodName);

    return result;
}

/* ═══════════════════════════════════════════════════════════════════
 * 导出函数: Python → ctypes 调用
 * ═══════════════════════════════════════════════════════════════════ */

__attribute__((visibility("default")))
void pybridge_jni_set_proxy_callback(void *callback) {
    g_proxy_invoke_callback = (PyObject *(*)(int, const char *, int, PyObject *))callback;
    LOGI("Proxy callback registered\n");
}

__attribute__((visibility("default")))
int pybridge_jni_create_proxy(
    int proxy_id, int interface_count, const char **interface_names,
    int method_count, const char **method_names)
{
    (void)method_names;

    JNIEnv *env = get_jni_env();
    if (!env) {
        LOGE("Cannot get JNIEnv\n");
        return -1;
    }

    /* 加载接口类 */
    jclass *iface_classes = (jclass *)malloc(sizeof(jclass) * interface_count);
    for (int i = 0; i < interface_count; i++) {
        char jni_name[256];
        snprintf(jni_name, sizeof(jni_name), "%s", interface_names[i]);
        for (char *p = jni_name; *p; p++) {
            if (*p == '.') *p = '/';
        }

        iface_classes[i] = (*env)->FindClass(env, jni_name);
        if (!iface_classes[i]) {
            LOGE("Interface not found: %s\n", interface_names[i]);
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
            for (int j = 0; j < i; j++) {
                (*env)->DeleteLocalRef(env, iface_classes[j]);
            }
            free(iface_classes);
            return -1;
        }
    }

    /* 构建接口 Class 数组 */
    jclass objClass = (*env)->FindClass(env, "java/lang/Class");
    jobjectArray ifaceArray = (*env)->NewObjectArray(env, (jsize)interface_count, objClass, NULL);
    for (int i = 0; i < interface_count; i++) {
        (*env)->SetObjectArrayElement(env, ifaceArray, (jsize)i, iface_classes[i]);
    }

    /* 创建 PyBridgeProxyHandler */
    jclass handlerClass = (*env)->FindClass(env, "com/bridge/py/proxy/PyBridgeProxyHandler");
    if (!handlerClass) {
        (*env)->ExceptionClear(env);
        LOGE("PyBridgeProxyHandler not found — add runtime/src/main/kotlin/com/bridge/py/proxy/\n");
        for (int i = 0; i < interface_count; i++) {
            (*env)->DeleteLocalRef(env, iface_classes[i]);
        }
        (*env)->DeleteLocalRef(env, ifaceArray);
        (*env)->DeleteLocalRef(env, objClass);
        free(iface_classes);
        return -1;
    }

    jmethodID handlerCtor = (*env)->GetMethodID(env, handlerClass, "<init>", "(I)V");
    if (!handlerCtor) {
        LOGE("Handler constructor not found\n");
        (*env)->DeleteLocalRef(env, handlerClass);
        goto cleanup;
    }

    jobject handler = (*env)->NewObject(env, handlerClass, handlerCtor, (jint)proxy_id);

    /* 获取 ClassLoader */
    jmethodID getClassLoader = (*env)->GetMethodID(env, objClass, "getClassLoader",
        "()Ljava/lang/ClassLoader;");
    jobject classLoader = (*env)->CallObjectMethod(env, iface_classes[0], getClassLoader);

    /* Proxy.newProxyInstance() */
    jclass proxyClass = (*env)->FindClass(env, "java/lang/reflect/Proxy");
    jmethodID newProxyInstance = (*env)->GetStaticMethodID(env, proxyClass, "newProxyInstance",
        "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");

    jobject javaProxy = (*env)->CallStaticObjectMethod(env, proxyClass, newProxyInstance,
        classLoader, ifaceArray, handler);

    if (!javaProxy) {
        LOGE("Failed to create Proxy\n");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, handler);
        (*env)->DeleteLocalRef(env, handlerClass);
        (*env)->DeleteLocalRef(env, proxyClass);
        (*env)->DeleteLocalRef(env, classLoader);
        goto cleanup;
    }

    /* 创建 global ref */
    jobject globalProxy = (*env)->NewGlobalRef(env, javaProxy);

    /* 存储到映射表 */
    int mapping_idx = proxy_mapping_find_free();
    if (mapping_idx < 0) {
        LOGE("Proxy mapping table full\n");
        (*env)->DeleteGlobalRef(env, globalProxy);
        (*env)->DeleteLocalRef(env, javaProxy);
        (*env)->DeleteLocalRef(env, handler);
        (*env)->DeleteLocalRef(env, handlerClass);
        (*env)->DeleteLocalRef(env, proxyClass);
        (*env)->DeleteLocalRef(env, classLoader);
        goto cleanup;
    }

    g_proxy_mappings[mapping_idx].proxy_id = proxy_id;
    g_proxy_mappings[mapping_idx].java_proxy = globalProxy;
    g_proxy_mappings[mapping_idx].active = 1;
    g_proxy_count++;

    LOGI("Created proxy id=%d, interfaces=%d\n", proxy_id, interface_count);

    /* 清理 */
    (*env)->DeleteLocalRef(env, javaProxy);
    (*env)->DeleteLocalRef(env, handler);
    (*env)->DeleteLocalRef(env, handlerClass);
    (*env)->DeleteLocalRef(env, proxyClass);
    (*env)->DeleteLocalRef(env, classLoader);

    int ref_id = mapping_idx; /* 使用映射索引作为引用 ID */

cleanup:
    for (int i = 0; i < interface_count; i++) {
        (*env)->DeleteLocalRef(env, iface_classes[i]);
    }
    (*env)->DeleteLocalRef(env, ifaceArray);
    (*env)->DeleteLocalRef(env, objClass);
    free(iface_classes);

    return ref_id >= 0 ? ref_id : -1;
}

__attribute__((visibility("default")))
void pybridge_jni_release_proxy(int proxy_id) {
    JNIEnv *env = get_jni_env();
    if (!env) return;

    int idx = proxy_mapping_find_by_id(proxy_id);
    if (idx < 0) return;

    if (g_proxy_mappings[idx].java_proxy) {
        (*env)->DeleteGlobalRef(env, g_proxy_mappings[idx].java_proxy);
    }

    g_proxy_mappings[idx].active = 0;
    g_proxy_mappings[idx].java_proxy = NULL;
    g_proxy_mappings[idx].proxy_id = 0;
    g_proxy_count--;

    LOGI("Released proxy id=%d\n", proxy_id);
}

__attribute__((visibility("default")))
PyObject *pybridge_jni_proxy_invoke(
    int proxy_id, const char *method_name, int arg_count, PyObject *args)
{
    if (!g_proxy_invoke_callback) {
        LOGE("No callback registered\n");
        return pb_from_none();
    }
    return g_proxy_invoke_callback(proxy_id, method_name, arg_count, args);
}

/* ═══════════════════════════════════════════════════════════════════
 * JNI 方法 — PyBridgeProxyHandler.nativeInvoke()
 * ═══════════════════════════════════════════════════════════════════ */

JNIEXPORT jobject JNICALL
Java_com_bridge_py_proxy_PyBridgeProxyHandler_nativeInvoke(
    JNIEnv *env, jobject handler, jint proxyId, jobject jmethod, jobjectArray jargs)
{
    return proxy_invoke_handler(env, handler, jmethod, jargs);
}

/* ═══════════════════════════════════════════════════════════════════
 * JNI_OnLoad — 存储 JVM 句柄
 * ═══════════════════════════════════════════════════════════════════ */

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_jvm = vm;
    memset(g_proxy_mappings, 0, sizeof(g_proxy_mappings));
    g_proxy_count = 0;
    LOGI("PyBridge Static Proxy JNI loaded\n");
    return JNI_VERSION_1_6;
}