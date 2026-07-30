/**
 * DLC 数据桥接实现 — 通用端点读取/转换/写入
 *
 * 依赖：
 *   - dlc_value.c 的工具函数（dlc_string/dlc_array_new/dlc_array_push/
 *     dlc_free/dlc_to_json/dlc_array_get/dlc_array_len/dlc_object_get）
 *   - dlc_registry.c 的路由函数（dlc_find/dlc_bridge_set_global/
 *     dlc_bridge_exec/dlc_bridge_get_global）
 *
 * 不依赖任何具体语言运行时，也不依赖 jni/pybridge（可独立编译）。
 * 异步执行依赖 pthread。
 */

#include "dlc_bridge.h"
#include "dlc_registry.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ─── 端点结构 ────────────────────────────────────────────────── */

typedef struct {
    DLC_EndpointType type;
    char* lang;
    /* FILE */
    char* file_path;
    /* MEM / STRING（源：复制的数据；目标：run 后写入的 buffer） */
    char* buf;
    int   len;          /* 数据长度（字节） */
    int   cap;          /* buffer 容量 */
    /* STDIO */
    DLC_StdioType stdio;
} DLC_Endpoint;

/* ─── 桥接器结构 ──────────────────────────────────────────────── */

struct DLC_Bridge {
    char* name;
    char  err[512];          /* 错误信息缓存，空串表示无错误 */

    DLC_Endpoint sources[DLC_BRIDGE_MAX_ENDPOINTS];
    int          n_sources;

    DLC_Endpoint sinks[DLC_BRIDGE_MAX_ENDPOINTS];
    int          n_sinks;

    DLC_BridgeMode  mode;
    DLC_TransformFn cb;           /* CALLBACK 模式 transform */
    char*           tlang;        /* LANG 模式语言名 */
    char*           tcode;        /* LANG 模式脚本代码 */
    DLC_TransformFn stages[DLC_BRIDGE_MAX_STAGES];  /* PIPELINE 模式 stages */
    int             n_stages;
    char*           rule_json;    /* JSON_RULE 模式规则 */

    int max_retry;               /* 失败重试次数 */

    /* 异步执行 */
    pthread_t       thread;
    int             async_running;
    int             async_result;
    DLC_BridgeCallback async_cb;
    void*           async_user_data;
};

/* ─── 内部辅助：错误 ──────────────────────────────────────────── */

static void bridge_set_error(DLC_Bridge* b, const char* fmt, ...) {
    if (!b) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b->err, sizeof(b->err), fmt, ap);
    va_end(ap);
}

static void bridge_clear_error(DLC_Bridge* b) {
    if (!b) return;
    b->err[0] = '\0';
}

/* ─── 内部辅助：文件 I/O ──────────────────────────────────────── */

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
static int write_file_all(const char* path, const char* content, int len) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    size_t wr = fwrite(content, 1, (size_t)len, f);
    fclose(f);
    return (wr == (size_t)len) ? 0 : -1;
}

/* ─── 内部辅助：端点初始化/释放 ───────────────────────────────── */

static void endpoint_init(DLC_Endpoint* e, DLC_EndpointType type, const char* lang) {
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->lang = lang ? strdup(lang) : NULL;
}

static void endpoint_free(DLC_Endpoint* e) {
    if (!e) return;
    free(e->lang);
    free(e->file_path);
    free(e->buf);
    memset(e, 0, sizeof(*e));
}

/* 目标端点 buffer 追加写入 */
static int endpoint_buf_append(DLC_Endpoint* e, const char* data, int len) {
    if (len <= 0) return 0;
    int need = e->len + len + 1;
    if (need > e->cap) {
        int newcap = e->cap ? e->cap * 2 : 256;
        while (newcap < need) newcap *= 2;
        if (newcap > DLC_BRIDGE_MEM_MAX) return -1;
        char* nb = realloc(e->buf, newcap);
        if (!nb) return -1;
        e->buf = nb;
        e->cap = newcap;
    }
    memcpy(e->buf + e->len, data, len);
    e->len += len;
    e->buf[e->len] = '\0';
    return 0;
}

/* ─── 内部辅助：读取源端点 → DLC_STRING ───────────────────────── */

