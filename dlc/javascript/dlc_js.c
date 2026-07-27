/**
 * DLC JavaScript 连接器 (基于 QuickJS 嵌入式引擎)
 *
 * QuickJS 是一个轻量级、可嵌入的 JavaScript 引擎，单个 C 文件即可编译。
 * 下载: https://bellard.org/quickjs/
 *
 * 编译:
 *   1. 下载 quickjs 源码到 dlc/javascript/quickjs/
 *   2. cd dlc/javascript && make
 *
 * 或使用系统 Node.js 作为备选（通过管道通信）。
 */

#include "../dlc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── QuickJS 头文件 (条件编译) ──────────────────────────────────── */

#ifdef DLC_USE_QUICKJS
#include "quickjs/quickjs.h"

static JSRuntime* g_rt = NULL;
static JSContext* g_ctx = NULL;

/* ─── QuickJS → DLC_Value 转换 ──────────────────────────────────── */

static DLC_Value* js_to_dlc(JSContext* ctx, JSValue val) {
    if (JS_IsNull(val) || JS_IsUndefined(val))
        return dlc_null();

    if (JS_IsBool(val))
        return dlc_bool(JS_ToBool(ctx, val));

    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        if (d == (int64_t)d && d >= INT64_MIN && d <= INT64_MAX)
            return dlc_int((int64_t)d);
        return dlc_double(d);
    }

    if (JS_IsString(val)) {
        const char* s = JS_ToCString(ctx, val);
        DLC_Value* v = dlc_string(s);
        JS_FreeCString(ctx, s);
        return v;
    }

    if (JS_IsArray(ctx, val)) {
        JSValue len_val = JS_GetPropertyStr(ctx, val, "length");
        int len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);

        DLC_Value* arr = dlc_array_new(len);
        for (int i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, val, i);
            dlc_array_push(arr, js_to_dlc(ctx, item));
            JS_FreeValue(ctx, item);
        }
        return arr;
    }

    if (JS_IsObject(val)) {
        JSPropertyEnum* props = NULL;
        uint32_t prop_count = 0;
        DLC_Value* obj = dlc_object_new(8);

        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, val, JS_GPN_STRING_MASK) == 0) {
            for (uint32_t i = 0; i < prop_count; i++) {
                const char* key = JS_AtomToCString(ctx, props[i].atom);
                JSValue pval = JS_GetProperty(ctx, val, props[i].atom);
                dlc_object_set(obj, key, js_to_dlc(ctx, pval));
                JS_FreeValue(ctx, pval);
                JS_FreeCString(ctx, key);
            }
            for (uint32_t i = 0; i < prop_count; i++)
                JS_FreeAtom(ctx, props[i].atom);
            js_free(ctx, props);
        }
        return obj;
    }

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        DLC_Value* v = dlc_error("JS_EXCEPTION", msg);
        JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        return v;
    }

    return dlc_null();
}

/* ─── DLC_Value → QuickJS ───────────────────────────────────────── */

static JSValue dlc_to_js(JSContext* ctx, DLC_Value* val) {
    if (!val) return JS_NULL;

    switch (val->type) {
    case DLC_NULL:   return JS_NULL;
    case DLC_BOOL:   return JS_NewBool(ctx, val->data.bool_val);
    case DLC_INT:    return JS_NewInt64(ctx, val->data.int_val);
    case DLC_DOUBLE: return JS_NewFloat64(ctx, val->data.double_val);
    case DLC_STRING: return JS_NewString(ctx, val->data.string_val ? val->data.string_val : "");
    case DLC_ARRAY: {
        int len = val->data.array_val ? val->data.array_val->length : 0;
        JSValue arr = JS_NewArray(ctx);
        for (int i = 0; i < len; i++) {
            JS_SetPropertyUint32(ctx, arr, i, dlc_to_js(ctx, val->data.array_val->items[i]));
        }
        return arr;
    }
    case DLC_OBJECT: {
        JSValue obj = JS_NewObject(ctx);
        if (val->data.object_val) {
            for (int i = 0; i < val->data.object_val->count; i++) {
                JS_SetPropertyStr(ctx, obj,
                    val->data.object_val->pairs[i].key,
                    dlc_to_js(ctx, val->data.object_val->pairs[i].value));
            }
        }
        return obj;
    }
    case DLC_ERROR:
        return JS_ThrowInternalError(ctx, "%s", val->data.error_val.message);
    default:
        return JS_NULL;
    }
}

