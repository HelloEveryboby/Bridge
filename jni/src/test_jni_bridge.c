/**
 * JNI 桥接: TestJava ↔ pybridge_core
 *
 * JNI 函数名规则: Java_{类全路径}_{方法名}
 * TestJava 在 default package，所以是 Java_TestJava_xxx
 */
#include <jni.h>
#include "pybridge/pybridge_core.h"
#include <string.h>
#include <stdint.h>

/* TestJava.nativeInit */
JNIEXPORT jint JNICALL
Java_TestJava_nativeInit(JNIEnv* env, jclass clazz, jstring jhome, jobjectArray jpaths) {
    (void)clazz;
    const char* home = jhome ? (*env)->GetStringUTFChars(env, jhome, NULL) : NULL;
    int rc = pb_init(home);
    if (jhome) (*env)->ReleaseStringUTFChars(env, jhome, home);
    return rc;
}

/* TestJava.nativeFinalize */
JNIEXPORT void JNICALL
Java_TestJava_nativeFinalize(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    pb_finalize();
}

/* TestJava.nativeImportModule */
JNIEXPORT jlong JNICALL
Java_TestJava_nativeImportModule(JNIEnv* env, jclass clazz, jstring jname) {
    (void)clazz;
    const char* name = (*env)->GetStringUTFChars(env, jname, NULL);
    PyObject* mod = pb_import(name);
    (*env)->ReleaseStringUTFChars(env, jname, name);
    return (jlong)(intptr_t)mod;
}

/* TestJava.nativeCallFunction */
JNIEXPORT jobject JNICALL
Java_TestJava_nativeCallFunction(JNIEnv* env, jclass clazz,
                                  jlong ptr, jstring jfunc, jobjectArray jargs) {
    (void)clazz;
    PyObject* module = (PyObject*)(intptr_t)ptr;
    if (!module) return NULL;

    const char* func_name = (*env)->GetStringUTFChars(env, jfunc, NULL);

    /* 获取函数/属性 */
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
            PyObject* py_arg = NULL;

            if (!jarg) {
                py_arg = pb_from_none();
            } else {
                jclass cls = (*env)->GetObjectClass(env, jarg);

                /* String */
                jclass str_cls = (*env)->FindClass(env, "java/lang/String");
                if ((*env)->IsInstanceOf(env, jarg, str_cls)) {
                    const char* s = (*env)->GetStringUTFChars(env, (jstring)jarg, NULL);
                    py_arg = pb_from_string(s);
                    (*env)->ReleaseStringUTFChars(env, (jstring)jarg, s);
                }
                (*env)->DeleteLocalRef(env, str_cls);

                /* Integer */
                if (!py_arg) {
                    jclass int_cls = (*env)->FindClass(env, "java/lang/Integer");
                    if ((*env)->IsInstanceOf(env, jarg, int_cls)) {
                        jmethodID mid = (*env)->GetMethodID(env, int_cls, "intValue", "()I");
                        jint v = (*env)->CallIntMethod(env, jarg, mid);
                        py_arg = pb_from_long(v);
                    }
                    (*env)->DeleteLocalRef(env, int_cls);
                }

                /* Long */
                if (!py_arg) {
                    jclass long_cls = (*env)->FindClass(env, "java/lang/Long");
                    if ((*env)->IsInstanceOf(env, jarg, long_cls)) {
                        jmethodID mid = (*env)->GetMethodID(env, long_cls, "longValue", "()J");
                        jlong v = (*env)->CallLongMethod(env, jarg, mid);
                        py_arg = pb_from_long(v);
                    }
                    (*env)->DeleteLocalRef(env, long_cls);
                }

                /* Double */
                if (!py_arg) {
                    jclass dbl_cls = (*env)->FindClass(env, "java/lang/Double");
                    if ((*env)->IsInstanceOf(env, jarg, dbl_cls)) {
                        jmethodID mid = (*env)->GetMethodID(env, dbl_cls, "doubleValue", "()D");
                        jdouble v = (*env)->CallDoubleMethod(env, jarg, mid);
                        py_arg = pb_from_double(v);
                    }
                    (*env)->DeleteLocalRef(env, dbl_cls);
                }

                /* Boolean */
                if (!py_arg) {
                    jclass bool_cls = (*env)->FindClass(env, "java/lang/Boolean");
                    if ((*env)->IsInstanceOf(env, jarg, bool_cls)) {
                        jmethodID mid = (*env)->GetMethodID(env, bool_cls, "booleanValue", "()Z");
                        jboolean v = (*env)->CallBooleanMethod(env, jarg, mid);
                        py_arg = pb_from_bool(v);
                    }
                    (*env)->DeleteLocalRef(env, bool_cls);
                }

                /* List */
                if (!py_arg) {
                    jclass list_cls = (*env)->FindClass(env, "java/util/List");
                    if ((*env)->IsInstanceOf(env, jarg, list_cls)) {
                        jmethodID size_mid = (*env)->GetMethodID(env, list_cls, "size", "()I");
                        jmethodID get_mid = (*env)->GetMethodID(env, list_cls, "get", "(I)Ljava/lang/Object;");
                        jint len = (*env)->CallIntMethod(env, jarg, size_mid);
                        py_arg = pb_list_new(len);
                        for (jint j = 0; j < len; j++) {
                            jobject elem = (*env)->CallObjectMethod(env, jarg, get_mid, j);
                            /* 简化：只处理 Number */
                            jclass num_cls = (*env)->FindClass(env, "java/lang/Number");
                            if ((*env)->IsInstanceOf(env, elem, num_cls)) {
                                jmethodID dmid = (*env)->GetMethodID(env, num_cls, "doubleValue", "()D");
                                jdouble v = (*env)->CallDoubleMethod(env, elem, dmid);
                                pb_list_set(py_arg, j, pb_from_double(v));
                            }
                            (*env)->DeleteLocalRef(env, num_cls);
                            (*env)->DeleteLocalRef(env, elem);
                        }
                    }
                    (*env)->DeleteLocalRef(env, list_cls);
                }

                if (!py_arg) py_arg = pb_from_none();
                (*env)->DeleteLocalRef(env, cls);
            }

            pb_tuple_set(args, i, py_arg);
            (*env)->DeleteLocalRef(env, jarg);
        }
    }

    /* 调用 */
    PyObject* result = pb_call(func, args, NULL);
    pb_decref(func);

    /* 转换结果为 Java 对象 */
    if (!result || pb_is_none(result)) {
        pb_decref(result);
        return NULL;
    }

    /* bool → Boolean */
    if (pb_type_check(result, "bool")) {
        jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(Z)Ljava/lang/Boolean;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jboolean)pb_to_bool(result));
        (*env)->DeleteLocalRef(env, cls);
        pb_decref(result);
        return r;
    }

    /* int → Integer/Long */
    if (pb_type_check(result, "int")) {
        long v = pb_to_long(result);
        if (v >= -2147483648L && v <= 2147483647L) {
            jclass cls = (*env)->FindClass(env, "java/lang/Integer");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(I)Ljava/lang/Integer;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jint)v);
            (*env)->DeleteLocalRef(env, cls);
            pb_decref(result);
            return r;
        } else {
            jclass cls = (*env)->FindClass(env, "java/lang/Long");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(J)Ljava/lang/Long;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jlong)v);
            (*env)->DeleteLocalRef(env, cls);
            pb_decref(result);
            return r;
        }
    }

    /* float → Double */
    if (pb_type_check(result, "float")) {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(D)Ljava/lang/Double;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jdouble)pb_to_double(result));
        (*env)->DeleteLocalRef(env, cls);
        pb_decref(result);
        return r;
    }

    /* str → String */
    if (pb_type_check(result, "str")) {
        const char* s = pb_to_string(result);
        pb_decref(result);
        return s ? (*env)->NewStringUTF(env, s) : NULL;
    }

    /* list → ArrayList */
    if (pb_type_check(result, "list")) {
        long len = pb_list_size(result);
        jclass cls = (*env)->FindClass(env, "java/util/ArrayList");
        jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
        jmethodID add = (*env)->GetMethodID(env, cls, "add", "(Ljava/lang/Object;)Z");
        jobject list = (*env)->NewObject(env, cls, init, (jint)len);
        for (long i = 0; i < len; i++) {
            PyObject* item = pb_list_get(result, i);
            const char* s = pb_to_string(item);
            if (s) {
                jobject js = (*env)->NewStringUTF(env, s);
                (*env)->CallBooleanMethod(env, list, add, js);
                (*env)->DeleteLocalRef(env, js);
            }
        }
        (*env)->DeleteLocalRef(env, cls);
        pb_decref(result);
        return list;
    }

    /* fallback → String */
    const char* s = pb_to_string(result);
    pb_decref(result);
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}