/* 读取一个源端点为 DLC_STRING。失败返回 NULL 并设置错误 */
static DLC_Value* endpoint_read_source(DLC_Bridge* b, DLC_Endpoint* e) {
    switch (e->type) {
        case DLC_ENDPOINT_FILE: {
            char* content = read_file_all(e->file_path);
            if (!content) {
                bridge_set_error(b, "读取源文件失败: %s", e->file_path);
                return NULL;
            }
            DLC_Value* v = dlc_string(content);
            free(content);
            return v;
        }
        case DLC_ENDPOINT_MEM:
        case DLC_ENDPOINT_STRING:
            return dlc_string(e->buf ? e->buf : "");
        case DLC_ENDPOINT_STDIO: {
            /* 从 stdin 读取全部 */
            if (e->stdio != DLC_STDIO_STDIN) {
                bridge_set_error(b, "STDIO 源端点只支持 stdin");
                return NULL;
            }
            char* content = NULL;
            int cap = 0, len = 0;
            int c;
            while ((c = getchar()) != EOF) {
                if (len + 1 >= cap) {
                    cap = cap ? cap * 2 : 4096;
                    char* nb = realloc(content, cap);
                    if (!nb) { free(content); return NULL; }
                    content = nb;
                }
                content[len++] = (char)c;
            }
            if (content) {
                content[len] = '\0';
            } else {
                content = strdup("");
            }
            DLC_Value* v = dlc_string(content);
            free(content);
            return v;
        }
        default:
            bridge_set_error(b, "未知源端点类型 %d", e->type);
            return NULL;
    }
}

/* ─── 内部辅助：写入目标端点 ──────────────────────────────────── */

/* 把字符串写入一个目标端点。成功返回 0，失败返回 -1 并设置错误 */
static int endpoint_write_sink(DLC_Bridge* b, DLC_Endpoint* e, const char* data, int len) {
    switch (e->type) {
        case DLC_ENDPOINT_FILE:
            if (write_file_all(e->file_path, data, len) != 0) {
                bridge_set_error(b, "写入目标文件失败: %s", e->file_path);
                return -1;
            }
            return 0;
        case DLC_ENDPOINT_MEM:
        case DLC_ENDPOINT_STRING:
            if (endpoint_buf_append(e, data, len) != 0) {
                bridge_set_error(b, "内存目标端点 buffer 溢出（>%d 字节）", DLC_BRIDGE_MEM_MAX);
                return -1;
            }
            return 0;
        case DLC_ENDPOINT_STDIO: {
            FILE* out = (e->stdio == DLC_STDIO_STDERR) ? stderr : stdout;
            size_t wr = fwrite(data, 1, (size_t)len, out);
            return (wr == (size_t)len) ? 0 : -1;
        }
        default:
            bridge_set_error(b, "未知目标端点类型 %d", e->type);
            return -1;
    }
}

/* ─── 内部辅助：transform 执行（含重试） ──────────────────────── */

/* 执行单个 transform（CALLBACK / LANG / PIPELINE / JSON_RULE），返回结果。
   失败返回 NULL 并设置错误。input_array 的所有权：本函数内释放 */
