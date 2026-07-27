/**
 * DLC C++ 连接器
 *
 * 通过嵌入 Cling (C++ REPL) 或简单的 extern "C" 包装器实现 C++ 互操作。
 * 此实现使用 JIT 编译方式：将 C++ 代码编译为共享库并动态加载执行。
 *
 * 编译: g++ -shared -fPIC -O2 -o libdlc_cpp.so dlc_cpp.cpp -ldl
 */

#include "../dlc_protocol.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ─── 内部状态 ──────────────────────────────────────────────────── */

static char g_cpp_compiler[256] = "g++";
static char g_compile_flags[512] = "-std=c++17 -O2 -fPIC -shared";
static int  g_module_counter = 0;

/* 已加载的 C++ 模块 */
#define MAX_CPP_MODULES 64
static struct {
    char*  name;
    char*  source;
    void*  handle;
} g_modules[MAX_CPP_MODULES];
static int g_module_count = 0;

/* 函数注册表 */
#define MAX_CPP_FUNCTIONS 256
static struct {
    char*  name;
    void*  ptr;
    char*  signature;  /* "dd>d" = (double,double)->double */
} g_functions[MAX_CPP_FUNCTIONS];
static int g_function_count = 0;

/* ─── 编译 C++ 源码为 .so ───────────────────────────────────────── */

static char* compile_cpp(const char* source, const char* name) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/dlc_cpp_%d", g_module_counter++);
    mkdir(tmpdir, 0755);

    char srcfile[512];
    snprintf(srcfile, sizeof(srcfile), "%s/%s.cpp", tmpdir, name);

    /* 写入源码 */
    FILE* f = fopen(srcfile, "w");
    if (!f) { rmdir(tmpdir); return NULL; }
    fprintf(f, "%s", source);
    fclose(f);

    /* 编译 */
    char sofile[512];
    snprintf(sofile, sizeof(sofile), "%s/%s.so", tmpdir, name);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s -o %s %s 2>&1",
             g_cpp_compiler, g_compile_flags, sofile, srcfile);

    FILE* pipe = popen(cmd, "r");
    if (!pipe) { rmdir(tmpdir); return NULL; }

    char errbuf[4096] = {0};
    fread(errbuf, 1, sizeof(errbuf) - 1, pipe);
    int rc = pclose(pipe);

    if (rc != 0) {
        fprintf(stderr, "[DLC:CPP] Compile error:\n%s\n", errbuf);
        return NULL;
    }

    return strdup(sofile);
}

/* ─── 注册原生 C++ 函数 ─────────────────────────────────────────── */

static void register_cpp_func(const char* name, void* ptr, const char* sig) {
    if (g_function_count >= MAX_CPP_FUNCTIONS) return;
    g_functions[g_function_count].name = strdup(name);
    g_functions[g_function_count].ptr = ptr;
    g_functions[g_function_count].signature = strdup(sig);
    g_function_count++;
}

static void* find_cpp_func(const char* name) {
    for (int i = 0; i < g_function_count; i++) {
        if (strcmp(g_functions[i].name, name) == 0)
            return g_functions[i].ptr;
    }
    return NULL;
}

/* ─── 类型转换: DLC_Value → C++ 原生类型 ────────────────────────── */

static double val_to_double(DLC_Value* v) {
    if (!v) return 0.0;
    switch (v->type) {
    case DLC_INT:    return (double)v->data.int_val;
    case DLC_DOUBLE: return v->data.double_val;
    case DLC_BOOL:   return v->data.bool_val ? 1.0 : 0.0;
    case DLC_STRING: return v->data.string_val ? atof(v->data.string_val) : 0.0;
    default: return 0.0;
    }
}

/* ─── 初始化/终止 ───────────────────────────────────────────────── */

static int dlc_cpp_init(void* config) {
    (void)config;
    printf("[DLC:CPP] C++ Connector initialized (compiler: %s)\n", g_cpp_compiler);

    /* 注册内置 C++ 数学函数 */
    register_cpp_func("cpp_add",    NULL, "dd>d");
    register_cpp_func("cpp_sub",    NULL, "dd>d");
    register_cpp_func("cpp_mul",    NULL, "dd>d");
    register_cpp_func("cpp_div",    NULL, "dd>d");
    return 0;
}

