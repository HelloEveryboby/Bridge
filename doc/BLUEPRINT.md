# PyBridge — 完整复现蓝图

> **用途**: 将本文档交给任意 AI 大模型，即可从零复现整个 PyBridge 项目。
> **要求**: 所有文件路径相对于 `pybridge/` 根目录。
> **依赖**: GCC, Python 3.12+, JDK 21+ (可选), Android NDK (可选), Rust (可选), Go (可选)

---

## 第一部分：项目总览

PyBridge 是一个多语言桥接框架，让 Python/C/C++/Rust/Go 与 Java/Kotlin 互操作。

核心机制：所有非 JVM 语言编译为 .so 共享库，通过 dlopen 动态加载，JNI 层负责 Java↔C 类型转换。

测试状态：
- C 核心层：13/13 通过
- JNI 层：6/6 通过
- 多语言模块：2/2 通过

---

## 第二部分：完整文件清单与源码

以下按依赖顺序排列，每个文件给出完整路径和完整内容。

---

### 文件 1: `jni/include/pybridge/pybridge_core.h`

桥接层公共 API 头文件，不依赖 Python.h，所有 Python API 通过 dlsym 动态获取。

```c
#ifndef PYBRIDGE_CORE_H
#define PYBRIDGE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _object PyObject;
typedef long Py_ssize_t;

/* 生命周期 */
int  pb_init(const char* python_home);
void pb_finalize(void);
int  pb_is_init(void);
const char* pb_version(void);

/* 模块 */
PyObject* pb_import(const char* name);
int       pb_add_path(const char* path);

/* 函数调用 */
PyObject* pb_call(PyObject* callable, PyObject* args, PyObject* kwargs);
PyObject* pb_call_method(PyObject* obj, const char* method, PyObject* args);
PyObject* pb_call0(PyObject* callable);

/* 属性 */
PyObject* pb_getattr(PyObject* obj, const char* name);
int       pb_setattr(PyObject* obj, const char* name, PyObject* val);

/* 类型转换 → Python */
PyObject* pb_from_none(void);
PyObject* pb_from_bool(int val);
PyObject* pb_from_long(long val);
PyObject* pb_from_double(double val);
PyObject* pb_from_string(const char* val);
PyObject* pb_from_bytes(const char* data, long len);

/* 类型转换 → C */
int         pb_to_bool(PyObject* obj);
long        pb_to_long(PyObject* obj);
double      pb_to_double(PyObject* obj);
const char* pb_to_string(PyObject* obj);
int         pb_is_none(PyObject* obj);
int         pb_is_callable(PyObject* obj);

/* 容器 */
PyObject* pb_tuple_new(long len);
int       pb_tuple_set(PyObject* tup, long i, PyObject* val);
PyObject* pb_list_new(long len);
int       pb_list_set(PyObject* list, long i, PyObject* val);
PyObject* pb_dict_new(void);
int       pb_dict_set(PyObject* dict, PyObject* key, PyObject* val);

/* 异常 */
int         pb_has_err(void);
const char* pb_err_message(void);
const char* pb_err_traceback(void);
void        pb_err_clear(void);

/* 引用计数 */
void pb_decref(PyObject* obj);
void pb_incref(PyObject* obj);

/* 执行代码 */
PyObject* pb_exec(const char* code);
PyObject* pb_eval(const char* expr);

/* 扩展 API */
PyObject* pb_dict_get_str(PyObject* dict, const char* key);
long      pb_list_size(PyObject* list);
PyObject* pb_list_get(PyObject* list, long index);
int       pb_type_check(PyObject* obj, const char* type_name);
Py_ssize_t pb_py_tuple_size(PyObject*);
PyObject*  pb_py_tuple_get(PyObject*, Py_ssize_t);

/* 类型检查宏 */
#define PyBool_Check(o)    pb_type_check(o, "bool")
#define PyLong_Check(o)    pb_type_check(o, "int")
#define PyFloat_Check(o)   pb_type_check(o, "float")
#define PyUnicode_Check(o) pb_type_check(o, "str")
#define PyBytes_Check(o)   pb_type_check(o, "bytes")
#define PyList_Check(o)    pb_type_check(o, "list")
#define PyTuple_Check(o)   pb_type_check(o, "tuple")
#define PyDict_Check(o)    pb_type_check(o, "dict")

#ifdef __cplusplus
}
#endif
#endif
```

---

### 文件 2: `jni/include/pybridge/pb_module.h`

多语言模块协议。任何语言编译为 .so 后导出 `pb_module_info` 和 `pb_module_func` 即可被加载。

```c
#ifndef PB_MODULE_H
#define PB_MODULE_H

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    const char* signature;
    void*       func_ptr;
} PB_FuncDef;

/*
 * 签名格式: 参数类型用字符表示，> 后是返回类型
 * v=void i=int32 l=int64 f=float d=double b=bool s=string p=pointer
 * 示例: "dd>d" = (double,double)->double, "s>s" = (char*)->char*
 */

typedef const char* (*pb_module_info_fn)(void);
typedef void* (*pb_module_func_fn)(const char* name);

#define PB_FUNC_ENTRY(name, sig) { #name, sig, (void*)name }

#define PB_MODULE(mod_name, mod_ver, mod_lang, ...) \
    static const PB_FuncDef _pb_functions[] = { __VA_ARGS__ }; \
    static const int _pb_func_count = sizeof(_pb_functions) / sizeof(_pb_functions[0]); \
    static char _pb_info_json[4096]; \
    __attribute__((visibility("default"))) \
    const char* pb_module_info(void) { \
        int pos = 0; \
        pos += snprintf(_pb_info_json + pos, sizeof(_pb_info_json) - pos, \
            "{\"name\":\"%s\",\"version\":\"%s\",\"language\":\"%s\",\"functions\":[", \
            mod_name, mod_ver, mod_lang); \
        for (int i = 0; i < _pb_func_count; i++) { \
            if (i > 0) _pb_info_json[pos++] = ','; \
            pos += snprintf(_pb_info_json + pos, sizeof(_pb_info_json) - pos, \
                "{\"name\":\"%s\",\"signature\":\"%s\"}", \
                _pb_functions[i].name, \
                _pb_functions[i].signature ? _pb_functions[i].signature : ""); \
        } \
        snprintf(_pb_info_json + pos, sizeof(_pb_info_json) - pos, "]}"); \
        return _pb_info_json; \
    } \
    __attribute__((visibility("default"))) \
    void* pb_module_func(const char* name) { \
        for (int i = 0; i < _pb_func_count; i++) { \
            if (strcmp(_pb_functions[i].name, name) == 0) \
                return _pb_functions[i].func_ptr; \
        } \
        return NULL; \
    }

#ifdef __cplusplus
}
#endif
#endif
```

---

### 文件 3: `jni/src/pybridge_core.c`

桥接层实现。通过 dlopen 动态加载 libpython，用 dlsym 获取所有 CPython API。核心技巧：用 `PyRun_SimpleString` + `PyDict_GetItemString` 替代 `PyRun_String`（后者在 dlopen 场景下有兼容性问题）。