/* ─── 初始化 ────────────────────────────────────────────────────── */

static int dlc_js_init(void* config) {
    (void)config;
    g_rt = JS_NewRuntime();
    if (!g_rt) return -1;
    JS_SetMemoryLimit(g_rt, 256 * 1024 * 1024); /* 256MB */
    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) { JS_FreeRuntime(g_rt); g_rt = NULL; return -1; }

    /* 注入全局 bridge 对象，用于跨语言调用 */
    const char* bridge_js =
        "var __dlc_bridge = {"
        "  _registry: {},"
        "  register: function(name, fn) { this._registry[name] = fn; },"
        "  call: function(name) {"
        "    var args = Array.prototype.slice.call(arguments, 1);"
        "    var fn = this._registry[name];"
        "    return fn ? fn.apply(null, args) : null;"
        "  }"
        "};";
    JSValue result = JS_Eval(g_ctx, bridge_js, strlen(bridge_js), "<dlc_init>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(g_ctx);
        const char* msg = JS_ToCString(g_ctx, exc);
        fprintf(stderr, "[DLC:JS] Init error: %s\n", msg);
        JS_FreeCString(g_ctx, msg);
        JS_FreeValue(g_ctx, exc);
    }
    JS_FreeValue(g_ctx, result);

    printf("[DLC:JS] QuickJS engine initialized\n");
    return 0;
}

static void dlc_js_finalize(void) {
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = NULL; }
    if (g_rt)  { JS_FreeRuntime(g_rt); g_rt = NULL; }
    printf("[DLC:JS] Finalized\n");
}

/* ─── 执行 ──────────────────────────────────────────────────────── */

static DLC_Value* dlc_js_eval(const char* code) {
    if (!g_ctx) return dlc_error("JS_NOT_INIT", "JavaScript engine not initialized");
    JSValue r = JS_Eval(g_ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);
    DLC_Value* v = js_to_dlc(g_ctx, r);
    JS_FreeValue(g_ctx, r);
    return v;
}

static DLC_Value* dlc_js_exec(const char* code) {
    return dlc_js_eval(code); /* 在 JS 中 eval 和 exec 相同 */
}

/* ─── 函数调用 ──────────────────────────────────────────────────── */

static DLC_Value* dlc_js_call(const char* func_name, DLC_Value** args, int argc) {
    if (!g_ctx) return dlc_error("JS_NOT_INIT", "JavaScript engine not initialized");

    /* 获取全局函数 */
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue func = JS_GetPropertyStr(g_ctx, global, func_name);
    JS_FreeValue(g_ctx, global);

    if (JS_IsUndefined(func)) {
        JS_FreeValue(g_ctx, func);
        char msg[256];
        snprintf(msg, sizeof(msg), "JS function '%s' not found", func_name);
        return dlc_error("JS_FUNC_NOT_FOUND", msg);
    }

    /* 构建参数 */
    JSValue js_args[16];
    int n = argc < 16 ? argc : 16;
    for (int i = 0; i < n; i++)
        js_args[i] = dlc_to_js(g_ctx, args[i]);

    JSValue result = JS_Call(g_ctx, func, JS_UNDEFINED, n, js_args);

    for (int i = 0; i < n; i++)
        JS_FreeValue(g_ctx, js_args[i]);
    JS_FreeValue(g_ctx, func);

    DLC_Value* v = js_to_dlc(g_ctx, result);
    JS_FreeValue(g_ctx, result);
    return v;
}

/* ─── 全局变量 ──────────────────────────────────────────────────── */

static int dlc_js_set_global(const char* name, DLC_Value* val) {
    if (!g_ctx) return -1;
    JSValue global = JS_GetGlobalObject(g_ctx);
    JS_SetPropertyStr(g_ctx, global, name, dlc_to_js(g_ctx, val));
    JS_FreeValue(g_ctx, global);
    return 0;
}

