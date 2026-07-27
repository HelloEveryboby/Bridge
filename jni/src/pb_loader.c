/**
 * PyBridge 模块加载器
 *
 * 动态加载各语言编译的 .so 模块，统一调用接口。
 *
 * 编译: gcc -shared -fPIC -o libpb_loader.so pb_loader.c -ldl
 */
#include "pybridge/pb_module.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 模块句柄 ────────────────────────────────────────────────────── */

typedef struct PB_Module {
    char*            name;           /* 模块名 */
    char*            path;           /* .so 路径 */
    void*            handle;         /* dlopen 句柄 */
    pb_module_info_fn info_fn;       /* pb_module_info */
    pb_module_func_fn func_fn;       /* pb_module_func */
    char*            info_json;      /* 缓存的元数据 JSON */
    struct PB_Module* next;          /* 链表 */
} PB_Module;

static PB_Module* g_modules = NULL;

/* ── 加载模块 ────────────────────────────────────────────────────── */

PB_Module* pb_load_module(const char* name, const char* path) {
    /* 检查是否已加载 */
    for (PB_Module* m = g_modules; m; m = m->next) {
        if (strcmp(m->name, name) == 0) return m;
    }

    /* dlopen */
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "pb_load_module(%s): %s\n", name, dlerror());
        return NULL;
    }

    /* 获取导出函数 */
    pb_module_info_fn info_fn = (pb_module_info_fn)dlsym(handle, "pb_module_info");
    pb_module_func_fn func_fn = (pb_module_func_fn)dlsym(handle, "pb_module_func");

    if (!info_fn || !func_fn) {
        fprintf(stderr, "pb_load_module(%s): not a valid PB module (missing pb_module_info/pb_module_func)\n", name);
        dlclose(handle);
        return NULL;
    }

    /* 创建模块对象 */
    PB_Module* mod = (PB_Module*)calloc(1, sizeof(PB_Module));
    mod->name = strdup(name);
    mod->path = strdup(path);
    mod->handle = handle;
    mod->info_fn = info_fn;
    mod->func_fn = func_fn;
    mod->info_json = strdup(info_fn());

    /* 加入链表 */
    mod->next = g_modules;
    g_modules = mod;

    return mod;
}

/* ── 获取函数指针 ────────────────────────────────────────────────── */

void* pb_get_func(PB_Module* mod, const char* func_name) {
    if (!mod || !mod->func_fn) return NULL;
    return mod->func_fn(func_name);
}

/* ── 获取模块信息 ────────────────────────────────────────────────── */

const char* pb_module_get_info(PB_Module* mod) {
    return mod ? mod->info_json : NULL;
}

const char* pb_module_get_name(PB_Module* mod) {
    return mod ? mod->name : NULL;
}

/* ── 枚举所有模块 ────────────────────────────────────────────────── */

int pb_module_count(void) {
    int count = 0;
    for (PB_Module* m = g_modules; m; m = m->next) count++;
    return count;
}

PB_Module* pb_module_get(int index) {
    int i = 0;
    for (PB_Module* m = g_modules; m; m = m->next) {
        if (i++ == index) return m;
    }
    return NULL;
}

PB_Module* pb_module_find(const char* name) {
    for (PB_Module* m = g_modules; m; m = m->next) {
        if (strcmp(m->name, name) == 0) return m;
    }
    return NULL;
}

/* ── 卸载模块 ────────────────────────────────────────────────────── */

void pb_unload_module(PB_Module* mod) {
    if (!mod) return;

    /* 从链表移除 */
    if (g_modules == mod) {
        g_modules = mod->next;
    } else {
        for (PB_Module* m = g_modules; m; m = m->next) {
            if (m->next == mod) { m->next = mod->next; break; }
        }
    }

    dlclose(mod->handle);
    free(mod->name);
    free(mod->path);
    free(mod->info_json);
    free(mod);
}

void pb_unload_all(void) {
    while (g_modules) {
        PB_Module* m = g_modules;
        g_modules = m->next;
        dlclose(m->handle);
        free(m->name);
        free(m->path);
        free(m->info_json);
        free(m);
    }
}

/* ── 类型签名解析 ────────────────────────────────────────────────── */

/**
 * 解析签名字符串，返回参数类型数组和返回类型
 *
 * "dd>d" → params=['d','d'], ret='d', param_count=2
 */
typedef struct {
    char   param_types[16];  /* 参数类型字符 */
    int    param_count;      /* 参数数量 */
    char   return_type;      /* 返回类型字符 */
} PB_Signature;

PB_Signature pb_parse_signature(const char* sig) {
    PB_Signature s = {{0}, 0, 'v'};
    if (!sig) return s;

    const char* arrow = strchr(sig, '>');
    if (!arrow) return s;

    /* 参数部分（> 之前） */
    for (const char* p = sig; p < arrow && s.param_count < 16; p++) {
        s.param_types[s.param_count++] = *p;
    }

    /* 返回类型（> 之后第一个字符） */
    if (*(arrow + 1)) s.return_type = *(arrow + 1);

    return s;
}