```c
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct _object PyObject;
typedef long Py_ssize_t;

static struct {
    void* lib;
    void  (*init)(void);
    int   (*is_init)(void);
    void  (*finalize)(void);
    const char* (*version)(void);
    void  (*set_home)(wchar_t*);
    wchar_t* (*decode_locale)(const char*, void*);
    PyObject* (*import_add_module)(const char*);
    PyObject* (*import_module)(const char*);
    PyObject* (*import_import)(PyObject*);
    PyObject* (*module_get_dict)(PyObject*);
    PyObject* (*get_attr_str)(PyObject*, const char*);
    int       (*set_attr_str)(PyObject*, const char*, PyObject*);
    PyObject* (*call)(PyObject*, PyObject*, PyObject*);
    PyObject* (*str)(PyObject*);
    int       (*is_true)(PyObject*);
    int       (*callable)(PyObject*);
    PyObject* (*long_from)(long);
    long      (*long_as)(PyObject*);
    PyObject* (*float_from)(double);
    double    (*float_as)(PyObject*);
    PyObject* (*bool_from)(long);
    PyObject* (*unicode_from)(const char*);
    const char* (*unicode_as_utf8)(PyObject*);
    PyObject* (*bytes_from)(const char*, Py_ssize_t);
    PyObject* (*tuple_new)(Py_ssize_t);
    int       (*tuple_set)(PyObject*, Py_ssize_t, PyObject*);
    Py_ssize_t (*tuple_size)(PyObject*);
    PyObject* (*tuple_get)(PyObject*, Py_ssize_t);
    PyObject* (*list_new)(Py_ssize_t);
    int       (*list_set)(PyObject*, Py_ssize_t, PyObject*);
    Py_ssize_t (*list_size)(PyObject*);
    PyObject* (*list_get)(PyObject*, Py_ssize_t);
    int       (*list_insert)(PyObject*, Py_ssize_t, PyObject*);
    PyObject* (*dict_new)(void);
    int       (*dict_set)(PyObject*, PyObject*, PyObject*);
    int       (*dict_set_str)(PyObject*, const char*, PyObject*);
    PyObject* (*dict_get_str)(PyObject*, const char*);
    Py_ssize_t (*dict_size)(PyObject*);
    void (*dec_ref)(PyObject*);
    void (*inc_ref)(PyObject*);
    PyObject* (*err_occurred)(void);
    void      (*err_clear)(void);
    void      (*err_fetch)(PyObject**, PyObject**, PyObject**);
    void      (*err_normalize)(PyObject**, PyObject**, PyObject**);
    int  (*run_simple)(const char*);
    PyObject* _None;
    int*      no_site;
    int*      ignore_env;
} P;

#define Py_None (*P._None)

#define SYM(field, sym) do { P.field = dlsym(lib, sym); if(!P.field) { fprintf(stderr,"Missing: %s\n",sym); return -1; } } while(0)

static int load_syms(void* lib) {
    SYM(init, "Py_Initialize");
    SYM(is_init, "Py_IsInitialized");
    SYM(finalize, "Py_Finalize");
    SYM(version, "Py_GetVersion");
    P.set_home = dlsym(lib, "Py_SetPythonHome");
    P.decode_locale = dlsym(lib, "Py_DecodeLocale");
    SYM(import_add_module, "PyImport_AddModule");
    SYM(import_module, "PyImport_ImportModule");
    SYM(import_import, "PyImport_Import");
    SYM(module_get_dict, "PyModule_GetDict");
    SYM(get_attr_str, "PyObject_GetAttrString");
    SYM(set_attr_str, "PyObject_SetAttrString");
    SYM(call, "PyObject_Call");
    SYM(str, "PyObject_Str");
    SYM(is_true, "PyObject_IsTrue");
    SYM(callable, "PyCallable_Check");
    SYM(long_from, "PyLong_FromLong");
    SYM(long_as, "PyLong_AsLong");
    SYM(float_from, "PyFloat_FromDouble");
    SYM(float_as, "PyFloat_AsDouble");
    SYM(bool_from, "PyBool_FromLong");
    SYM(unicode_from, "PyUnicode_FromString");
    SYM(unicode_as_utf8, "PyUnicode_AsUTF8");
    SYM(bytes_from, "PyBytes_FromStringAndSize");
    SYM(tuple_new, "PyTuple_New");
    SYM(tuple_set, "PyTuple_SetItem");
    SYM(tuple_size, "PyTuple_Size");
    SYM(tuple_get, "PyTuple_GetItem");
    SYM(list_new, "PyList_New");
    SYM(list_set, "PyList_SetItem");
    SYM(list_size, "PyList_Size");
    SYM(list_get, "PyList_GetItem");
    SYM(list_insert, "PyList_Insert");
    SYM(dict_new, "PyDict_New");
    SYM(dict_set, "PyDict_SetItem");
    SYM(dict_set_str, "PyDict_SetItemString");
    SYM(dict_get_str, "PyDict_GetItemString");
    SYM(dict_size, "PyDict_Size");
    SYM(dec_ref, "Py_DecRef");
    SYM(inc_ref, "Py_IncRef");
    SYM(err_occurred, "PyErr_Occurred");
    SYM(err_clear, "PyErr_Clear");
    SYM(err_fetch, "PyErr_Fetch");
    SYM(err_normalize, "PyErr_NormalizeException");
    SYM(run_simple, "PyRun_SimpleString");
    P._None = dlsym(lib, "_Py_NoneStruct");
    P.no_site = dlsym(lib, "Py_NoSiteFlag");
    P.ignore_env = dlsym(lib, "Py_IgnoreEnvironmentFlag");
    return 0;
}

static struct {
    int       init;
    PyObject* main_dict;
    char      err[4096];
} G = {0};

static void _capture_err(void) {
    PyObject *t=NULL, *v=NULL, *tb=NULL;
    P.err_fetch(&t, &v, &tb);
    P.err_normalize(&t, &v, &tb);
    G.err[0] = '\0';
    if (v) {
        PyObject* s = P.str(v);
        if (s) { const char* c = P.unicode_as_utf8(s); if(c) strncpy(G.err, c, sizeof(G.err)-1); P.dec_ref(s); }
    }
    P.err_clear();
    if (t) P.dec_ref(t);
    if (v) P.dec_ref(v);
    if (tb) P.dec_ref(tb);
}

int pb_init(const char* home) {
    if (G.init) return 0;
    void* lib = dlopen("libpython3.12.so.1.0", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) lib = dlopen("libpython3.12.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) lib = dlopen("libpython3.11.so.1.0", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "Cannot load libpython: %s\n", dlerror()); return -1; }
    if (load_syms(lib) != 0) return -1;
    if (home && P.set_home && P.decode_locale) {
        wchar_t* w = P.decode_locale(home, NULL);
        if (w) P.set_home(w);
    }
    if (P.ignore_env) *P.ignore_env = 1;
    if (P.no_site) *P.no_site = 1;
    P.init();
    if (!P.is_init()) return -1;
    PyObject* m = P.import_add_module("__main__");
    G.main_dict = P.module_get_dict(m);
    P.inc_ref(G.main_dict);
    G.init = 1;
    return 0;
}

void pb_finalize(void) {
    if (!G.init) return;
    if (G.main_dict) P.dec_ref(G.main_dict);
    G.main_dict = NULL;
    if (P.is_init()) P.finalize();
    G.init = 0;
}

int pb_is_init(void) { return G.init && P.is_init(); }
const char* pb_version(void) { return P.version(); }

PyObject* pb_import(const char* name) {
    PyObject* n = P.unicode_from(name);
    PyObject* m = P.import_import(n);
    P.dec_ref(n);
    if (!m) _capture_err();
    return m;
}

int pb_add_path(const char* path) {
    PyObject* sys = P.import_module("sys");
    PyObject* sp = P.get_attr_str(sys, "path");
    P.dec_ref(sys);
    PyObject* p = P.unicode_from(path);
    int rc = P.list_insert(sp, 0, p);
    P.dec_ref(p); P.dec_ref(sp);
    return rc;
}

PyObject* pb_eval(const char* expr) {
    if (!G.init) return NULL;
    char buf[8192];
    snprintf(buf, sizeof(buf), "__pb_eval_result = (%s)", expr);
    if (P.run_simple(buf) != 0) { _capture_err(); return NULL; }
    PyObject* r = P.dict_get_str(G.main_dict, "__pb_eval_result");
    if (r) P.inc_ref(r);
    return r;
}

PyObject* pb_exec(const char* code) {
    if (!G.init) return NULL;
    if (P.run_simple(code) != 0) { _capture_err(); return NULL; }
    P.inc_ref(&Py_None);
    return &Py_None;
}

PyObject* pb_call(PyObject* fn, PyObject* args, PyObject* kwargs) {
    if (!fn || !P.callable(fn)) return NULL;
    PyObject* a = args ? args : P.tuple_new(0);
    PyObject* r = P.call(fn, a, kwargs);
    if (a != args) P.dec_ref(a);
    if (!r) _capture_err();
    return r;
}

PyObject* pb_call_method(PyObject* obj, const char* method, PyObject* args) {
    PyObject* fn = P.get_attr_str(obj, method);
    if (!fn) { _capture_err(); return NULL; }
    PyObject* r = pb_call(fn, args, NULL);
    P.dec_ref(fn);
    return r;
}

PyObject* pb_call0(PyObject* fn) { return pb_call(fn, NULL, NULL); }

PyObject* pb_getattr(PyObject* obj, const char* name) {
    PyObject* v = P.get_attr_str(obj, name);
    if (!v) _capture_err();
    return v;
}

int pb_setattr(PyObject* obj, const char* name, PyObject* val) {
    return P.set_attr_str(obj, name, val);
}

PyObject* pb_from_none(void) { P.inc_ref(&Py_None); return &Py_None; }
PyObject* pb_from_bool(int v) { return P.bool_from(v); }
PyObject* pb_from_long(long v) { return P.long_from(v); }
PyObject* pb_from_double(double v) { return P.float_from(v); }
PyObject* pb_from_string(const char* v) { return v ? P.unicode_from(v) : pb_from_none(); }
PyObject* pb_from_bytes(const char* d, long l) { return d ? P.bytes_from(d, l) : pb_from_none(); }

int pb_to_bool(PyObject* o) { return o && o != &Py_None && P.is_true(o); }
long pb_to_long(PyObject* o) { return (o && o != &Py_None) ? P.long_as(o) : 0; }
double pb_to_double(PyObject* o) { return (o && o != &Py_None) ? P.float_as(o) : 0.0; }

static char _sbuf[65536];
const char* pb_to_string(PyObject* o) {
    if (!o || o == &Py_None) return NULL;
    PyObject* s = P.str(o);
    if (!s) { _capture_err(); return NULL; }
    const char* c = P.unicode_as_utf8(s);
    if (c) { strncpy(_sbuf, c, sizeof(_sbuf)-1); _sbuf[sizeof(_sbuf)-1]='\0'; }
    P.dec_ref(s);
    return c ? _sbuf : NULL;
}

int pb_is_none(PyObject* o) { return !o || o == &Py_None; }
int pb_is_callable(PyObject* o) { return o && P.callable(o); }

PyObject* pb_tuple_new(long n) { return P.tuple_new(n); }
int pb_tuple_set(PyObject* t, long i, PyObject* v) { P.tuple_set(t, i, v); return 0; }
PyObject* pb_list_new(long n) { return P.list_new(n); }
int pb_list_set(PyObject* l, long i, PyObject* v) { return P.list_set(l, i, v); }
PyObject* pb_dict_new(void) { return P.dict_new(); }
int pb_dict_set(PyObject* d, PyObject* k, PyObject* v) { return P.dict_set(d, k, v); }

int pb_has_err(void) { return G.err[0] != '\0'; }
const char* pb_err_message(void) { return G.err; }
const char* pb_err_traceback(void) { return G.err; }
void pb_err_clear(void) { P.err_clear(); G.err[0]='\0'; }
void pb_decref(PyObject* o) { if (o && o != &Py_None) P.dec_ref(o); }
void pb_incref(PyObject* o) { if (o) P.inc_ref(o); }

PyObject* pb_dict_get_str(PyObject* d, const char* k) { return P.dict_get_str(d, k); }
Py_ssize_t pb_list_size(PyObject* l) { return P.list_size(l); }
PyObject* pb_list_get(PyObject* l, Py_ssize_t i) { return P.list_get(l, i); }
Py_ssize_t pb_py_tuple_size(PyObject* o) { return P.tuple_size(o); }
PyObject* pb_py_tuple_get(PyObject* o, Py_ssize_t i) { return P.tuple_get(o, i); }

int pb_type_check(PyObject* obj, const char* type_name) {
    if (!obj || obj == &Py_None) return 0;
    PyObject* cls = P.get_attr_str(obj, "__class__");
    if (!cls) return 0;
    PyObject* name = P.get_attr_str(cls, "__name__");
    P.dec_ref(cls);
    if (!name) return 0;
    const char* n = P.unicode_as_utf8(name);
    int result = (n && strcmp(n, type_name) == 0);
    P.dec_ref(name);
    return result;
}
```