static DLC_Value* dlc_js_get_global(const char* name) {
    if (!g_ctx) return NULL;
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue val = JS_GetPropertyStr(g_ctx, global, name);
    JS_FreeValue(g_ctx, global);
    DLC_Value* result = js_to_dlc(g_ctx, val);
    JS_FreeValue(g_ctx, val);
    return result;
}

static void dlc_js_free_val(DLC_Value* val) { dlc_free(val); }

#else /* ─── 无 QuickJS: 使用 Node.js 管道通信 ──────────────────── */

static FILE* g_node_in = NULL;
static FILE* g_node_out = NULL;
static int   g_use_node = 0;

static int dlc_js_init(void* config) {
    (void)config;

    /* 尝试启动 Node.js 子进程 */
    const char* node_script =
        "const readline = require('readline');\n"
        "const rl = readline.createInterface({input: process.stdin});\n"
        "const registry = {};\n"
        "rl.on('line', (line) => {\n"
        "  try {\n"
        "    const req = JSON.parse(line);\n"
        "    let result;\n"
        "    if (req.op === 'eval') {\n"
        "      result = eval(req.code);\n"
        "    } else if (req.op === 'call') {\n"
        "      const fn = global[req.func];\n"
        "      result = fn ? fn.apply(null, req.args || []) : null;\n"
        "    } else if (req.op === 'exec') {\n"
        "      result = eval(req.code);\n"
        "    } else if (req.op === 'set') {\n"
        "      global[req.name] = req.value;\n"
        "      result = true;\n"
        "    } else if (req.op === 'get') {\n"
        "      result = global[req.name];\n"
        "    }\n"
        "    console.log(JSON.stringify({ok:true, result:result}));\n"
        "  } catch(e) {\n"
        "    console.log(JSON.stringify({ok:false, error:{code:'JS_ERR',message:e.message}}));\n"
        "  }\n"
        "});\n";

    g_node_in = popen("node -e '"  /* 简化: 直接用 stdin/stdout */
                      "const rl=require(\"readline\").createInterface({input:process.stdin});"
                      "rl.on(\"line\",l=>{try{"
                      "const r=JSON.parse(l);let v;"
                      "if(r.op===\"eval\")v=eval(r.code);"
                      "else if(r.op===\"call\"){const f=global[r.func];v=f?f.apply(null,r.args||[]):null;}"
                      "else if(r.op===\"exec\")v=eval(r.code);"
                      "console.log(JSON.stringify({ok:true,result:v}));"
                      "}catch(e){console.log(JSON.stringify({ok:false,error:e.message}));}});"
                      "'", "w");
    if (!g_node_in) {
        fprintf(stderr, "[DLC:JS] Node.js not available. Install node or QuickJS.\n");
        fprintf(stderr, "[DLC:JS] JavaScript DLC will run in stub mode.\n");
        return 0; /* 非致命错误 */
    }

    g_node_out = g_node_in; /* 双向管道 */
    g_use_node = 1;
    printf("[DLC:JS] Node.js subprocess connected\n");
    return 0;
}

static void dlc_js_finalize(void) {
    if (g_node_in) { pclose(g_node_in); g_node_in = NULL; }
    g_use_node = 0;
    printf("[DLC:JS] Finalized\n");
}

