/**
 * DLC 注册表实现 — 连接器管理 + 跨语言桥接路由
 */

#include "dlc_registry.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 全局注册表 ────────────────────────────────────────────────── */

static DLC_Connector* g_connectors[DLC_MAX_CONNECTORS];
static int g_count = 0;
static int g_initialized = 0;

/* ─── 注册 ──────────────────────────────────────────────────────── */

int dlc_register(DLC_Connector* connector) {
    if (!connector || !connector->language) return -1;
    if (g_count >= DLC_MAX_CONNECTORS) {
        fprintf(stderr, "[DLC] Registry full, cannot register '%s'\n", connector->language);
        return -1;
    }
    /* 检查是否已注册 */
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_connectors[i]->language, connector->language) == 0) {
            fprintf(stderr, "[DLC] '%s' already registered\n", connector->language);
            return -1;
        }
    }
    g_connectors[g_count++] = connector;
    printf("[DLC] Registered: %s (%s)\n", connector->language, connector->version);
    return 0;
}

/* ─── 动态加载 ──────────────────────────────────────────────────── */

DLC_Connector* dlc_load(const char* so_path) {
    void* handle = dlopen(so_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "[DLC] dlopen(%s) failed: %s\n", so_path, dlerror());
        return NULL;
    }

    dlc_get_connector_fn get_connector = (dlc_get_connector_fn)dlsym(handle, "dlc_get_connector");
    if (!get_connector) {
        fprintf(stderr, "[DLC] dlsym(dlc_get_connector) failed: %s\n", dlerror());
        dlclose(handle);
        return NULL;
    }

    DLC_Connector* conn = get_connector();
    if (!conn) {
        fprintf(stderr, "[DLC] dlc_get_connector() returned NULL\n");
        dlclose(handle);
        return NULL;
    }

    conn->native_handle = handle;  /* 保存句柄用于后续卸载 */
    dlc_register(conn);
    return conn;
}

/* ─── 查找 ──────────────────────────────────────────────────────── */

DLC_Connector* dlc_find(const char* language) {
    if (!language) return NULL;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_connectors[i]->language, language) == 0)
            return g_connectors[i];
    }
    return NULL;
}

int dlc_count(void) { return g_count; }

DLC_Connector* dlc_get(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return g_connectors[index];
}

/* ─── 卸载 ──────────────────────────────────────────────────────── */

void dlc_unload_all(void) {
    for (int i = g_count - 1; i >= 0; i--) {
        if (g_connectors[i]->finalize)
            g_connectors[i]->finalize();
        if (g_connectors[i]->native_handle)
            dlclose(g_connectors[i]->native_handle);
    }
    g_count = 0;
    g_initialized = 0;
}

/* ─── 跨语言桥接 ────────────────────────────────────────────────── */

DLC_Value* dlc_bridge_call(const char* target_lang, const char* func_name,
                           DLC_Value** args, int argc) {
    DLC_Connector* conn = dlc_find(target_lang);
    if (!conn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Language '%s' not registered", target_lang);
        return dlc_error("DLC_NOT_FOUND", msg);
    }
    if (!conn->call) {
        return dlc_error("DLC_NO_CALL", "Connector does not support call()");
    }
    return conn->call(func_name, args, argc);
}

DLC_Value* dlc_bridge_eval(const char* target_lang, const char* code) {
    DLC_Connector* conn = dlc_find(target_lang);
    if (!conn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Language '%s' not registered", target_lang);
        return dlc_error("DLC_NOT_FOUND", msg);
    }
    if (!conn->eval) {
        return dlc_error("DLC_NO_EVAL", "Connector does not support eval()");
    }
    return conn->eval(code);
}

DLC_Value* dlc_bridge_exec(const char* target_lang, const char* code) {
    DLC_Connector* conn = dlc_find(target_lang);
    if (!conn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Language '%s' not registered", target_lang);
        return dlc_error("DLC_NOT_FOUND", msg);
    }
    if (!conn->exec) {
        return dlc_error("DLC_NO_EXEC", "Connector does not support exec()");
    }
    return conn->exec(code);
}

int dlc_bridge_set_global(const char* target_lang, const char* name, DLC_Value* val) {
    DLC_Connector* conn = dlc_find(target_lang);
    if (!conn || !conn->set_global) return -1;
    return conn->set_global(name, val);
}

DLC_Value* dlc_bridge_get_global(const char* target_lang, const char* name) {
    DLC_Connector* conn = dlc_find(target_lang);
    if (!conn || !conn->get_global) return NULL;
    return conn->get_global(name);
}

/* ─── 批量初始化 ────────────────────────────────────────────────── */

int dlc_init_all(void) {
    if (g_initialized) return 0;
    int failures = 0;
    for (int i = 0; i < g_count; i++) {
        if (g_connectors[i]->init) {
            int rc = g_connectors[i]->init(NULL);
            if (rc != 0) {
                fprintf(stderr, "[DLC] Failed to init '%s' (rc=%d)\n",
                        g_connectors[i]->language, rc);
                failures++;
            } else {
                printf("[DLC] Initialized: %s\n", g_connectors[i]->language);
            }
        }
    }
    g_initialized = 1;
    return failures;
}