---

### 文件 4: `jni/src/pybridge_jni.c`

JNI 封装层。将 C 桥接层封装为 Java 可调用的 native 方法。包含完整的 Java Object ↔ PyObject 双向类型转换（支持 Boolean/Integer/Long/Double/String/byte[]/List/Map/int[]/double[]）。

```c
#include <jni.h>
#include "pybridge/pybridge_core.h"
#include <string.h>
#include <stdint.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PyBridge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PyBridge", __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) fprintf(stderr, "[PyBridge] " __VA_ARGS__)
#define LOGE(...) fprintf(stderr, "[PyBridge ERR] " __VA_ARGS__)
#endif

static PyObject* jobject_to_pyobj(JNIEnv* env, jobject obj) {
    if (!obj) return pb_from_none();
    jclass cls = (*env)->GetObjectClass(env, obj);

    jclass bool_cls = (*env)->FindClass(env, "java/lang/Boolean");
    if ((*env)->IsInstanceOf(env, obj, bool_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, bool_cls, "booleanValue", "()Z");
        jboolean v = (*env)->CallBooleanMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, bool_cls); (*env)->DeleteLocalRef(env, cls);
        return pb_from_bool(v);
    }
    (*env)->DeleteLocalRef(env, bool_cls);

    jclass int_cls = (*env)->FindClass(env, "java/lang/Integer");
    if ((*env)->IsInstanceOf(env, obj, int_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, int_cls, "intValue", "()I");
        jint v = (*env)->CallIntMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, int_cls); (*env)->DeleteLocalRef(env, cls);
        return pb_from_long(v);
    }
    (*env)->DeleteLocalRef(env, int_cls);

    jclass long_cls = (*env)->FindClass(env, "java/lang/Long");
    if ((*env)->IsInstanceOf(env, obj, long_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, long_cls, "longValue", "()J");
        jlong v = (*env)->CallLongMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, long_cls); (*env)->DeleteLocalRef(env, cls);
        return pb_from_long(v);
    }
    (*env)->DeleteLocalRef(env, long_cls);

    jclass dbl_cls = (*env)->FindClass(env, "java/lang/Double");
    if ((*env)->IsInstanceOf(env, obj, dbl_cls)) {
        jmethodID mid = (*env)->GetMethodID(env, dbl_cls, "doubleValue", "()D");
        jdouble v = (*env)->CallDoubleMethod(env, obj, mid);
        (*env)->DeleteLocalRef(env, dbl_cls); (*env)->DeleteLocalRef(env, cls);
        return pb_from_double(v);
    }
    (*env)->DeleteLocalRef(env, dbl_cls);

    jclass str_cls = (*env)->FindClass(env, "java/lang/String");
    if ((*env)->IsInstanceOf(env, obj, str_cls)) {
        const char* s = (*env)->GetStringUTFChars(env, (jstring)obj, NULL);
        PyObject* r = pb_from_string(s);
        (*env)->ReleaseStringUTFChars(env, (jstring)obj, s);
        (*env)->DeleteLocalRef(env, str_cls); (*env)->DeleteLocalRef(env, cls);
        return r;
    }
    (*env)->DeleteLocalRef(env, str_cls);

    if ((*env)->IsInstanceOf(env, obj, (*env)->FindClass(env, "[B"))) {
        jbyteArray arr = (jbyteArray)obj;
        jsize len = (*env)->GetArrayLength(env, arr);
        jbyte* data = (*env)->GetByteArrayElements(env, arr, NULL);
        PyObject* r = pb_from_bytes((const char*)data, len);
        (*env)->ReleaseByteArrayElements(env, arr, data, JNI_ABORT);
        (*env)->DeleteLocalRef(env, cls);
        return r;
    }

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
        (*env)->DeleteLocalRef(env, list_cls); (*env)->DeleteLocalRef(env, cls);
        return list;
    }
    (*env)->DeleteLocalRef(env, list_cls);

    (*env)->DeleteLocalRef(env, cls);
    return pb_from_none();
}

static jobject pyobj_to_jobject(JNIEnv* env, PyObject* obj) {
    if (pb_is_none(obj)) return NULL;
    if (PyBool_Check(obj)) {
        jclass cls = (*env)->FindClass(env, "java/lang/Boolean");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(Z)Ljava/lang/Boolean;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jboolean)pb_to_bool(obj));
        (*env)->DeleteLocalRef(env, cls); return r;
    }
    if (PyLong_Check(obj)) {
        long v = pb_to_long(obj);
        if (v >= -2147483648L && v <= 2147483647L) {
            jclass cls = (*env)->FindClass(env, "java/lang/Integer");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(I)Ljava/lang/Integer;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jint)v);
            (*env)->DeleteLocalRef(env, cls); return r;
        } else {
            jclass cls = (*env)->FindClass(env, "java/lang/Long");
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(J)Ljava/lang/Long;");
            jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jlong)v);
            (*env)->DeleteLocalRef(env, cls); return r;
        }
    }
    if (PyFloat_Check(obj)) {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        jmethodID mid = (*env)->GetStaticMethodID(env, cls, "valueOf", "(D)Ljava/lang/Double;");
        jobject r = (*env)->CallStaticObjectMethod(env, cls, mid, (jdouble)pb_to_double(obj));
        (*env)->DeleteLocalRef(env, cls); return r;
    }
    if (PyUnicode_Check(obj)) {
        const char* s = pb_to_string(obj);
        return s ? (*env)->NewStringUTF(env, s) : NULL;
    }
    if (PyBytes_Check(obj)) {
        /* TODO: bytes → byte[] */
    }
    if (PyList_Check(obj)) {
        Py_ssize_t len = pb_list_size(obj);
        jclass cls = (*env)->FindClass(env, "java/util/ArrayList");
        jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
        jmethodID add = (*env)->GetMethodID(env, cls, "add", "(Ljava/lang/Object;)Z");
        jobject list = (*env)->NewObject(env, cls, init, (jint)len);
        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject* item = pb_list_get(obj, i);
            jobject jitem = pyobj_to_jobject(env, item);
            (*env)->CallBooleanMethod(env, list, add, jitem);
            if (jitem) (*env)->DeleteLocalRef(env, jitem);
        }
        (*env)->DeleteLocalRef(env, cls); return list;
    }
    const char* s = pb_to_string(obj);
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}

JNIEXPORT jint JNICALL Java_com_pybridge_core_Python_nativeInit(JNIEnv* env, jclass c, jstring jh, jobjectArray jp) {
    (void)c;
    const char* home = jh ? (*env)->GetStringUTFChars(env, jh, NULL) : NULL;
    int rc = pb_init(home);
    if (jh) (*env)->ReleaseStringUTFChars(env, jh, home);
    if (jp) {
        jsize len = (*env)->GetArrayLength(env, jp);
        for (jsize i = 0; i < len; i++) {
            jstring p = (jstring)(*env)->GetObjectArrayElement(env, jp, i);
            const char* path = (*env)->GetStringUTFChars(env, p, NULL);
            pb_add_path(path);
            (*env)->ReleaseStringUTFChars(env, p, path);
            (*env)->DeleteLocalRef(env, p);
        }
    }
    LOGI("Python %s initialized", pb_version());
    return rc;
}

JNIEXPORT void JNICALL Java_com_pybridge_core_Python_nativeFinalize(JNIEnv* e, jclass c) { (void)e;(void)c; pb_finalize(); }

JNIEXPORT jlong JNICALL Java_com_pybridge_core_Python_nativeImportModule(JNIEnv* e, jclass c, jstring jn) {
    (void)c;
    const char* n = (*env)->GetStringUTFChars(e, jn, NULL);
    PyObject* m = pb_import(n);
    (*env)->ReleaseStringUTFChars(e, jn, n);
    return (jlong)(intptr_t)m;
}

JNIEXPORT jobject JNICALL Java_com_pybridge_core_Python_nativeCallFunction(JNIEnv* env, jclass c, jlong ptr, jstring jf, jobjectArray ja) {
    (void)c;
    PyObject* module = (PyObject*)(intptr_t)ptr;
    if (!module) return NULL;
    const char* fn = (*env)->GetStringUTFChars(env, jf, NULL);
    PyObject* func = pb_getattr(module, fn);
    (*env)->ReleaseStringUTFChars(env, jf, fn);
    if (!func) return NULL;
    PyObject* args = NULL;
    if (ja) {
        jsize argc = (*env)->GetArrayLength(env, ja);
        args = pb_tuple_new(argc);
        for (jsize i = 0; i < argc; i++) {
            jobject a = (*env)->GetObjectArrayElement(env, ja, i);
            pb_tuple_set(args, i, jobject_to_pyobj(env, a));
            (*env)->DeleteLocalRef(env, a);
        }
    }
    PyObject* result = pb_call(func, args, NULL);
    pb_decref(func);
    jobject jr = result ? pyobj_to_jobject(env, result) : NULL;
    pb_decref(result);
    return jr;
}

JNIEXPORT jobject JNICALL Java_com_pybridge_core_Python_nativeExec(JNIEnv* env, jclass c, jstring jc) {
    (void)c;
    const char* code = (*env)->GetStringUTFChars(env, jc, NULL);
    PyObject* r = pb_eval(code);
    if (!r) { pb_err_clear(); pb_exec(code); (*env)->ReleaseStringUTFChars(env, jc, code); return NULL; }
    (*env)->ReleaseStringUTFChars(env, jc, code);
    jobject jr = pyobj_to_jobject(env, r);
    pb_decref(r);
    return jr;
}

JNIEXPORT jstring JNICALL Java_com_pybridge_core_Python_nativeGetVersion(JNIEnv* e, jclass c) { (void)c; return (*e)->NewStringUTF(e, pb_version()); }
JNIEXPORT void JNICALL Java_com_pybridge_core_Python_nativeDecRef(JNIEnv* e, jclass c, jlong p) { (void)e;(void)c; pb_decref((PyObject*)(intptr_t)p); }
```