static DLC_Value* run_transform(DLC_Bridge* b, DLC_Value* input_array) {
    DLC_Value* result = NULL;

    if (b->mode == DLC_BRIDGE_MODE_CALLBACK) {
        result = b->cb(input_array);
        dlc_free(input_array);
        if (!result) {
            bridge_set_error(b, "transform 回调返回 NULL");
            return NULL;
        }
    } else if (b->mode == DLC_BRIDGE_MODE_LANG) {
        DLC_Connector* conn = dlc_find(b->tlang);
        if (!conn) {
            bridge_set_error(b, "语言 '%s' 未注册", b->tlang);
            dlc_free(input_array);
            return NULL;
        }
        int rc = dlc_bridge_set_global(b->tlang, "bridge_input", input_array);
        dlc_free(input_array);
        if (rc != 0) {
            bridge_set_error(b, "set_global(bridge_input) 失败 (rc=%d)", rc);
            return NULL;
        }
        DLC_Value* exec_ret = dlc_bridge_exec(b->tlang, b->tcode);
        if (exec_ret) {
            if (exec_ret->type == DLC_ERROR) {
                bridge_set_error(b, "exec 失败: %s",
                    exec_ret->data.error_val.message ? exec_ret->data.error_val.message : "(无信息)");
                dlc_free(exec_ret);
                return NULL;
            }
            dlc_free(exec_ret);
        }
        result = dlc_bridge_get_global(b->tlang, "bridge_output");
        if (!result) {
            bridge_set_error(b, "get_global(bridge_output) 返回 NULL");
            return NULL;
        }
        if (result->type == DLC_ERROR) {
            bridge_set_error(b, "bridge_output 错误: %s",
                result->data.error_val.message ? result->data.error_val.message : "(无信息)");
            dlc_free(result);
            return NULL;
        }
    } else if (b->mode == DLC_BRIDGE_MODE_PIPELINE) {
        /* 链式执行：input → stage1 → stage2 → ... → stageN */
        result = input_array;
        for (int i = 0; i < b->n_stages; i++) {
            DLC_Value* next = b->stages[i](result);
            dlc_free(result);
            if (!next) {
                bridge_set_error(b, "pipeline stage %d 返回 NULL", i);
                return NULL;
            }
            if (next->type == DLC_ERROR) {
                bridge_set_error(b, "pipeline stage %d 错误: %s", i,
                    next->data.error_val.message ? next->data.error_val.message : "(无信息)");
                dlc_free(next);
                return NULL;
            }
            result = next;
        }
    } else if (b->mode == DLC_BRIDGE_MODE_JSON_RULE) {
        /* JSON 规则委托给 javascript DLC 连接器 */
        DLC_Connector* conn = dlc_find("javascript");
        if (!conn) {
            bridge_set_error(b, "JSON_RULE 模式需要 javascript 连接器（未注册）");
            dlc_free(input_array);
            return NULL;
        }
        /* 构造 JS 脚本：把 rule_json 和 bridge_input 都注入 */
        /* 用 set_global 注入 rule，exec 一段脚本处理 */
        DLC_Value* rule_val = dlc_string(b->rule_json);
        dlc_bridge_set_global("javascript", "bridge_rule", rule_val);
        dlc_free(rule_val);
        dlc_bridge_set_global("javascript", "bridge_input", input_array);
        dlc_free(input_array);
        /* 脚本：解析 rule，按 op 分派 */
        const char* js_code =
            "(function(){"
            "  var rule = JSON.parse(bridge_rule);"
            "  var input = bridge_input;"
            "  var out = input;"
            "  if (rule.op === 'map' && Array.isArray(out)) {"
            "    out = out.map(function(x){ return eval(rule.expr); });"
            "  } else if (rule.op === 'filter' && Array.isArray(out)) {"
            "    out = out.filter(function(x){ return eval(rule.expr); });"
            "  } else if (rule.op === 'field' && out && typeof out === 'object') {"
            "    out = out[rule.name];"
            "  } else if (rule.op === 'rename' && out && typeof out === 'object') {"
            "    out[rule.to] = out[rule.from]; delete out[rule.from];"
            "  }"
            "  bridge_output = out;"
            "})();";
        DLC_Value* exec_ret = dlc_bridge_exec("javascript", js_code);
        if (exec_ret) {
            if (exec_ret->type == DLC_ERROR) {
                bridge_set_error(b, "JSON_RULE exec 失败: %s",
                    exec_ret->data.error_val.message ? exec_ret->data.error_val.message : "(无信息)");
                dlc_free(exec_ret);
                return NULL;
            }
            dlc_free(exec_ret);
        }
        result = dlc_bridge_get_global("javascript", "bridge_output");
        if (!result) {
            bridge_set_error(b, "JSON_RULE: bridge_output 返回 NULL");
            return NULL;
        }
    } else {
        bridge_set_error(b, "未知 transform 模式 %d", b->mode);
        dlc_free(input_array);
        return NULL;
    }

    return result;
}

/* 带 retry 的 transform 执行。需要重新读取源数据用于重试。
   返回结果；失败返回 NULL。
   注意：重试时需要重新构造 input_array，因此本函数接收源读取函数 */
