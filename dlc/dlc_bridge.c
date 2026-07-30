/**
 * DLC 数据桥接实现 — 文件读取/转换/写入
 *
 * 依赖：
 *   - dlc_value.c 的工具函数（dlc_string/dlc_array_new/dlc_array_push/
 *     dlc_free/dlc_to_json）
 *   - dlc_registry.c 的路由函数（dlc_find/dlc_bridge_set_global/
 *     dlc_bridge_exec/dlc_bridge_get_global）
 *
 * 不依赖任何具体语言运行时。
 */

#include "dlc_bridge.h"
#include "dlc_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ─── 桥接器结构 ────────────────────────────────────────────────── */

typedef struct {
    char* path;
    char* lang;
} DLC_Endpoint;

struct DLC_Bridge {
    char* name;
    char  err[512];          /* 错误信息缓存，空串表示无错误 */

    DLC_Endpoint sources[DLC_BRIDGE_MAX_ENDPOINTS];
    int          n_sources;

    DLC_Endpoint sinks[DLC_BRIDGE_MAX_ENDPOINTS];
    int          n_sinks;

    DLC_BridgeMode  mode;
    DLC_TransformFn cb;      /* CALLBACK 模式 transform */
    char*           tlang;   /* LANG 模式语言名 */
    char*           tcode;   /* LANG 模式脚本代码 */
};

/* ─── 内部辅助 ──────────────────────────────────────────────────── */

/* 设置错误信息（printf 风格） */
static void bridge_set_error(DLC_Bridge* b, const char* fmt, ...) {
    if (!b) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b->err, sizeof(b->err), fmt, ap);
    va_end(ap);
}

/* 清除错误信息 */
static void bridge_clear_error(DLC_Bridge* b) {
    if (!b) return;
    b->err[0] = '\0';
}

/* 读取整个文件为字符串（调用方负责 free）。失败返回 NULL */
static char* read_file_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* 将字符串写入文件（覆盖模式）。成功返回 0，失败返回 -1 */
static int write_file_all(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t wr = fwrite(content, 1, len, f);
    fclose(f);
    return (wr == len) ? 0 : -1;
}

/* ─── 创建/销毁 ────────────────────────────────────────────────── */

DLC_Bridge* dlc_bridge_create(const char* name) {
    DLC_Bridge* b = (DLC_Bridge*)calloc(1, sizeof(DLC_Bridge));
    if (!b) return NULL;
    b->name  = name ? strdup(name) : strdup("bridge");
    b->mode  = DLC_BRIDGE_MODE_CALLBACK;
    b->cb    = NULL;
    b->tlang = NULL;
    b->tcode = NULL;
    b->err[0] = '\0';
    return b;
}

void dlc_bridge_destroy(DLC_Bridge* b) {
    if (!b) return;
    free(b->name);
    for (int i = 0; i < b->n_sources; i++) {
        free(b->sources[i].path);
        free(b->sources[i].lang);
    }
    for (int i = 0; i < b->n_sinks; i++) {
        free(b->sinks[i].path);
        free(b->sinks[i].lang);
    }
    free(b->tlang);
    free(b->tcode);
    free(b);
}

/* ─── 端点管理 ──────────────────────────────────────────────────── */