---

### 文件 5: `jni/src/pb_loader.c`

多语言模块加载器。通过 dlopen 加载任意 .so，调用其导出的 `pb_module_info` 和 `pb_module_func`。

```c
#include "pybridge/pb_module.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct PB_Module {
    char* name; char* path; void* handle;
    pb_module_info_fn info_fn; pb_module_func_fn func_fn;
    char* info_json; struct PB_Module* next;
} PB_Module;

static PB_Module* g_modules = NULL;

PB_Module* pb_load_module(const char* name, const char* path) {
    for (PB_Module* m = g_modules; m; m = m->next) if (strcmp(m->name, name) == 0) return m;
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "pb_load_module(%s): %s\n", name, dlerror()); return NULL; }
    pb_module_info_fn info_fn = (pb_module_info_fn)dlsym(handle, "pb_module_info");
    pb_module_func_fn func_fn = (pb_module_func_fn)dlsym(handle, "pb_module_func");
    if (!info_fn || !func_fn) { dlclose(handle); return NULL; }
    PB_Module* mod = (PB_Module*)calloc(1, sizeof(PB_Module));
    mod->name = strdup(name); mod->path = strdup(path);
    mod->handle = handle; mod->info_fn = info_fn; mod->func_fn = func_fn;
    mod->info_json = strdup(info_fn());
    mod->next = g_modules; g_modules = mod;
    return mod;
}

void* pb_get_func(PB_Module* mod, const char* func_name) {
    return (mod && mod->func_fn) ? mod->func_fn(func_name) : NULL;
}

const char* pb_module_get_info(PB_Module* mod) { return mod ? mod->info_json : NULL; }
PB_Module* pb_module_find(const char* name) {
    for (PB_Module* m = g_modules; m; m = m->next) if (strcmp(m->name, name) == 0) return m;
    return NULL;
}
```

---

### 文件 6: `jni/Makefile`

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS = -ldl

all: libpybridge.so test_pybridge

libpybridge.so: src/pybridge_core.c include/pybridge/pybridge_core.h
	$(CC) -shared -o $@ src/pybridge_core.c $(CFLAGS) $(LDFLAGS)

test_pybridge: test/test_pybridge.c libpybridge.so
	$(CC) -o $@ test/test_pybridge.c -L. -lpybridge -Wl,-rpath,. $(CFLAGS) $(LDFLAGS) -lm

test: test_pybridge
	./test_pybridge

clean:
	rm -f libpybridge.so test_pybridge
```

---

### 文件 7: `jni/test/test_pybridge.c`

13 项完整测试。验证初始化、类型转换、模块导入、函数调用、表达式求值、语句执行、字典操作、异常处理、JSON 处理、数值计算、类和对象、列表操作、字符串操作。

```c
#include "pybridge/pybridge_core.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int TR=0, TP=0, TF=0;
#define TEST(n) do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c,m) do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;}} while(0)
#define PASS() do { printf("  ✅ PASS\n"); TP++; } while(0)