/* TestJava.nativeExec */
JNIEXPORT jobject JNICALL
Java_TestJava_nativeExec(JNIEnv* env, jclass clazz, jstring jcode) {
    (void)clazz;
    const char* code = (*env)->GetStringUTFChars(env, jcode, NULL);

    /* 尝试 eval */
    PyObject* result = pb_eval(code);
    if (!result) {
        pb_err_clear();
        pb_exec(code);
        (*env)->ReleaseStringUTFChars(env, jcode, code);
        return NULL;
    }

    (*env)->ReleaseStringUTFChars(env, jcode, code);

    /* 转换结果 */
    if (pb_is_none(result)) { pb_decref(result); return NULL; }

    if (pb_type_check(result, "bool")) {
        jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(Z)Ljava/lang/Boolean;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jboolean)pb_to_bool(result));
        (*env)->DeleteLocalRef(env, cls);
        pb_decref(result);
        return r;
    }
    if (pb_type_check(result, "int")) {
        long v = pb_to_long(result);
        if (v >= -2147483648L && v <= 2147483647L) {
            jclass cls = (*env)->FindClass(env, "java/lang/Integer");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(I)Ljava/lang/Integer;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jint)v);
            (*env)->DeleteLocalRef(env, cls);
            pb_decref(result);
            return r;
        } else {
            jclass cls = (*env)->FindClass(env, "java/lang/Long");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(J)Ljava/lang/Long;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jlong)v);
            (*env)->DeleteLocalRef(env, cls);
            pb_decref(result);
            return r;
        }
    }
    if (pb_type_check(result, "float")) {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(D)Ljava/lang/Double;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jdouble)pb_to_double(result));
        (*env)->DeleteLocalRef(env, cls);
        pb_decref(result);
        return r;
    }
    if (pb_type_check(result, "str")) {
        const char* s = pb_to_string(result);
        pb_decref(result);
        return s ? (*env)->NewStringUTF(env, s) : NULL;
    }

    const char* s = pb_to_string(result);
    pb_decref(result);
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}

/* TestJava.nativeGetVersion */
JNIEXPORT jstring JNICALL
Java_TestJava_nativeGetVersion(JNIEnv* env, jclass clazz) {
    (void)clazz;
    return (*env)->NewStringUTF(env, pb_version());
}

/* TestJava.nativeDecRef */
JNIEXPORT void JNICALL
Java_TestJava_nativeDecRef(JNIEnv* env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    pb_decref((PyObject*)(intptr_t)ptr);
}
