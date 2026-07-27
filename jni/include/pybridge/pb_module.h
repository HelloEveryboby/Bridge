/**
 * PyBridge 多语言模块协议
 *
 * 任何语言只要编译为 .so 并导出以下函数，就能被 PyBridge 加载。
 *
 * 导出函数:
 *   pb_module_info()  → 返回模块元数据 JSON
 *   pb_module_func(name) → 按名获取函数指针
 *
 * 编译示例:
 *   C:    gcc -shared -fPIC -o libmymod.so mymod.c
 *   C++:  g++ -shared -fPIC -o libmymod.so mymod.cpp
 *   Rust: cargo build --release (crate-type=["cdylib"])
 *   Go:   go build -buildmode=c-shared -o libmymod.so mymod.go
 */
#ifndef PB_MODULE_H
#define PB_MODULE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 模块元数据 ──────────────────────────────────────────────────── */

/**
 * 函数描述符
 */
typedef struct {
    const char* name;        /* 函数名 */
    const char* signature;   /* 参数签名，如 "dd>d" 表示 (double, double) -> double */
    void*       func_ptr;    /* 函数指针 */
} PB_FuncDef;

/**
 * 签名字符含义:
 *   v = void
 *   i = int32
 *   l = int64
 *   f = float
 *   d = double
 *   b = bool
 *   s = string (const char*)
 *   p = pointer (void*)
 *   > 分隔参数和返回值
 *
 * 示例:
 *   "dd>d"    → double add(double, double)
 *   "ii>i"    → int add(int, int)
 *   "s>s"     → const char* greet(const char*)
 *   "v>v"     → void init()
 *   "si>v"    → void log(const char*, int)
 */

/* ── 模块必须导出的函数 ──────────────────────────────────────────── */

/**
 * 返回模块信息 JSON
 *
 * 格式:
 * {
 *   "name": "mymod",
 *   "version": "1.0.0",
 *   "language": "rust",
 *   "functions": [
 *     {"name": "add", "signature": "dd>d"},
 *     {"name": "greet", "signature": "s>s"}
 *   ]
 * }
 */
typedef const char* (*pb_module_info_fn)(void);

/**
 * 按名获取函数指针
 */
typedef void* (*pb_module_func_fn)(const char* name);

/* ── 辅助宏：简化模块定义 ────────────────────────────────────────── */

/**
 * 定义一个函数并注册到模块
 *
 * C 用法:
 *   PB_DEF(double, add, double a, double b) {
 *       return a + b;
 *   }
 */
#define PB_DEF(ret, name, ...) \
    ret name(__VA_ARGS__); \
    static const PB_FuncDef _pb_func_##name = { #name, NULL, (void*)name }; \
    ret name(__VA_ARGS__)

/**
 * 声明模块函数表（在模块 .c 文件末尾使用）
 *
 * 用法:
 *   PB_MODULE("mymod", "1.0.0", "c",
 *       PB_FUNC_ENTRY(add, "dd>d"),
 *       PB_FUNC_ENTRY(greet, "s>s")
 *   )
 */
#define PB_FUNC_ENTRY(name, sig) { #name, sig, (void*)name }

#define PB_MODULE(mod_name, mod_ver, mod_lang, ...) \
    static const PB_FuncDef _pb_functions[] = { __VA_ARGS__ }; \
    static const int _pb_func_count = sizeof(_pb_functions) / sizeof(_pb_functions[0]); \
    \
    static char _pb_info_json[4096]; \
    \
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
    \
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

#endif /* PB_MODULE_H */