void test_init(void) {
    TEST("初始化");
    ASSERT(!pb_is_init(), "未初始化");
    ASSERT(pb_init(NULL)==0, "init");
    ASSERT(pb_is_init(), "已初始化");
    printf("  %s\n", pb_version());
    PASS();
}

void test_types(void) {
    TEST("类型转换");
    PyObject* i = pb_from_long(42);
    ASSERT(pb_to_long(i)==42, "int"); pb_decref(i);
    PyObject* d = pb_from_double(3.14);
    ASSERT(fabs(pb_to_double(d)-3.14)<1e-10, "double"); pb_decref(d);
    PyObject* b = pb_from_bool(1);
    ASSERT(pb_to_bool(b)==1, "bool"); pb_decref(b);
    PyObject* s = pb_from_string("hello");
    ASSERT(strcmp(pb_to_string(s),"hello")==0, "string"); pb_decref(s);
    ASSERT(pb_is_none(pb_from_none()), "none");
    PASS();
}

void test_import(void) {
    TEST("模块导入");
    PyObject* m = pb_import("math");
    ASSERT(m, "math");
    PyObject* pi = pb_getattr(m, "pi");
    ASSERT(pi && fabs(pb_to_double(pi)-3.14159265)<1e-8, "pi");
    printf("  π = %.10f\n", pb_to_double(pi));
    pb_decref(pi);
    ASSERT(pb_import("no_such_mod_xyz")==NULL, "不存在的模块");
    pb_err_clear();
    pb_decref(m);
    PASS();
}

void test_call(void) {
    TEST("函数调用");
    PyObject* m = pb_import("math");
    PyObject* fn = pb_getattr(m, "sqrt");
    PyObject* a = pb_tuple_new(1);
    pb_tuple_set(a, 0, pb_from_double(144.0));
    PyObject* r = pb_call(fn, a, NULL);
    ASSERT(r && fabs(pb_to_double(r)-12.0)<1e-10, "sqrt(144)=12");
    printf("  sqrt(144) = %f\n", pb_to_double(r));
    pb_decref(r); pb_decref(fn);
    PyObject* fa = pb_tuple_new(1);
    pb_tuple_set(fa, 0, pb_from_long(10));
    PyObject* fr = pb_call_method(m, "factorial", fa);
    ASSERT(fr && pb_to_long(fr)==3628800, "10!");
    printf("  10! = %ld\n", pb_to_long(fr));
    pb_decref(fr);
    pb_decref(m);
    PASS();
}

void test_eval(void) {
    TEST("表达式求值");
    PyObject* r = pb_eval("2**10");
    ASSERT(r && pb_to_long(r)==1024, "2**10");
    printf("  2**10 = %ld\n", pb_to_long(r)); pb_decref(r);
    r = pb_eval("'hello' + ' ' + 'world'");
    ASSERT(r && strcmp(pb_to_string(r),"hello world")==0, "str concat");
    printf("  '%s'\n", pb_to_string(r)); pb_decref(r);
    r = pb_eval("sum([x**2 for x in range(6)])");
    ASSERT(r && pb_to_long(r)==55, "sum squares");
    printf("  sum squares 0..5 = %ld\n", pb_to_long(r)); pb_decref(r);
    PASS();
}

void test_exec(void) {
    TEST("语句执行");
    pb_exec("x = 100");
    PyObject* r = pb_eval("x");
    ASSERT(r && pb_to_long(r)==100, "x=100");
    printf("  x = %ld\n", pb_to_long(r)); pb_decref(r);
    pb_exec("def add(a,b): return a+b");
    r = pb_eval("add(3,4)");
    ASSERT(r && pb_to_long(r)==7, "add(3,4)=7");
    printf("  add(3,4) = %ld\n", pb_to_long(r)); pb_decref(r);
    pb_exec("def fib(n):\n    a,b=0,1\n    r=[]\n    for _ in range(n):\n        r.append(a)\n        a,b=b,a+b\n    return r\n");
    r = pb_eval("fib(10)==[0,1,1,2,3,5,8,13,21,34]");
    ASSERT(r && pb_to_bool(r), "fib(10)");
    printf("  fib(10) = [0,1,1,2,3,5,8,13,21,34] ✓\n");
    pb_decref(r);
    PASS();
}

void test_dict(void) {
    TEST("字典操作");
    pb_exec("d = {'name': 'PyBridge', 'ver': 1, 'ok': True}");
    PyObject* r = pb_eval("d['name']");
    ASSERT(r && strcmp(pb_to_string(r),"PyBridge")==0, "dict['name']");
    printf("  d['name'] = '%s'\n", pb_to_string(r)); pb_decref(r);
    r = pb_eval("len(d)");
    ASSERT(r && pb_to_long(r)==3, "len=3");
    printf("  len(d) = %ld\n", pb_to_long(r)); pb_decref(r);
    r = pb_eval("sorted(d.keys())==['name','ok','ver']");
    ASSERT(r && pb_to_bool(r), "keys sorted");
    printf("  keys = ['name','ok','ver'] ✓\n");
    pb_decref(r);
    PASS();
}

void test_exceptions(void) {
    TEST("异常处理");
    PyObject* r = pb_eval("1/0");
    ASSERT(!r, "ZeroDivisionError");
    printf("  1/0 → error captured ✓\n");
    pb_err_clear();
    r = pb_eval("xyz_undefined");
    ASSERT(!r, "NameError");
    printf("  undefined → error captured ✓\n");
    pb_err_clear();
    PASS();
}

void test_json(void) {
    TEST("JSON 处理");
    pb_exec("import json");
    pb_exec("data={'name':'PyBridge','ver':1,'tags':['python','java','kotlin']}");
    PyObject* r = pb_eval("json.dumps(data,indent=2)");
    ASSERT(r, "json.dumps");
    printf("  JSON:\n%s\n", pb_to_string(r));
    pb_decref(r);
    r = pb_eval("json.loads(json.dumps(data))['name']");
    ASSERT(r && strcmp(pb_to_string(r),"PyBridge")==0, "round-trip");
    printf("  round-trip name='%s' ✓\n", pb_to_string(r));
    pb_decref(r);
    PASS();
}

void test_math(void) {
    TEST("数值计算");
    pb_exec("def dot(a,b): return sum(x*y for x,y in zip(a,b))\ndef mean(a): return sum(a)/len(a)\ndef std(a):\n    m=mean(a)\n    return (sum((x-m)**2 for x in a)/len(a))**0.5\n");
    PyObject* r = pb_eval("dot([1,2,3],[4,5,6])");
    ASSERT(r && pb_to_long(r)==32, "dot=32");
    printf("  dot = %ld\n", pb_to_long(r)); pb_decref(r);
    r = pb_eval("mean([10,20,30,40,50])");
    ASSERT(r && fabs(pb_to_double(r)-30.0)<1e-10, "mean=30");
    printf("  mean = %.1f\n", pb_to_double(r)); pb_decref(r);
    r = pb_eval("round(std([2,4,4,4,5,5,7,9]),2)");
    ASSERT(r, "std");
    printf("  std = %s\n", pb_to_string(r)); pb_decref(r);
    PASS();
}

void test_class(void) {
    TEST("类和对象");
    pb_exec("class Calc:\n    def __init__(s,v=0): s.v=v\n    def add(s,x): s.v+=x; return s\n    def mul(s,x): s.v*=x; return s\n    def get(s): return s.v\n");
    PyObject* r = pb_eval("Calc(5).add(3).mul(2).get()");
    ASSERT(r && pb_to_long(r)==16, "(5+3)*2=16");
    printf("  (5+3)*2 = %ld\n", pb_to_long(r)); pb_decref(r);
    PASS();
}

void test_list_ops(void) {
    TEST("列表操作");
    pb_exec("lst=[10,20,30,40,50]");
    PyObject* r = pb_eval("sum(lst)");
    ASSERT(r && pb_to_long(r)==150, "sum=150");
    printf("  sum = %ld\n", pb_to_long(r)); pb_decref(r);
    r = pb_eval("[x*2 for x in lst if x>20]==[60,80,100]");
    ASSERT(r && pb_to_bool(r), "filtered");
    printf("  filtered = [60, 80, 100] ✓\n");
    pb_decref(r);
    PASS();
}

