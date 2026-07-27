/**
 * PyBridge JNI 封装层
 *
 * 将 C 桥接层 (pybridge_core.h) 封装为 Java 可调用的 native 方法。
 * 这是 Java/Kotlin ↔ CPython 的真正连接点。
 *
 * 对应 Java 类: com.pybridge.core.Python
 * 编译: NDK clang -shared -o libpybridge_jni.so pybridge_jni.c -lpybridge -lpython3.12
 */
#include <jni.h>
#include "pybridge/pybridge_core.h"
#include <string.h>
#include <stdint.h>

/* 日志: Android 用 __android_log_print, 桌面用 fprintf */
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PyBridge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PyBridge", __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) fprintf(stderr, "[PyBridge] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[PyBridge ERR] " __VA_ARGS__)
#endif

/* ── Java ↔ C 类型转换辅助 ───────────────────────────────────────── */

/**
 * 将 Java Object 自动转为 PyObject*
 *
 * 支持: null, Boolean, Integer, Long, Float, Double, String, byte[],
 *       List, Map, Set, Object[]
 */
static PyObject* jobject_to_pyobj(JNIEnv* env, jobject obj) {
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

    /* Float */
    jclass float_cls = (*env)->FindClass(env, "java/lang/Float");
    if ((*env)->IsInstanceOf(env, obj, float_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, float_cls, "floatValue", "()F");
        jfloat v = (*env)->CallFloatMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, float_cls);
        (*env)->DeleteLocalRef(env, cls);
        return pb_from_double(v);
    }
    (*env)->DeleteLocalRef(env, float_cls);

    /* Double */
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
        const char* s = (*env)->GetStringUTFChars(env, (jstring)obj, NULL);
        PyObject* r = pb_from_string(s);
        (*env)->ReleaseStringUTFChars(env, (jstring)obj, s);
        (*env)->DeleteLocalRef(env, str_cls);
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }
    (*env)->DeleteLocalRef(env, str_cls);

    /* byte[] */
    if ((*env)->IsInstanceOf(env, obj, (*env)->FindClass(env, "[B"))) {
        jbyteArray arr = (jbyteArray)obj;
        jsize len = (*env)->GetArrayLength(env, arr);
        jbyte* data = (*env)->GetByteArrayElements(env, arr, NULL);
        PyObject* r = pb_from_bytes((const char*)data, len);
        (*env)->ReleaseByteArrayElements(env, arr, data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }

    /* List / ArrayList */
    jclass list_cls = (*env)->FindClass(env, "java/util/List");
    if ((*env)->IsInstanceOf(env, obj, list_cls)) {
        jmethodID size_mid = (*env)->GetMethodID(env, list_cls, "size", "()I");
        jmethodID get_mid = (*env)->GetMethodID(env, list_cls, "get", "(I)Ljava/lang/Object;");
        jint len = (*env)->CallIntMethod(env, obj, size_mid);

        PyObject* list = pb_list_new(len);
        for (jint i = 0; i < len; i++) {
            jobject elem = (*env)->CallObjectMethod(env, obj, get_mid, i);
            PyObject* py_elem = jobject_to_pyobj(env, elem);
            pb_list_set(list, i, py_elem);
            (*env)->DeleteLocalRef(env, elem);
        }
        (*env)->DeleteLocalRef(env, list_cls);
        (*env)->DeleteLocalRef(env, cls);
        return list;
    }
    (*env)->DeleteLocalRef(env, list_cls);

    /* Map / HashMap */
    jclass map_cls = (*env)->FindClass(env, "java/util/Map");
    if ((*env)->IsInstanceOf(env, obj, map_cls)) {
        PyObject* dict = pb_dict_new();

        jclass set_cls = (*env)->FindClass(env, "java/util/Set");
        jclass iter_cls = (*env)->FindClass(env, "java/util/Iterator");
        jclass entry_cls = (*env)->FindClass(env, "java/util/Map$Entry");

        jmethodID entry_set = (*env)->GetMethodID(env, map_cls, "entrySet", "()Ljava/util/Set;");
        jmethodID iterator = (*env)->GetMethodID(env, set_cls, "iterator", "()Ljava/util/Iterator;");
        jmethodID has_next = (*env)->GetMethodID(env, iter_cls, "hasNext", "()Z");
        jmethodID next = (*env)->GetMethodID(env, iter_cls, "next", "()Ljava/lang/Object;");
        jmethodID get_key = (*env)->GetMethodID(env, entry_cls, "getKey", "()Ljava/lang/Object;");
        jmethodID get_val = (*env)->GetMethodID(env, entry_cls, "getValue", "()Ljava/lang/Object;");

        jobject entries = (*env)->CallObjectMethod(env, obj, entry_set);
        jobject iter = (*env)->CallObjectMethod(env, entries, iterator);

        while ((*env)->CallBooleanMethod(env, iter, has_next)) {
            jobject entry = (*env)->CallObjectMethod(env, iter, next);
            jobject key = (*env)->CallObjectMethod(env, entry, get_key);
            jobject val = (*env)->CallObjectMethod(env, entry, get_val);

            PyObject* py_key = jobject_to_pyobj(env, key);
            PyObject* py_val = jobject_to_pyobj(env, val);
            pb_dict_set(dict, py_key, py_val);

            (*env)->DeleteLocalRef(env, key);
            (*env)->DeleteLocalRef(env, val);
            (*env)->DeleteLocalRef(env, entry);
        }

        (*env)->DeleteLocalRef(env, iter);
        (*env)->DeleteLocalRef(env, entries);
        (*env)->DeleteLocalRef(env, entry_cls);
        (*env)->DeleteLocalRef(env, iter_cls);
        (*env)->DeleteLocalRef(env, set_cls);
        (*env)->DeleteLocalRef(env, map_cls);
        (*env)->DeleteLocalRef(env, cls);
        return dict;
    }
    (*env)->DeleteLocalRef(env, map_cls);

    /* int[] */
    if ((*env)->IsInstanceOf(env, obj, (*env)->FindClass(env, "[I"))) {
        jintArray arr = (jintArray)obj;
        jsize len = (*env)->GetArrayLength(env, arr);
        jint* data = (*env)->GetIntArrayElements(env, arr, NULL);
        PyObject* list = pb_list_new(len);
        for (jsize i = 0; i < len; i++) {
            pb_list_set(list, i, pb_from_long(data[i]));
        }
        (*env)->ReleaseIntArrayElements(env, arr, data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, cls);
        return list;
    }

    /* double[] */
    if ((*env)->IsInstanceOf(env, obj, (*env)->FindClass(env, "[D"))) {
        jdoubleArray arr = (jdoubleArray)obj;
        jsize len = (*env)->GetArrayLength(env, arr);
        jdouble* data = (*env)->GetDoubleArrayElements(env, arr, NULL);
        PyObject* list = pb_list_new(len);
        for (jsize i = 0; i < len; i++) {
            pb_list_set(list, i, pb_from_double(data[i]));
        }
        (*env)->ReleaseDoubleArrayElements(env, arr, data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, cls);
        return list;
    }

    /* 未知类型 → None */
    (*env)->DeleteLocalRef(env, cls);
    return pb_from_none();
}