static DLC_Value* run_transform_with_retry(DLC_Bridge* b) {
    /* 第一次执行：用已读取的源数据。为支持重试，每次重新读取源 */
    for (int attempt = 0; attempt <= b->max_retry; attempt++) {
        /* 重新读取所有源 → input_array（重试时需要重新读，因为前一次 transform 释放了它） */
        DLC_Value* input_array = dlc_array_new(b->n_sources);
        if (!input_array) {
            bridge_set_error(b, "创建输入数组失败（内存不足）");
            return NULL;
        }
        int read_ok = 0;
        int read_fail = 0;
        for (int i = 0; i < b->n_sources; i++) {
            DLC_Value* src = endpoint_read_source(b, &b->sources[i]);
            if (!src) {
                read_fail = 1;
                break;
            }
            dlc_array_push(input_array, src);
            read_ok++;
        }
        if (read_fail) {
            dlc_free(input_array);
            return NULL;  /* 读取错误不重试 */
        }

        /* 保存错误前状态，执行 transform */
        char saved_err[512];
        strncpy(saved_err, b->err, sizeof(saved_err) - 1);
        saved_err[sizeof(saved_err) - 1] = '\0';
        bridge_clear_error(b);

        DLC_Value* result = run_transform(b, input_array);

        if (result && result->type != DLC_ERROR) {
            return result;  /* 成功 */
        }

        /* 失败：记录错误，准备重试 */
        if (result) {
            /* DLC_ERROR 类型，提取信息 */
            if (result->data.error_val.message) {
                bridge_set_error(b, "transform 失败(尝试%d): %s", attempt + 1,
                    result->data.error_val.message);
            }
            dlc_free(result);
        }
        /* 若 run_transform 已设置错误则保留 */

        if (attempt < b->max_retry) {
            /* 还有重试机会，继续 */
            continue;
        }
        /* 重试用尽，返回 NULL（错误已设置） */
        return NULL;
    }
    return NULL;
}

/* ─── 创建/销毁 ────────────────────────────────────────────────── */

DLC_Bridge* dlc_bridge_create(const char* name) {
    DLC_Bridge* b = (DLC_Bridge*)calloc(1, sizeof(DLC_Bridge));
    if (!b) return NULL;
    b->name  = name ? strdup(name) : strdup("bridge");
    b->mode  = DLC_BRIDGE_MODE_CALLBACK;
    b->err[0] = '\0';
    return b;
}

void dlc_bridge_destroy(DLC_Bridge* b) {
    if (!b) return;
    /* 若有异步任务在运行，等待完成 */
    if (b->async_running) {
        dlc_bridge_wait(b);
    }
    free(b->name);
    for (int i = 0; i < b->n_sources; i++) endpoint_free(&b->sources[i]);
    for (int i = 0; i < b->n_sinks; i++) endpoint_free(&b->sinks[i]);
    free(b->tlang);
    free(b->tcode);
    free(b->rule_json);
    free(b);
}

/* ─── 源端点 ──────────────────────────────────────────────────── */

int dlc_bridge_add_source(DLC_Bridge* b, const char* file_path, const char* lang) {
    return dlc_bridge_add_source_file(b, file_path, lang);
}