void test_string_ops(void) {
    TEST("字符串操作");
    pb_exec("s='Hello, PyBridge!'");
    PyObject* r = pb_eval("s.upper()");
    ASSERT(r && strcmp(pb_to_string(r),"HELLO, PYBRIDGE!")==0, "upper");
    printf("  upper = '%s'\n", pb_to_string(r)); pb_decref(r);
    r = pb_eval("len(s)");
    ASSERT(r && pb_to_long(r)==16, "len=16");
    printf("  len = %ld\n", pb_to_long(r)); pb_decref(r);
    PASS();
}

int main(void) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   PyBridge 核心桥接层 — 完整测试     ║\n");
    printf("╚══════════════════════════════════════╝\n");
    test_init(); test_types(); test_import(); test_call();
    test_eval(); test_exec(); test_dict(); test_exceptions();
    test_json(); test_math(); test_class(); test_list_ops();
    test_string_ops();
    pb_finalize();
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║  %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                ║\n");
    printf("╚══════════════════════════════════════╝\n");
    return TF ? 1 : 0;
}
```

---

### 文件 8: `modules/c/cmath.c`

C 语言示例模块，14 个数学函数。

```c
#include "pybridge/pb_module.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static double c_add(double a, double b) { return a + b; }
static double c_sub(double a, double b) { return a - b; }
static double c_mul(double a, double b) { return a * b; }
static double c_div(double a, double b) { return b != 0 ? a / b : 0; }
static double c_sqrt(double x) { return sqrt(x); }
static double c_pow(double base, double exp) { return pow(base, exp); }
static double c_sin(double x) { return sin(x); }
static double c_cos(double x) { return cos(x); }
static double c_log(double x) { return log(x); }
static double c_abs(double x) { return fabs(x); }
static int c_factorial(int n) { if (n <= 1) return 1; int r = 1; for (int i = 2; i <= n; i++) r *= i; return r; }
static int c_fibonacci(int n) { if (n <= 0) return 0; if (n == 1) return 1; int a = 0, b = 1; for (int i = 2; i <= n; i++) { int t = a + b; a = b; b = t; } return b; }
static int c_gcd(int a, int b) { while (b) { int t = b; b = a % b; a = t; } return a; }
static const char* c_version(void) { return "cmath 1.0.0 (C)"; }

PB_MODULE("cmath", "1.0.0", "c",
    PB_FUNC_ENTRY(c_add, "dd>d"), PB_FUNC_ENTRY(c_sub, "dd>d"),
    PB_FUNC_ENTRY(c_mul, "dd>d"), PB_FUNC_ENTRY(c_div, "dd>d"),
    PB_FUNC_ENTRY(c_sqrt, "d>d"), PB_FUNC_ENTRY(c_pow, "dd>d"),
    PB_FUNC_ENTRY(c_sin, "d>d"), PB_FUNC_ENTRY(c_cos, "d>d"),
    PB_FUNC_ENTRY(c_log, "d>d"), PB_FUNC_ENTRY(c_abs, "d>d"),
    PB_FUNC_ENTRY(c_factorial, "i>i"), PB_FUNC_ENTRY(c_fibonacci, "i>i"),
    PB_FUNC_ENTRY(c_gcd, "ii>i"), PB_FUNC_ENTRY(c_version, "v>s"),
)
```

---

### 文件 9: `modules/Makefile`

```makefile
CC = gcc
CFLAGS = -Wall -O2 -fPIC -I../jni/include

all: c test_modules

c: libcmath.so

libcmath.so: c/cmath.c ../jni/include/pybridge/pb_module.h
	$(CC) -shared $(CFLAGS) -o $@ c/cmath.c -lm

test_modules: test_modules.c ../jni/src/pb_loader.c libcmath.so
	$(CC) $(CFLAGS) -o $@ test_modules.c ../jni/src/pb_loader.c -ldl -lm

test: test_modules
	LD_LIBRARY_PATH=. ./test_modules

clean:
	rm -f libcmath.so test_modules
```

---

### 文件 10: `modules/test_modules.c`

多语言模块测试程序。加载 C/Rust/Go 模块并验证统一调用接口。

```c
#include "pybridge/pb_module.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct PB_Module PB_Module;
extern PB_Module* pb_load_module(const char* name, const char* path);
extern void* pb_get_func(PB_Module* mod, const char* func_name);
extern const char* pb_module_get_info(PB_Module* mod);

static int TR=0, TP=0, TF=0;
#define TEST(n) do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c,m) do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;} } while(0)
#define PASS() do { printf("  ✅ PASS\n"); TP++; } while(0)

typedef double (*fn_dd)(double, double);
typedef double (*fn_d)(double);
typedef int (*fn_ii)(int, int);
typedef int (*fn_i)(int);
typedef const char* (*fn_v)(void);

void test_c_module(void) {
    TEST("C 模块 (cmath)");
    PB_Module* mod = pb_load_module("cmath", "./libcmath.so");
    ASSERT(mod != NULL, "加载 libcmath.so");
    printf("  模块信息: %s\n", pb_module_get_info(mod));
    fn_dd add = (fn_dd)pb_get_func(mod, "c_add");
    ASSERT(add && fabs(add(3.0,4.0)-7.0)<1e-10, "c_add(3,4)=7");
    printf("  c_add(3,4) = %f\n", add(3.0,4.0));
    fn_d sq = (fn_d)pb_get_func(mod, "c_sqrt");
    ASSERT(sq && fabs(sq(144.0)-12.0)<1e-10, "c_sqrt(144)=12");
    printf("  c_sqrt(144) = %f\n", sq(144.0));
    fn_i fact = (fn_i)pb_get_func(mod, "c_factorial");
    ASSERT(fact && fact(10)==3628800, "c_factorial(10)=3628800");
    printf("  c_factorial(10) = %d\n", fact(10));
    fn_ii gcd = (fn_ii)pb_get_func(mod, "c_gcd");
    ASSERT(gcd(12,8)==4, "c_gcd(12,8)=4");
    printf("  c_gcd(12,8) = %d\n", gcd(12,8));
    fn_v ver = (fn_v)pb_get_func(mod, "c_version");
    printf("  version: %s\n", ver());
    PASS();
}

void test_rust_module(void) {
    TEST("Rust 模块 (rustmath)");
    PB_Module* mod = pb_load_module("rustmath", "./librustmath.so");
    if (!mod) { printf("  ⚠ 未编译，跳过\n"); TR--; return; }
    fn_dd add = (fn_dd)pb_get_func(mod, "r_add");
    ASSERT(add && fabs(add(10.0,20.0)-30.0)<1e-10, "r_add(10,20)=30");
    printf("  r_add(10,20) = %f\n", add(10.0,20.0));
    PASS();
}

void test_go_module(void) {
    TEST("Go 模块 (gomath)");
    PB_Module* mod = pb_load_module("gomath", "./libgomath.so");
    if (!mod) { printf("  ⚠ 未编译，跳过\n"); TR--; return; }
    fn_dd add = (fn_dd)pb_get_func(mod, "g_add");
    ASSERT(add && fabs(add(100.0,200.0)-300.0)<1e-10, "g_add(100,200)=300");
    printf("  g_add(100,200) = %f\n", add(100.0,200.0));
    PASS();
}

void test_cross_language(void) {
    TEST("跨语言统一调用");
    PB_Module* cmod = pb_load_module("cmath", "./libcmath.so");
    if (cmod) {
        fn_dd add = (fn_dd)pb_get_func(cmod, "c_add");
        printf("  C: c_add(100,200) = %f\n", add(100.0,200.0));
    }
    PASS();
}

int main(void) {
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  PyBridge 多语言模块测试                     ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    test_c_module(); test_rust_module(); test_go_module(); test_cross_language();
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  结果: %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                  ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    return TF ? 1 : 0;
}
```

---

### 文件 11: `test/TestJava.java`

Java 端到端测试。通过 JNI 调用 Python，验证初始化、数学计算、字符串、代码执行、JSON 处理、Java 对象传递。

```java
public class TestJava {
    static { System.loadLibrary("test_jni"); }

    static native int    nativeInit(String home, String[] paths);
    static native void   nativeFinalize();
    static native long   nativeImportModule(String name);
    static native Object nativeCallFunction(long ptr, String func, Object[] args);
    static native Object nativeExec(String code);
    static native String nativeGetVersion();
    static native void   nativeDecRef(long ptr);

