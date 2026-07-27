/**
 * PyBridge Core — 生产可用版
 *
 * 使用 PyRun_SimpleString + Dict Get/Set 实现代码执行，
 * 绕过 dlopen 场景下 PyRun_String 的兼容性问题。
 *
 * 编译: make
 * 测试: make test
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct _object PyObject;
typedef long Py_ssize_t;

/* ── 函数指针表 ──────────────────────────────────────────────────── */

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
    PyObject* (*call_obj_args)(PyObject*, PyObject*);
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
    void      (*err_print)(void);

    int  (*run_simple)(const char*);
    PyObject* (*compile_string)(const char*, const char*, int);
    PyObject* (*eval_code)(PyObject*, PyObject*, PyObject*);

    PyObject* _None;
    int*      no_site;
    int*      no_user_site;
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
    SYM(err_print, "PyErr_Print");
    SYM(run_simple, "PyRun_SimpleString");
    SYM(compile_string, "Py_CompileString");
    SYM(eval_code, "PyEval_EvalCode");

    P._None = dlsym(lib, "_Py_NoneStruct");
    P.no_site = dlsym(lib, "Py_NoSiteFlag");
    P.no_user_site = dlsym(lib, "Py_NoUserSiteDirectory");
    P.ignore_env = dlsym(lib, "Py_IgnoreEnvironmentFlag");
    P.call_obj_args = dlsym(lib, "PyObject_CallFunctionObjArgs");

    return 0;
}

/* ── 全局状态 ────────────────────────────────────────────────────── */

static struct {
    int       init;
    PyObject* main_dict;
    PyObject* builtins;
    char      err[4096];
    int       eval_counter;
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

/* ── 公共 API ────────────────────────────────────────────────────── */

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
    if (P.no_user_site) *P.no_user_site = 1;

    P.init();
    if (!P.is_init()) return -1;

    PyObject* m = P.import_add_module("__main__");
    G.main_dict = P.module_get_dict(m);
    P.inc_ref(G.main_dict);
    G.builtins = P.import_add_module("builtins");
    P.inc_ref(G.builtins);
    G.eval_counter = 0;
    G.init = 1;
    return 0;
}

void pb_finalize(void) {
    if (!G.init) return;
    if (G.main_dict) P.dec_ref(G.main_dict);
    if (G.builtins) P.dec_ref(G.builtins);
    G.main_dict = NULL; G.builtins = NULL;
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

/* 使用 PyRun_SimpleString + PyDict_GetItemString 实现 eval/exec */

PyObject* pb_eval(const char* expr) {
    if (!G.init) return NULL;
    char buf[8192];
    snprintf(buf, sizeof(buf), "__pb_eval_result = (%s)", expr);
    if (P.run_simple(buf) != 0) { _capture_err(); return NULL; }
    PyObject* r = P.dict_get_str(G.main_dict, "__pb_eval_result");
    if (r) P.inc_ref(r);  /* dict_get 返回借用引用，转为新引用 */
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

/* 暴露 dict_get_str 给测试用 */
PyObject* pb_dict_get_str(PyObject* d, const char* k) { return P.dict_get_str(d, k); }
Py_ssize_t pb_list_size(PyObject* l) { return P.list_size(l); }
PyObject* pb_list_get(PyObject* l, Py_ssize_t i) { return P.list_get(l, i); }

/* 类型检查 */
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

/* bytes/tuple 辅助 */
Py_ssize_t pb_py_bytes_size(PyObject* o) { return P.bytes_from ? 0 : 0; /* TODO */ }
const char* pb_py_bytes_as_string(PyObject* o) { return NULL; /* TODO */ }
Py_ssize_t pb_py_tuple_size(PyObject* o) { return P.tuple_size(o); }
PyObject* pb_py_tuple_get(PyObject* o, Py_ssize_t i) { return P.tuple_get(o, i); }