/**
 * 将 PyObject* 自动转为 Java Object
 */
static jobject pyobj_to_jobject(JNIEnv* env, PyObject* obj) {
    if (pb_is_none(obj)) return NULL;

    /* bool (必须在 int 之前检查，因为 Python bool 是 int 子类) */
    if (PyBool_Check(obj)) {
        jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(Z)Ljava/lang/Boolean;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jboolean)pb_to_bool(obj));
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }

    /* int */
    if (PyLong_Check(obj)) {
        long v = pb_to_long(obj);
        /* 小整数用 Integer，大整数用 Long */
        if (v >= -2147483648L && v <= 2147483647L) {
            jclass cls = (*env)->FindClass(env, "java/lang/Integer");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(I)Ljava/lang/Integer;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jint)v);
            (*env)->DeleteLocalRef(env, cls);
            return r;
        } else {
            jclass cls = (*env)->FindClass(env, "java/lang/Long");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(J)Ljava/lang/Long;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jlong)v);
            (*env)->DeleteLocalRef(env, cls);
            return r;
        }
    }

    /* float */
    if (PyFloat_Check(obj)) {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(D)Ljava/lang/Double;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jdouble)pb_to_double(obj));
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }

    /* str */
    if (PyUnicode_Check(obj)) {
        const char* s = pb_to_string(obj);
        return s ? (*env)->NewStringUTF(env, s) : NULL;
    }

    /* bytes */
    if (PyBytes_Check(obj)) {
        const char* data = pb_py_bytes_as_string(obj);
        Py_ssize_t len = pb_py_bytes_size(obj);
        jbyteArray arr = (*env)->NewByteArray(env, len);
        (*env)->SetByteArrayRegion(env, arr, 0, len, (const jbyte*)data);
        return arr;
    }

    /* list / tuple → ArrayList */
    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t len = PyList_Check(obj) ? pb_list_size(obj) : pb_py_tuple_size(obj);

        jclass cls = (*env)->FindClass(env, "java/util/ArrayList");
        jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
        jmethodID add = (*env)->GetMethodID(env, cls, "add", "(Ljava/lang/Object;)Z");
        jobject list = (*env)->NewObject(env, cls, init, (jint)len);

        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject* item = PyList_Check(obj) ? pb_list_get(obj, i) : pb_py_tuple_get(obj, i);
            jobject jitem = pyobj_to_jobject(env, item);
            (*env)->CallBooleanMethod(env, list, add, jitem);
            if (jitem) (*env)->DeleteLocalRef(env, jitem);
        }
        (*env)->DeleteLocalRef(env, cls);
        return list;
    }

    /* dict → HashMap */
    if (PyDict_Check(obj)) {
        jclass cls = (*env)->FindClass(env, "java/util/HashMap");
        jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
        jmethodID put = (*env)->GetMethodID(env, cls, "put",
            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
        jobject map = (*env)->NewObject(env, cls, init, (jint)pb_eval("len(__pb_eval_result)"));
        /* TODO: 遍历 dict */
        (*env)->DeleteLocalRef(env, cls);
        return map;
    }

    /* fallback: 转为 String */
    const char* s = pb_to_string(obj);
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}