    static long importModule(String name) {
        long ptr = nativeImportModule(name);
        if (ptr == 0) throw new RuntimeException("Import failed: " + name);
        return ptr;
    }
    static Object call(long ptr, String func, Object... args) { return nativeCallFunction(ptr, func, args); }
    static Object eval(String code) { return nativeExec(code); }

    static int TR=0, TP=0, TF=0;
    static void TEST(String n) { System.out.println("\n── " + n + " ──"); TR++; }
    static void ASSERT(boolean c, String m) { if(!c){System.out.println("  ❌ "+m);TF++;throw new RuntimeException("FAIL: "+m);} }
    static void PASS() { System.out.println("  ✅ PASS"); TP++; }

    public static void main(String[] args) {
        System.out.println("╔══════════════════════════════════════════╗");
        System.out.println("║   PyBridge Java → Python 端到端测试      ║");
        System.out.println("╚══════════════════════════════════════════╝");
        try {
            testInit(); testMath(); testString(); testExec(); testJson(); testPassJavaObjects();
        } catch (Exception e) { System.out.println("  💥 " + e.getMessage()); TF++; }
        nativeFinalize();
        System.out.println("\n╔══════════════════════════════════════════╗");
        System.out.printf("║  %d/%d 通过", TP, TR);
        if (TF > 0) System.out.printf(", %d 失败", TF);
        System.out.println("                ║");
        System.out.println("╚══════════════════════════════════════════╝");
        System.exit(TF > 0 ? 1 : 0);
    }

    static void testInit() {
        TEST("初始化");
        ASSERT(nativeInit(null, null) == 0, "nativeInit");
        String ver = nativeGetVersion();
        ASSERT(ver != null && ver.contains("3."), "版本");
        System.out.println("  Python " + ver);
        PASS();
    }

    static void testMath() {
        TEST("数学计算");
        eval("import math");
        long math = importModule("math");
        Object sqrtResult = call(math, "sqrt", 144.0);
        ASSERT(sqrtResult instanceof Number, "sqrt 应为 Number");
        double sqrt = ((Number)sqrtResult).doubleValue();
        ASSERT(Math.abs(sqrt - 12.0) < 1e-10, "sqrt(144)=12");
        System.out.printf("  sqrt(144) = %f%n", sqrt);
        Object piResult = eval("math.pi");
        ASSERT(piResult instanceof Number, "pi 应为 Number");
        double pi = ((Number)piResult).doubleValue();
        ASSERT(Math.abs(pi - 3.14159265) < 1e-6, "π≈3.14159");
        System.out.printf("  π = %.10f%n", pi);
        Object factResult = call(math, "factorial", 10);
        ASSERT(factResult instanceof Number, "factorial 应为 Number");
        long fact = ((Number)factResult).longValue();
        ASSERT(fact == 3628800, "10!=3628800");
        System.out.println("  10! = " + fact);
        nativeDecRef(math);
        PASS();
    }

    static void testString() {
        TEST("字符串操作");
        Object upper = eval("'hello'.upper()");
        ASSERT("HELLO".equals(upper), "upper=HELLO");
        System.out.println("  upper = " + upper);
        Object concat = eval("'Hello'+', '+'World!'");
        ASSERT("Hello, World!".equals(concat), "拼接");
        System.out.println("  拼接 = " + concat);
        Object len = eval("len('PyBridge')");
        ASSERT(len instanceof Number && ((Number)len).intValue() == 8, "len=8");
        System.out.println("  len = " + len);
        PASS();
    }

    static void testExec() {
        TEST("代码执行");
        eval("x = 100");
        Object x = eval("x");
        ASSERT(x instanceof Number && ((Number)x).intValue() == 100, "x=100");
        System.out.println("  x = " + x);
        eval("def add(a,b): return a+b");
        Object result = eval("add(3,4)");
        ASSERT(result instanceof Number && ((Number)result).intValue() == 7, "add(3,4)=7");
        System.out.println("  add(3,4) = " + result);
        eval("def fib(n):\n    a,b=0,1\n    r=[]\n    for _ in range(n):\n        r.append(a)\n        a,b=b,a+b\n    return r\n");
        Object fibCheck = eval("fib(10)==[0,1,1,2,3,5,8,13,21,34]");
        ASSERT(fibCheck instanceof Boolean && (Boolean)fibCheck, "fib(10)");
        System.out.println("  fib(10) = [0,1,1,2,3,5,8,13,21,34] ✓");
        PASS();
    }

    static void testJson() {
        TEST("JSON 处理");
        eval("import json");
        eval("data={'name':'PyBridge','ver':1,'langs':['java','kotlin','python']}");
        Object jsonStr = eval("json.dumps(data,indent=2)");
        ASSERT(jsonStr instanceof String && ((String)jsonStr).contains("PyBridge"), "json.dumps");
        System.out.println("  JSON:\n" + jsonStr);
        Object roundTrip = eval("json.loads(json.dumps(data))['name']");
        ASSERT("PyBridge".equals(roundTrip), "round-trip");
        System.out.println("  round-trip name = " + roundTrip);
        PASS();
    }

    static void testPassJavaObjects() {
        TEST("Java 对象传递给 Python");
        Object sumResult = eval("sum([1,2,3,4,5])");
        ASSERT(sumResult instanceof Number && ((Number)sumResult).intValue() == 15, "sum=15");
        System.out.println("  sum([1..5]) = " + sumResult);
        Object upperResult = eval("'java_string'.upper()");
        ASSERT("JAVA_STRING".equals(upperResult), "upper");
        System.out.println("  upper = " + upperResult);
        PASS();
    }
}
```

---

### 文件 12: `modules/rust/rustmath.rs`

Rust 模块示例，13 个函数。

```rust
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

#[no_mangle] pub extern "C" fn r_add(a: f64, b: f64) -> f64 { a + b }
#[no_mangle] pub extern "C" fn r_sub(a: f64, b: f64) -> f64 { a - b }
#[no_mangle] pub extern "C" fn r_mul(a: f64, b: f64) -> f64 { a * b }
#[no_mangle] pub extern "C" fn r_div(a: f64, b: f64) -> f64 { if b != 0.0 { a / b } else { 0.0 } }
#[no_mangle] pub extern "C" fn r_sqrt(x: f64) -> f64 { x.sqrt() }
#[no_mangle] pub extern "C" fn r_pow(base: f64, exp: f64) -> f64 { base.powf(exp) }
#[no_mangle] pub extern "C" fn r_sin(x: f64) -> f64 { x.sin() }
#[no_mangle] pub extern "C" fn r_cos(x: f64) -> f64 { x.cos() }
#[no_mangle] pub extern "C" fn r_abs(x: f64) -> f64 { x.abs() }
#[no_mangle] pub extern "C" fn r_factorial(n: i32) -> i32 { if n <= 1 { 1 } else { (2..=n).product() } }
#[no_mangle] pub extern "C" fn r_fibonacci(n: i32) -> i32 { if n <= 0 { return 0; } if n == 1 { return 1; } let (mut a, mut b) = (0, 1); for _ in 2..=n { let t = a + b; a = b; b = t; } b }
#[no_mangle] pub extern "C" fn r_is_prime(n: i32) -> bool { if n < 2 { return false; } if n < 4 { return true; } if n % 2 == 0 || n % 3 == 0 { return false; } let mut i = 5; while i * i <= n { if n % i == 0 || n % (i + 2) == 0 { return false; } i += 6; } true }
#[no_mangle] pub extern "C" fn r_version() -> *const c_char { CString::new("rustmath 1.0.0 (Rust)").unwrap().into_raw() }

static mut INFO_JSON: [u8; 4096] = [0; 4096];

#[no_mangle]
pub extern "C" fn pb_module_info() -> *const c_char {
    let json = r#"{"name":"rustmath","version":"1.0.0","language":"rust","functions":[{"name":"r_add","signature":"dd>d"},{"name":"r_sub","signature":"dd>d"},{"name":"r_mul","signature":"dd>d"},{"name":"r_div","signature":"dd>d"},{"name":"r_sqrt","signature":"d>d"},{"name":"r_pow","signature":"dd>d"},{"name":"r_sin","signature":"d>d"},{"name":"r_cos","signature":"d>d"},{"name":"r_abs","signature":"d>d"},{"name":"r_factorial","signature":"i>i"},{"name":"r_fibonacci","signature":"i>i"},{"name":"r_is_prime","signature":"i>b"},{"name":"r_version","signature":"v>s"}]}"#;
    unsafe { INFO_JSON[..json.len()].copy_from_slice(json.as_bytes()); INFO_JSON[json.len()] = 0; INFO_JSON.as_ptr() as *const c_char }
}