int dlc_bridge_add_source(DLC_Bridge* b, const char* file_path, const char* lang) {
    if (!b || !file_path) return -1;
    if (b->n_sources >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "源端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    b->sources[b->n_sources].path = strdup(file_path);
    b->sources[b->n_sources].lang = lang ? strdup(lang) : NULL;
    b->n_sources++;
    return 0;
}

int dlc_bridge_add_sink(DLC_Bridge* b, const char* file_path, const char* lang) {
    if (!b || !file_path) return -1;
    if (b->n_sinks >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "目标端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    b->sinks[b->n_sinks].path = strdup(file_path);
    b->sinks[b->n_sinks].lang = lang ? strdup(lang) : NULL;
    b->n_sinks++;
    return 0;
}

/* ─── transform 设置 ────────────────────────────────────────────── */

int dlc_bridge_set_transform_cb(DLC_Bridge* b, DLC_TransformFn fn) {
    if (!b) return -1;
    free(b->tlang); b->tlang = NULL;
    free(b->tcode); b->tcode = NULL;
    b->mode = DLC_BRIDGE_MODE_CALLBACK;
    b->cb   = fn;
    return 0;
}

int dlc_bridge_set_transform_lang(DLC_Bridge* b, const char* lang, const char* code) {
    if (!b || !lang || !code) return -1;
    free(b->tlang); b->tlang = strdup(lang);
    free(b->tcode); b->tcode = strdup(code);
    b->mode = DLC_BRIDGE_MODE_LANG;
    b->cb   = NULL;
    return 0;
}

/* ─── 错误信息 ──────────────────────────────────────────────────── */

const char* dlc_bridge_error(DLC_Bridge* b) {
    if (!b) return NULL;
    return b->err[0] ? b->err : NULL;
}

/* ─── 执行桥接 ──────────────────────────────────────────────────── */

int dlc_bridge_run(DLC_Bridge* b) {
    if (!b) return -1;
    bridge_clear_error(b);

    /* 校验 transform 已设置 */
    if (b->mode == DLC_BRIDGE_MODE_CALLBACK && b->cb == NULL) {
        bridge_set_error(b, "未设置 transform（请先调用 dlc_bridge_set_transform_cb "
                            "或 dlc_bridge_set_transform_lang）");
        return -1;
    }
    if (b->mode == DLC_BRIDGE_MODE_LANG && (b->tlang == NULL || b->tcode == NULL)) {
        bridge_set_error(b, "未设置 transform（请先调用 dlc_bridge_set_transform_cb "
                            "或 dlc_bridge_set_transform_lang）");
        return -1;
    }
    if (b->n_sources == 0) {
        bridge_set_error(b, "无源端点");
        return -1;
    }
    if (b->n_sinks == 0) {
        bridge_set_error(b, "无目标端点");
        return -1;
    }

    /* 1. 读取所有源文件 → 合并为 DLC_ARRAY（每项为源文件 DLC_STRING 内容） */
    DLC_Value* input_array = dlc_array_new(b->n_sources);
    if (!input_array) {
        bridge_set_error(b, "创建输入数组失败（内存不足）");
        return -1;
    }
    int read_ok = 0;
    for (int i = 0; i < b->n_sources; i++) {
        char* content = read_file_all(b->sources[i].path);
        if (!content) {
            bridge_set_error(b, "读取源文件失败: %s", b->sources[i].path);
            dlc_free(input_array);
            return -1;
        }
        dlc_array_push(input_array, dlc_string(content));
        free(content);
        read_ok++;
    }

    /* 2. 执行 transform，得到结果 DLC_Value */
    DLC_Value* result = NULL;
    if (b->mode == DLC_BRIDGE_MODE_CALLBACK) {
        /* CALLBACK 模式：直接调用回调，输入数组由桥接器释放 */
        result = b->cb(input_array);
        dlc_free(input_array);
        if (!result) {
            bridge_set_error(b, "transform 回调返回 NULL");
            return -1;
        }
    } else {
        /* LANG 模式：检查语言是否已注册 */
        DLC_Connector* conn = dlc_find(b->tlang);
        if (!conn) {
            bridge_set_error(b, "语言 '%s' 未注册", b->tlang);
            dlc_free(input_array);
            return -1;
        }
        /* set_global("bridge_input", input_array) */
        int rc = dlc_bridge_set_global(b->tlang, "bridge_input", input_array);
        if (rc != 0) {
            bridge_set_error(b, "set_global(bridge_input) 失败 (rc=%d)", rc);
            dlc_free(input_array);
            return -1;
        }
        /* 连接器将 DLC_Value 转换为原生表示后不再持有该指针，桥接器负责释放 */
        dlc_free(input_array);

        /* exec(code) — 返回值不再需要，释放 */
        DLC_Value* exec_ret = dlc_bridge_exec(b->tlang, b->tcode);
        if (exec_ret) {
            if (exec_ret->type == DLC_ERROR) {
                bridge_set_error(b, "exec 失败: %s",
                    exec_ret->data.error_val.message ? exec_ret->data.error_val.message
                                                     : "(无信息)");
                dlc_free(exec_ret);
                return -1;
            }
            dlc_free(exec_ret);
        }

        /* get_global("bridge_output") — 返回新值，桥接器拥有并最终释放 */
        result = dlc_bridge_get_global(b->tlang, "bridge_output");
        if (!result) {
            bridge_set_error(b, "get_global(bridge_output) 返回 NULL（脚本未产出输出）");
            return -1;
        }
        if (result->type == DLC_ERROR) {
            bridge_set_error(b, "bridge_output 错误: %s",
                result->data.error_val.message ? result->data.error_val.message : "(无信息)");
            dlc_free(result);
            return -1;
        }
    }

    /* 3. 把 transform 结果转为字符串（STRING 直接用；其他类型走 JSON） */
    char* out_str = NULL;
    if (result->type == DLC_STRING) {
        out_str = result->data.string_val ? strdup(result->data.string_val) : strdup("");
    } else {
        out_str = dlc_to_json(result);
    }
    if (!out_str) {
        bridge_set_error(b, "结果转字符串失败（内存不足）");
        dlc_free(result);
        return -1;
    }

    /* 4. 写入所有目标文件 */
    for (int i = 0; i < b->n_sinks; i++) {
        if (write_file_all(b->sinks[i].path, out_str) != 0) {
            bridge_set_error(b, "写入目标文件失败: %s", b->sinks[i].path);
            free(out_str);
            dlc_free(result);
            return -1;
        }
    }

    free(out_str);
    dlc_free(result);
    return read_ok;
}