static DLC_Value* dlc_js_eval(const char* code) {
    if (!g_use_node) {
        return dlc_error("JS_NOT_AVAIL",
            "JavaScript runtime not available. Install Node.js or QuickJS.");
    }
    char req[4096];
    /* 转义 code 中的双引号 */
    char escaped[8192];
    int j = 0;
    for (int i = 0; code[i] && j < 8190; i++) {
        if (code[i] == '"') escaped[j++] = '\\';
        if (code[i] == '\n') escaped[j++] = '\\', escaped[j++] = 'n';
        else if (code[i] == '\r') { /* skip */ }
        else escaped[j++] = code[i];
    }
    escaped[j] = '\0';
    snprintf(req, sizeof(req), "{\"op\":\"eval\",\"code\":\"%s\"}\n", escaped);
    fputs(req, g_node_in);
    fflush(g_node_in);

    char resp[8192];
    if (!fgets(resp, sizeof(resp), g_node_in)) {
        return dlc_error("JS_PIPE_ERR", "Failed to read from Node.js");
    }

    /* 解析 JSON 响应 */
    /* 简化: 查找 "result": 后的内容 */
    char* rp = strstr(resp, "\"result\":");
    if (rp) {
        rp += 9;
        /* 跳过空白 */
        while (*rp == ' ' || *rp == ':') rp++;
        if (*rp == '"') {
            rp++;
            char* end = strchr(rp, '"');
            if (end) *end = '\0';
            return dlc_string(rp);
        } else if (strncmp(rp, "null", 4) == 0) {
            return dlc_null();
        } else if (strncmp(rp, "true", 4) == 0) {
            return dlc_bool(1);
        } else if (strncmp(rp, "false", 5) == 0) {
            return dlc_bool(0);
        } else {
            return dlc_string(rp); /* 返回原始字符串 */
        }
    }

    return dlc_null();
}

static DLC_Value* dlc_js_exec(const char* code) { return dlc_js_eval(code); }

static DLC_Value* dlc_js_call(const char* func_name, DLC_Value** args, int argc) {
    if (!g_use_node) {
        return dlc_error("JS_NOT_AVAIL", "JavaScript runtime not available.");
    }

    /* 构建 JSON 请求 */
    char req[8192];
    int pos = snprintf(req, sizeof(req),
        "{\"op\":\"call\",\"func\":\"%s\",\"args\":[", func_name);
    for (int i = 0; i < argc; i++) {
        if (i > 0) req[pos++] = ',';
        char* json = dlc_to_json(args[i]);
        pos += snprintf(req + pos, sizeof(req) - pos, "%s", json);
        free(json);
    }
    snprintf(req + pos, sizeof(req) - pos, "]}\n");
    fputs(req, g_node_in);
    fflush(g_node_in);

    char resp[8192];
    if (!fgets(resp, sizeof(resp), g_node_in)) {
        return dlc_error("JS_PIPE_ERR", "Failed to read from Node.js");
    }

    /* 简化解析 */
    char* okp = strstr(resp, "\"ok\":true");
    if (okp) {
        char* rp = strstr(resp, "\"result\":");
        if (rp) {
            rp += 9;
            while (*rp == ' ' || *rp == ':') rp++;
            if (*rp == '"') {
                rp++;
                char* end = strchr(rp, '"');
                if (end) *end = '\0';
                return dlc_string(rp);
            } else if (strncmp(rp, "null", 4) == 0) {
                return dlc_null();
            } else if (strncmp(rp, "true", 4) == 0) {
                return dlc_bool(1);
            } else if (strncmp(rp, "false", 5) == 0) {
                return dlc_bool(0);
            }
        }
        return dlc_null();
    }

    return dlc_error("JS_CALL_ERR", "JavaScript call failed");
}

static int dlc_js_set_global(const char* name, DLC_Value* val) {
    (void)name; (void)val;
    return -1;
}

static DLC_Value* dlc_js_get_global(const char* name) {
    (void)name;
    return NULL;
}

static void dlc_js_free_val(DLC_Value* val) { dlc_free(val); }

#endif /* DLC_USE_QUICKJS */

/* ─── 导出连接器 ────────────────────────────────────────────────── */

static DLC_Connector g_js_connector = {
    .language    = "javascript",
    .version     = "ES2020/QuickJS",
    .file_ext    = ".js",
    .description = "JavaScript connector via QuickJS (embedded) or Node.js (pipe)",
    .init        = dlc_js_init,
    .finalize    = dlc_js_finalize,
    .eval        = dlc_js_eval,
    .exec        = dlc_js_exec,
    .call        = dlc_js_call,
    .set_global  = dlc_js_set_global,
    .get_global  = dlc_js_get_global,
    .free_val    = dlc_js_free_val,
    .native_handle = NULL,
};

#ifdef __cplusplus
extern "C" {
#endif

DLC_Connector* dlc_get_connector(void) {
    return &g_js_connector;
}

#ifdef __cplusplus
}
#endif