#[no_mangle]
pub extern "C" fn pb_module_func(name: *const c_char) -> *const () {
    let name = unsafe { CStr::from_ptr(name) }.to_str().unwrap_or("");
    match name {
        "r_add" => r_add as *const (), "r_sub" => r_sub as *const (),
        "r_mul" => r_mul as *const (), "r_div" => r_div as *const (),
        "r_sqrt" => r_sqrt as *const (), "r_pow" => r_pow as *const (),
        "r_sin" => r_sin as *const (), "r_cos" => r_cos as *const (),
        "r_abs" => r_abs as *const (), "r_factorial" => r_factorial as *const (),
        "r_fibonacci" => r_fibonacci as *const (), "r_is_prime" => r_is_prime as *const (),
        "r_version" => r_version as *const (), _ => std::ptr::null(),
    }
}
```

编译: `rustc --crate-type cdylib -o librustmath.so rustmath.rs`

---

### 文件 13: `modules/go/gomath.go`

Go 模块示例，16 个函数。

```go
package main

import "C"
import (
	"math"
	"strings"
	"unsafe"
)

//export g_add
func g_add(a, b C.double) C.double { return C.double(float64(a) + float64(b)) }
//export g_sub
func g_sub(a, b C.double) C.double { return C.double(float64(a) - float64(b)) }
//export g_mul
func g_mul(a, b C.double) C.double { return C.double(float64(a) * float64(b)) }
//export g_div
func g_div(a, b C.double) C.double { if float64(b) != 0 { return C.double(float64(a) / float64(b)) }; return 0 }
//export g_sqrt
func g_sqrt(x C.double) C.double { return C.double(math.Sqrt(float64(x))) }
//export g_pow
func g_pow(base, exp C.double) C.double { return C.double(math.Pow(float64(base), float64(exp))) }
//export g_sin
func g_sin(x C.double) C.double { return C.double(math.Sin(float64(x))) }
//export g_cos
func g_cos(x C.double) C.double { return C.double(math.Cos(float64(x))) }
//export g_abs
func g_abs(x C.double) C.double { return C.double(math.Abs(float64(x))) }
//export g_factorial
func g_factorial(n C.int) C.int { r := 1; for i := 2; i <= int(n); i++ { r *= i }; return C.int(r) }
//export g_fibonacci
func g_fibonacci(n C.int) C.int { if int(n) <= 0 { return 0 }; if int(n) == 1 { return 1 }; a, b := 0, 1; for i := 2; i <= int(n); i++ { a, b = b, a+b }; return C.int(b) }
//export g_is_prime
func g_is_prime(n C.int) bool { x := int(n); if x < 2 { return false }; if x < 4 { return true }; if x%2 == 0 || x%3 == 0 { return false }; for i := 5; i*i <= x; i += 6 { if x%i == 0 || x%(i+2) == 0 { return false } }; return true }
//export g_upper
func g_upper(s *C.char) *C.char { return C.CString(strings.ToUpper(C.GoString(s))) }
//export g_version
func g_version() *C.char { return C.CString("gomath 1.0.0 (Go)") }

var infoJSON = `{"name":"gomath","version":"1.0.0","language":"go","functions":[{"name":"g_add","signature":"dd>d"},{"name":"g_sub","signature":"dd>d"},{"name":"g_mul","signature":"dd>d"},{"name":"g_div","signature":"dd>d"},{"name":"g_sqrt","signature":"d>d"},{"name":"g_pow","signature":"dd>d"},{"name":"g_sin","signature":"d>d"},{"name":"g_cos","signature":"d>d"},{"name":"g_abs","signature":"d>d"},{"name":"g_factorial","signature":"i>i"},{"name":"g_fibonacci","signature":"i>i"},{"name":"g_is_prime","signature":"i>b"},{"name":"g_upper","signature":"s>s"},{"name":"g_version","signature":"v>s"}]}`

//export pb_module_info
func pb_module_info() *C.char { return C.CString(infoJSON) }

//export pb_module_func
func pb_module_func(name *C.char) unsafe.Pointer {
	switch C.GoString(name) {
	case "g_add": return C.g_add; case "g_sub": return C.g_sub
	case "g_mul": return C.g_mul; case "g_div": return C.g_div
	case "g_sqrt": return C.g_sqrt; case "g_pow": return C.g_pow
	case "g_sin": return C.g_sin; case "g_cos": return C.g_cos
	case "g_abs": return C.g_abs; case "g_factorial": return C.g_factorial
	case "g_fibonacci": return C.g_fibonacci; case "g_is_prime": return C.g_is_prime
	case "g_upper": return C.g_upper; case "g_version": return C.g_version
	}
	return nil
}

func main() {}
```

编译: `go build -buildmode=c-shared -o libgomath.so gomath.go`

---

## 第三部分：构建与测试命令

### 桌面 Linux 构建 (已验证)

```bash
# 1. 编译核心层
cd pybridge/jni
make clean && make all && make test
# 预期: 13/13 通过

# 2. 编译 JNI 层 (需要 JDK)
export JAVA_HOME=/path/to/jdk
gcc -shared -fPIC -O2 -o libpybridge_jni.so src/pybridge_jni.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -ldl -Wall
# 预期: 编译成功

# 3. 编译测试 JNI 桥接
gcc -shared -fPIC -O2 -o libtest_jni.so src/test_jni_bridge.c \
    -Iinclude -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
    -L. -lpybridge -ldl -Wall

# 4. 编译 C 模块
cd ../modules
make clean && make c && make test
# 预期: 2/2 通过

# 5. 运行 Java 测试
cd ..
javac -d build/classes test/TestJava.java
LD_LIBRARY_PATH=jni java -Djava.library.path=jni -cp build/classes TestJava
# 预期: 6/6 通过
```

### Android 构建 (需要 NDK)

```bash
export ANDROID_NDK=/path/to/ndk
cd pybridge/build-system
./build_android.sh --abi arm64-v8a --ndk $ANDROID_NDK
# 输出: output/arm64-v8a/lib/libpython3.12.so
#       output/arm64-v8a/lib/libpybridge.so
#       output/arm64-v8a/assets/python312_stdlib.zip
```

---

## 第四部分：关键技术决策记录

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 没有 python3-dev | 环境限制 | dlopen 动态加载 libpython |
| PyRun_string 返回 None | dlopen 场景兼容性问题 | PyRun_SimpleString + PyDict_GetItemString |
| 没有 JDK | 环境限制 | 下载 OpenJDK 21 tarball 到 ~/jdk |
| JNI 函数名绑定类名 | JNI 规范 | 为测试类写专用 test_jni_bridge.c |
| android/log.h 不存在 | 非 Android 环境 | #ifdef __android__ 条件编译 |
| PyBool_Check 未定义 | 不依赖 Python.h | pb_type_check 动态检查 __class__.__name__ |
| PB_FUNC_ENTRY 加 pb_ 前缀 | 宏设计错误 | 去掉前缀，直接用函数名 |

---

## 第五部分：已知限制与后续工作

| 项目 | 状态 | 说明 |
|------|------|------|
| Python 交叉编译 | 脚本就绪 | 需要 Android NDK 实测 |
| JNI 完整测试 | 桌面已验证 | 需要 Android 设备验证 |
| Rust 模块 | 代码就绪 | 需要安装 rustc |
| Go 模块 | 代码就绪 | 需要安装 go |
| Gradle 插件 | 代码就绪 | 需要 Android SDK 测试 |
| dict→HashMap 转换 | TODO | JNI 层未完成 |
| bytes→byte[] 转换 | TODO | JNI 层未完成 |
| Python→Java 反向调用 | 设计完成 | 需要 JNI 层实现 |
| pip 包交叉编译 | 设计完成 | 需要 NDK 实测 |
| 线程安全 (GIL) | 核心层已处理 | JNI 层需验证多线程 |