/* ── JNI Native 方法实现 ─────────────────────────────────────────── */

/*
 * com.pybridge.core.Python.nativeInit
 */
JNIEXPORT jint JNICALL
Java_com_pybridge_core_Python_nativeInit(JNIEnv* env, jclass clazz,
                                          jstring jhome, jobjectArray jpaths) {
    (void)clazz;

    const char* home = jhome ? (*env)->GetStringUTFChars(env, jhome, NULL) : NULL;
    int rc = pb_init(home);
    if (jhome) (*env)->ReleaseStringUTFChars(env, jhome, home);

    if (rc != 0) {
        LOGE("pb_init failed");
        return (jint)rc;
    }

    /* 添加模块路径 */
    if (jpaths) {
        jsize len = (*env)->GetArrayLength(env, jpaths);
        for (jsize i = 0; i < len; i++) {
            jstring jpath = (jstring)(*env)->GetObjectArrayElement(env, jpaths, i);
            const char* path = (*env)->GetStringUTFChars(env, jpath, NULL);
            pb_add_path(path);
            (*env)->ReleaseStringUTFChars(env, jpath, path);
            (*env)->DeleteLocalRef(env, jpath);
        }
    }

    LOGI("Python %s initialized", pb_version());
    return 0;
}

/*
 * com.pybridge.core.Python.nativeFinalize
 */
JNIEXPORT void JNICALL
Java_com_pybridge_core_Python_nativeFinalize(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    pb_finalize();
    LOGI("Python finalized");
}

/*
 * com.pybridge.core.Python.nativeImportModule
 */
JNIEXPORT jlong JNICALL
Java_com_pybridge_core_Python_nativeImportModule(JNIEnv* env, jclass clazz,
                                                   jstring jname) {
    (void)clazz;
    const char* name = (*env)->GetStringUTFChars(env, jname, NULL);
    PyObject* mod = pb_import(name);
    (*env)->ReleaseStringUTFChars(env, jname, name);
    return (jlong)(intptr_t)mod;
}

/*
 * com.pybridge.core.Python.nativeCallFunction
 */
JNIEXPORT jobject JNICALL
Java_com_pybridge_core_Python_nativeCallFunction(JNIEnv* env, jclass clazz,
                                                   jlong ptr, jstring jfunc,
                                                   jobjectArray jargs) {
    (void)clazz;
    PyObject* module = (PyObject*)(intptr_t)ptr;
    if (!module) return NULL;

    /* 获取函数 */
    const char* func_name = (*env)->GetStringUTFChars(env, jfunc, NULL);
    PyObject* func = pb_getattr(module, func_name);
    (*env)->ReleaseStringUTFChars(env, jfunc, func_name);

    if (!func) return NULL;

    /* 转换参数 */
    PyObject* args = NULL;
    if (jargs) {
        jsize argc = (*env)->GetArrayLength(env, jargs);
        args = pb_tuple_new(argc);
        for (jsize i = 0; i < argc; i++) {
            jobject jarg = (*env)->GetObjectArrayElement(env, jargs, i);
            PyObject* py_arg = jobject_to_pyobj(env, jarg);
            pb_tuple_set(args, i, py_arg);
            (*env)->DeleteLocalRef(env, jarg);
        }
    }

    /* 调用 */
    PyObject* result = pb_call(func, args, NULL);
    pb_decref(func);

    /* 转换结果 */
    jobject jresult = result ? pyobj_to_jobject(env, result) : NULL;
    pb_decref(result);
    return jresult;
}

/*
 * com.pybridge.core.Python.nativeExec
 */
JNIEXPORT jobject JNICALL
Java_com_pybridge_core_Python_nativeExec(JNIEnv* env, jclass clazz, jstring jcode) {
    (void)clazz;
    const char* code = (*env)->GetStringUTFChars(env, jcode, NULL);

    /* 尝试 eval（表达式），失败则 exec（语句） */
    PyObject* result = pb_eval(code);
    if (!result) {
        pb_err_clear();
        pb_exec(code);
        (*env)->ReleaseStringUTFChars(env, jcode, code);
        return NULL;
    }

    (*env)->ReleaseStringUTFChars(env, jcode, code);
    jobject jresult = pyobj_to_jobject(env, result);
    pb_decref(result);
    return jresult;
}

/*
 * com.pybridge.core.Python.nativeGetVersion
 */
JNIEXPORT jstring JNICALL
Java_com_pybridge_core_Python_nativeGetVersion(JNIEnv* env, jclass clazz) {
    (void)clazz;
    return (*env)->NewStringUTF(env, pb_version());
}

/*
 * com.pybridge.core.Python.nativeDecRef
 */
JNIEXPORT void JNICALL
Java_com_pybridge_core_Python_nativeDecRef(JNIEnv* env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    pb_decref((PyObject*)(intptr_t)ptr);
}