static void dlc_cpp_finalize(void) {
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i].handle) dlclose(g_modules[i].handle);
        free(g_modules[i].name);
        free(g_modules[i].source);
    }
    g_module_count = 0;
    for (int i = 0; i < g_function_count; i++) {
        free(g_functions[i].name);
        free(g_functions[i].signature);
    }
    g_function_count = 0;
    printf("[DLC:CPP] Finalized\n");
}

/* ─── 执行 ──────────────────────────────────────────────────────── */

static DLC_Value* dlc_cpp_eval(const char* code) {
    /* C++ 不支持 eval，返回错误提示 */
    return dlc_error("CPP_NO_EVAL",
        "C++ does not support runtime eval. Use dlc_cpp_exec() to compile and run code.");
}

static DLC_Value* dlc_cpp_exec(const char* code) {
    if (!code || !*code) return dlc_null();

    char modname[64];
    snprintf(modname, sizeof(modname), "dlc_cpp_mod_%d", g_module_counter);

    char* so_path = compile_cpp(code, modname);
    if (!so_path) {
        return dlc_error("CPP_COMPILE_ERR",
            "Failed to compile C++ code. Check compiler output above.");
    }

    void* handle = dlopen(so_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        char msg[512];
        snprintf(msg, sizeof(msg), "dlopen failed: %s", dlerror());
        return dlc_error("CPP_LOAD_ERR", msg);
    }

    /* 查找入口函数 */
    typedef int (*entry_fn)(void);
    entry_fn entry = (entry_fn)dlsym(handle, "dlc_main");
    if (entry) {
        int rc = entry();
        dlclose(handle);
        return dlc_int(rc);
    }

    dlclose(handle);
    return dlc_int(0);
}

/* ─── 函数调用 ──────────────────────────────────────────────────── */

static DLC_Value* dlc_cpp_call(const char* func_name, DLC_Value** args, int argc) {
    /* 先查注册表 */
    void* fptr = find_cpp_func(func_name);

    /* 再查动态模块 */
    if (!fptr) {
        for (int i = g_module_count - 1; i >= 0; i--) {
            fptr = dlsym(g_modules[i].handle, func_name);
            if (fptr) break;
        }
    }

    if (!fptr) {
        char msg[256];
        snprintf(msg, sizeof(msg), "C++ function '%s' not found", func_name);
        return dlc_error("CPP_FUNC_NOT_FOUND", msg);
    }

    /* 根据参数个数调用 */
    typedef double (*fn_dd)(double, double);
    typedef double (*fn_d)(double);
    typedef int    (*fn_ii)(int, int);
    typedef int    (*fn_i)(int);

    if (argc == 2) {
        fn_dd fn = (fn_dd)fptr;
        double r = fn(val_to_double(args[0]), val_to_double(args[1]));
        return dlc_double(r);
    } else if (argc == 1) {
        fn_d fn = (fn_d)fptr;
        double r = fn(val_to_double(args[0]));
        return dlc_double(r);
    } else if (argc == 0) {
        fn_d fn = (fn_d)fptr;
        double r = fn(0);
        return dlc_double(r);
    }

    return dlc_error("CPP_ARGS_ERR", "Unsupported argument count for C++ call");
}

/* ─── 全局变量 ──────────────────────────────────────────────────── */

static int dlc_cpp_set_global(const char* name, DLC_Value* val) {
    (void)name; (void)val;
    return -1; /* C++ 不支持运行时全局变量设置 */
}

static DLC_Value* dlc_cpp_get_global(const char* name) {
    (void)name;
    return NULL; /* C++ 不支持运行时全局变量获取 */
}

/* ─── 内存管理 ──────────────────────────────────────────────────── */

static void dlc_cpp_free_val(DLC_Value* val) {
    dlc_free(val);
}

/* ─── 导出连接器 ────────────────────────────────────────────────── */

static DLC_Connector g_cpp_connector = {
    .language    = "cpp",
    .version     = "C++17/g++",
    .file_ext    = ".cpp",
    .description = "C++ connector via JIT compilation to shared library",
    .init        = dlc_cpp_init,
    .finalize    = dlc_cpp_finalize,
    .eval        = dlc_cpp_eval,
    .exec        = dlc_cpp_exec,
    .call        = dlc_cpp_call,
    .set_global  = dlc_cpp_set_global,
    .get_global  = dlc_cpp_get_global,
    .free_val    = dlc_cpp_free_val,
    .native_handle = NULL,
};

/* ─── 为 C 模块提供便捷的宏注册 ─────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

DLC_Connector* dlc_get_connector(void) {
    return &g_cpp_connector;
}

#ifdef __cplusplus
}
#endif