int dlc_bridge_add_source_file(DLC_Bridge* b, const char* file_path, const char* lang) {
    if (!b || !file_path) return -1;
    if (b->n_sources >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "源端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sources[b->n_sources++];
    endpoint_init(e, DLC_ENDPOINT_FILE, lang);
    e->file_path = strdup(file_path);
    return 0;
}

int dlc_bridge_add_source_mem(DLC_Bridge* b, const char* data, int len, const char* lang) {
    if (!b || !data || len < 0) return -1;
    if (b->n_sources >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "源端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sources[b->n_sources++];
    endpoint_init(e, DLC_ENDPOINT_MEM, lang);
    e->cap = len + 1;
    e->buf = malloc(e->cap);
    if (!e->buf) { b->n_sources--; endpoint_init(e, 0, NULL); return -1; }
    memcpy(e->buf, data, len);
    e->buf[len] = '\0';
    e->len = len;
    return 0;
}

int dlc_bridge_add_source_str(DLC_Bridge* b, const char* str, const char* lang) {
    if (!b || !str) return -1;
    return dlc_bridge_add_source_mem(b, str, (int)strlen(str), lang);
}

int dlc_bridge_add_source_stdio(DLC_Bridge* b, DLC_StdioType which, const char* lang) {
    if (!b) return -1;
    if (b->n_sources >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "源端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sources[b->n_sources++];
    endpoint_init(e, DLC_ENDPOINT_STDIO, lang);
    e->stdio = which;
    return 0;
}

/* ─── 目标端点 ────────────────────────────────────────────────── */

int dlc_bridge_add_sink(DLC_Bridge* b, const char* file_path, const char* lang) {
    return dlc_bridge_add_sink_file(b, file_path, lang);
}

int dlc_bridge_add_sink_file(DLC_Bridge* b, const char* file_path, const char* lang) {
    if (!b || !file_path) return -1;
    if (b->n_sinks >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "目标端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sinks[b->n_sinks++];
    endpoint_init(e, DLC_ENDPOINT_FILE, lang);
    e->file_path = strdup(file_path);
    return 0;
}

int dlc_bridge_add_sink_mem(DLC_Bridge* b, const char* lang) {
    if (!b) return -1;
    if (b->n_sinks >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "目标端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sinks[b->n_sinks++];
    endpoint_init(e, DLC_ENDPOINT_MEM, lang);
    return 0;
}

int dlc_bridge_add_sink_str(DLC_Bridge* b, const char* lang) {
    if (!b) return -1;
    if (b->n_sinks >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "目标端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sinks[b->n_sinks++];
    endpoint_init(e, DLC_ENDPOINT_STRING, lang);
    return 0;
}

int dlc_bridge_add_sink_stdio(DLC_Bridge* b, DLC_StdioType which, const char* lang) {
    if (!b) return -1;
    if (b->n_sinks >= DLC_BRIDGE_MAX_ENDPOINTS) {
        bridge_set_error(b, "目标端点数已达上限 %d", DLC_BRIDGE_MAX_ENDPOINTS);
        return -1;
    }
    DLC_Endpoint* e = &b->sinks[b->n_sinks++];
    endpoint_init(e, DLC_ENDPOINT_STDIO, lang);
    e->stdio = which;
    return 0;
}

/* ─── 取出内存/字符串目标数据 ──────────────────────────────────── */

const char* dlc_bridge_get_sink_mem(DLC_Bridge* b, int idx, int* len) {
    if (!b || idx < 0 || idx >= b->n_sinks) return NULL;
    DLC_Endpoint* e = &b->sinks[idx];
    if (e->type != DLC_ENDPOINT_MEM && e->type != DLC_ENDPOINT_STRING) return NULL;
    if (len) *len = e->len;
    return e->buf;
}

const char* dlc_bridge_get_sink_str(DLC_Bridge* b, int idx) {
    if (!b || idx < 0 || idx >= b->n_sinks) return NULL;
    DLC_Endpoint* e = &b->sinks[idx];
    if (e->type != DLC_ENDPOINT_STRING && e->type != DLC_ENDPOINT_MEM) return NULL;
    return e->buf;
}

/* ─── transform 设置 ───────────────────────────────────────────── */

int dlc_bridge_set_transform_cb(DLC_Bridge* b, DLC_TransformFn fn) {
    if (!b) return -1;
    free(b->tlang); b->tlang = NULL;
    free(b->tcode); b->tcode = NULL;
    free(b->rule_json); b->rule_json = NULL;
    b->n_stages = 0;
    b->mode = DLC_BRIDGE_MODE_CALLBACK;
    b->cb   = fn;
    return 0;
}

int dlc_bridge_set_transform_lang(DLC_Bridge* b, const char* lang, const char* code) {
    if (!b || !lang || !code) return -1;
    free(b->tlang); b->tlang = strdup(lang);
    free(b->tcode); b->tcode = strdup(code);
    free(b->rule_json); b->rule_json = NULL;
    b->n_stages = 0;
    b->mode = DLC_BRIDGE_MODE_LANG;
    b->cb   = NULL;
    return 0;
}

int dlc_bridge_add_stage(DLC_Bridge* b, DLC_TransformFn stage) {
    if (!b || !stage) return -1;
    if (b->n_stages >= DLC_BRIDGE_MAX_STAGES) {
        bridge_set_error(b, "pipeline stage 数已达上限 %d", DLC_BRIDGE_MAX_STAGES);
        return -1;
    }
    /* 切换到 PIPELINE 模式（保留已添加的 stages） */
    if (b->mode != DLC_BRIDGE_MODE_PIPELINE) {
        free(b->tlang); b->tlang = NULL;
        free(b->tcode); b->tcode = NULL;
        free(b->rule_json); b->rule_json = NULL;
        b->cb = NULL;
        b->mode = DLC_BRIDGE_MODE_PIPELINE;
    }
    b->stages[b->n_stages++] = stage;
    return 0;
}

int dlc_bridge_set_transform_rule(DLC_Bridge* b, const char* rule_json) {
    if (!b || !rule_json) return -1;
    free(b->tlang); b->tlang = NULL;
    free(b->tcode); b->tcode = NULL;
    b->n_stages = 0;
    b->cb = NULL;
    free(b->rule_json); b->rule_json = strdup(rule_json);
    b->mode = DLC_BRIDGE_MODE_JSON_RULE;
    return 0;
}

/* ─── 运行时配置 ───────────────────────────────────────────────── */

int dlc_bridge_set_retry(DLC_Bridge* b, int max_retries) {
    if (!b) return -1;
    b->max_retry = max_retries < 0 ? 0 : max_retries;
    return 0;
}

/* ─── 错误信息/模式查询 ────────────────────────────────────────── */

const char* dlc_bridge_error(DLC_Bridge* b) {
    if (!b) return NULL;
    return b->err[0] ? b->err : NULL;
}

DLC_BridgeMode dlc_bridge_mode(DLC_Bridge* b) {
    return b ? b->mode : DLC_BRIDGE_MODE_CALLBACK;
}

/* ─── 执行桥接（同步） ────────────────────────────────────────── */

int dlc_bridge_run(DLC_Bridge* b) {
    if (!b) return -1;
    bridge_clear_error(b);

    /* 校验 transform 已设置 */
    if (b->mode == DLC_BRIDGE_MODE_CALLBACK && b->cb == NULL) {
        bridge_set_error(b, "未设置 transform（请先调用 set_transform_cb/lang/add_stage/set_transform_rule）");
        return -1;
    }
    if (b->mode == DLC_BRIDGE_MODE_LANG && (b->tlang == NULL || b->tcode == NULL)) {
        bridge_set_error(b, "未设置 transform（LANG 模式参数缺失）");
        return -1;
    }
    if (b->mode == DLC_BRIDGE_MODE_PIPELINE && b->n_stages == 0) {
        bridge_set_error(b, "未设置 transform（PIPELINE 模式无 stage）");
        return -1;
    }
    if (b->mode == DLC_BRIDGE_MODE_JSON_RULE && b->rule_json == NULL) {
        bridge_set_error(b, "未设置 transform（JSON_RULE 模式规则缺失）");
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

    /* 执行 transform（含重试逻辑，内部负责读取源） */
    DLC_Value* result = run_transform_with_retry(b);
    if (!result) {
        return -1;
    }

    /* 把结果转为字符串（STRING 直接用；其他类型走 JSON） */
    char* out_str = NULL;
    int out_len = 0;
    if (result->type == DLC_STRING) {
        out_str = result->data.string_val ? strdup(result->data.string_val) : strdup("");
        out_len = (int)strlen(out_str);
    } else {
        out_str = dlc_to_json(result);
        out_len = out_str ? (int)strlen(out_str) : 0;
    }
    if (!out_str) {
        bridge_set_error(b, "结果转字符串失败（内存不足）");
        dlc_free(result);
        return -1;
    }

    /* 写入所有目标端点 */
    for (int i = 0; i < b->n_sinks; i++) {
        if (endpoint_write_sink(b, &b->sinks[i], out_str, out_len) != 0) {
            free(out_str);
            dlc_free(result);
            return -1;
        }
    }

    free(out_str);
    dlc_free(result);
    return b->n_sources;
}

/* ─── 异步执行 ────────────────────────────────────────────────── */

/* 异步线程入口 */
static void* async_thread_fn(void* arg) {
    DLC_Bridge* b = (DLC_Bridge*)arg;
    int result = dlc_bridge_run(b);
    b->async_result = result;
    if (b->async_cb) {
        b->async_cb(b, result, b->async_user_data);
    }
    b->async_running = 0;
    return NULL;
}

int dlc_bridge_run_async(DLC_Bridge* b, DLC_BridgeCallback callback, void* user_data) {
    if (!b) return -1;
    if (b->async_running) {
        bridge_set_error(b, "已有异步任务在执行");
        return -1;
    }
    b->async_cb = callback;
    b->async_user_data = user_data;
    b->async_running = 1;
    b->async_result = 0;
    int rc = pthread_create(&b->thread, NULL, async_thread_fn, b);
    if (rc != 0) {
        b->async_running = 0;
        bridge_set_error(b, "创建线程失败 (rc=%d)", rc);
        return -1;
    }
    return 0;
}

int dlc_bridge_wait(DLC_Bridge* b) {
    if (!b || !b->async_running) return 0;
    int rc = pthread_join(b->thread, NULL);
    if (rc != 0) {
        bridge_set_error(b, "等待线程失败 (rc=%d)", rc);
        return -1;
    }
    return b->async_result;
}
