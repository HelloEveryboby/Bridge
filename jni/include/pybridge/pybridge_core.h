/**
 * PyBridge Core — 动态加载版头文件
 *
 * 不依赖 Python.h，所有 Python API 通过 dlsym 动态获取
 */
#ifndef PYBRIDGE_CORE_H
#define PYBRIDGE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Python 对象（不透明指针） */
typedef struct _object PyObject;
typedef long Py_ssize_t;  /* 64位 */

/* ── 生命周期 ────────────────────────────────────────────────────── */

/** 初始化 Python 解释器。返回 0 成功，-1 失败。 */
int  pb_init(const char* python_home);

/** 关闭解释器 */
void pb_finalize(void);

/** 是否已初始化 */
int  pb_is_init(void);

/** Python 版本字符串 */
const char* pb_version(void);

/* ── 模块 ───────────────────────────────────────────────────────── */

/** 导入模块，返回新引用。失败返回 NULL。 */
PyObject* pb_import(const char* name);

/** 添加模块搜索路径到 sys.path[0] */
int pb_add_path(const char* path);

/* ── 函数调用 ───────────────────────────────────────────────────── */

/** 调用 callable(args, kwargs)。返回新引用。 */
PyObject* pb_call(PyObject* callable, PyObject* args, PyObject* kwargs);

/** 调用 obj.method(args) */
PyObject* pb_call_method(PyObject* obj, const char* method, PyObject* args);

/** 无参调用 */
PyObject* pb_call0(PyObject* callable);

/* ── 属性 ───────────────────────────────────────────────────────── */

PyObject* pb_getattr(PyObject* obj, const char* name);
int       pb_setattr(PyObject* obj, const char* name, PyObject* val);

/* ── 类型转换 → Python ──────────────────────────────────────────── */

PyObject* pb_from_none(void);
PyObject* pb_from_bool(int val);
PyObject* pb_from_long(long val);
PyObject* pb_from_double(double val);
PyObject* pb_from_string(const char* val);
PyObject* pb_from_bytes(const char* data, long len);

/* ── 类型转换 → C ──────────────────────────────────────────────── */

int         pb_to_bool(PyObject* obj);
long        pb_to_long(PyObject* obj);
double      pb_to_double(PyObject* obj);
const char* pb_to_string(PyObject* obj);  // 内部缓冲区，下次调用失效
int         pb_is_none(PyObject* obj);
int         pb_is_callable(PyObject* obj);

/* ── 容器 ───────────────────────────────────────────────────────── */

PyObject* pb_tuple_new(long len);
int       pb_tuple_set(PyObject* tup, long i, PyObject* val);
PyObject* pb_list_new(long len);
int       pb_list_set(PyObject* list, long i, PyObject* val);
PyObject* pb_dict_new(void);
int       pb_dict_set(PyObject* dict, PyObject* key, PyObject* val);

/* ── 异常 ───────────────────────────────────────────────────────── */

int         pb_has_err(void);
const char* pb_err_message(void);
const char* pb_err_traceback(void);
void        pb_err_clear(void);

/* ── 引用计数 ───────────────────────────────────────────────────── */

void pb_decref(PyObject* obj);
void pb_incref(PyObject* obj);

/* ── 执行代码 ───────────────────────────────────────────────────── */

/** 执行语句。返回 None 或 NULL（失败）。 */
PyObject* pb_exec(const char* code);

/** 求值表达式。返回结果或 NULL（失败）。 */
PyObject* pb_eval(const char* expr);

/* ── 扩展 API ───────────────────────────────────────────────────── */

/** 从 dict 获取字符串键的值 */
PyObject* pb_dict_get_str(PyObject* dict, const char* key);

/** 获取 list 长度 */
long pb_list_size(PyObject* list);

/** 获取 list 元素 */
PyObject* pb_list_get(PyObject* list, long index);

/* 类型检查（JNI 层需要） */
int pb_type_check(PyObject* obj, const char* type_name);

/* 快捷类型检查宏 */
#define PyBool_Check(o)    pb_type_check(o, "bool")
#define PyLong_Check(o)    pb_type_check(o, "int")
#define PyFloat_Check(o)   pb_type_check(o, "float")
#define PyUnicode_Check(o) pb_type_check(o, "str")
#define PyBytes_Check(o)   pb_type_check(o, "bytes")
#define PyList_Check(o)    pb_type_check(o, "list")
#define PyTuple_Check(o)   pb_type_check(o, "tuple")
#define PyDict_Check(o)    pb_type_check(o, "dict")

/* 需要的 C API 函数（从 libpython 动态加载） */
Py_ssize_t pb_py_bytes_size(PyObject*);
const char* pb_py_bytes_as_string(PyObject*);
Py_ssize_t pb_py_tuple_size(PyObject*);
PyObject* pb_py_tuple_get(PyObject*, Py_ssize_t);

#ifdef __cplusplus
}
#endif

#endif /* PYBRIDGE_CORE_H */
