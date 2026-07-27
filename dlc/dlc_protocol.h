/**
 * DLC (Dynamic Language Connector) — 统一协议头文件
 *
 * 所有语言连接器必须实现此协议。每个连接器编译为 .so 共享库，
 * 导出 dlc_get_connector() 函数，返回 DLC_Connector 结构体指针。
 *
 * 支持的语言: Python, C, C++, Java, JavaScript, C#, SQL, TypeScript, Rust
 *
 * 跨语言调用流程:
 *   App → dlc_call("python", "my_func", args) → Python DLC 执行
 *   Python → dlc_bridge_call("javascript", ...) → DLC Bridge 路由 → JS DLC 执行
 */

#ifndef DLC_PROTOCOL_H
#define DLC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 值类型枚举 ────────────────────────────────────────────────── */

typedef enum {
    DLC_NULL     = 0,
    DLC_BOOL     = 1,
    DLC_INT      = 2,
    DLC_DOUBLE   = 3,
    DLC_STRING   = 4,
    DLC_BYTES    = 5,
    DLC_ARRAY    = 6,
    DLC_OBJECT   = 7,
    DLC_FUNCTION = 8,
    DLC_ERROR    = 9,
    DLC_DATE     = 10
} DLC_Type;

/* ─── 数组结构 ──────────────────────────────────────────────────── */

typedef struct DLC_Array {
    struct DLC_Value** items;
    int    length;
    int    capacity;
} DLC_Array;

/* ─── 对象键值对 ────────────────────────────────────────────────── */

typedef struct DLC_KVPair {
    char*  key;
    struct DLC_Value* value;
} DLC_KVPair;

typedef struct DLC_Object {
    DLC_KVPair* pairs;
    int   count;
    int   capacity;
} DLC_Object;

/* ─── 通用值类型 ────────────────────────────────────────────────── */

typedef struct DLC_Value {
    DLC_Type type;
    union {
        int      bool_val;
        int64_t  int_val;
        double   double_val;
        char*    string_val;
        DLC_Array*  array_val;
        DLC_Object* object_val;
        void*    ptr_val;
        struct {
            char* code;
            char* message;
        } error_val;
    } data;
} DLC_Value;

/* ─── 连接器接口 ────────────────────────────────────────────────── */

typedef struct DLC_Connector {
    /* 元数据 */
    const char* language;       /* "python", "javascript", "csharp", ... */
    const char* version;        /* 运行时版本号 */
    const char* file_ext;       /* ".py", ".js", ".cs", ... */
    const char* description;    /* 一句话描述 */

    /* 生命周期 */
    int  (*init)(void* config);     /* 初始化运行时，返回 0 成功 */
    void (*finalize)(void);         /* 关闭运行时 */

    /* 执行 */
    DLC_Value* (*eval)(const char* code);    /* 求值表达式，返回结果 */
    DLC_Value* (*exec)(const char* code);    /* 执行语句，返回 NULL 或最后表达式结果 */

    /* 函数调用 */
    DLC_Value* (*call)(const char* func_name, DLC_Value** args, int argc);

    /* 全局变量 */
    int  (*set_global)(const char* name, DLC_Value* val);
    DLC_Value* (*get_global)(const char* name);

    /* 内存管理 */
    void (*free_val)(DLC_Value* val);

    /* 原生句柄 (用于高级操作) */
    void* native_handle;
} DLC_Connector;

/* ─── 每个 DLC .so 必须导出的函数 ───────────────────────────────── */

typedef DLC_Connector* (*dlc_get_connector_fn)(void);

/* ─── 值构造/析构工具函数 ───────────────────────────────────────── */

/* 创建值 */
DLC_Value* dlc_null(void);
DLC_Value* dlc_bool(int v);
DLC_Value* dlc_int(int64_t v);
DLC_Value* dlc_double(double v);
DLC_Value* dlc_string(const char* v);
DLC_Value* dlc_error(const char* code, const char* msg);
DLC_Value* dlc_array_new(int capacity);
DLC_Value* dlc_object_new(int capacity);

/* 数组操作 */
void dlc_array_push(DLC_Value* arr, DLC_Value* item);
DLC_Value* dlc_array_get(DLC_Value* arr, int index);
int  dlc_array_len(DLC_Value* arr);

/* 对象操作 */
void dlc_object_set(DLC_Value* obj, const char* key, DLC_Value* val);
DLC_Value* dlc_object_get(DLC_Value* obj, const char* key);
int  dlc_object_has(DLC_Value* obj, const char* key);

/* 释放值 */
void dlc_free(DLC_Value* val);

/* 序列化为 JSON 字符串 */
char* dlc_to_json(DLC_Value* val);

/* 值类型名称 */
const char* dlc_type_name(DLC_Type t);

#ifdef __cplusplus
}
#endif

#endif /* DLC_PROTOCOL_H */