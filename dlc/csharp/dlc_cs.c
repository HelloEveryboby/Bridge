/**
 * DLC C# 连接器 (.NET Hosting API)
 *
 * 使用 .NET nethost/hostfxr API 托管 .NET 运行时。
 * 需要 .NET SDK 6.0+ 安装。
 *
 * 编译:
 *   gcc -shared -fPIC -O2 -o libdlc_cs.so dlc_cs.c \
 *       -I/usr/share/dotnet/packs/Microsoft.NETCore.App.Host.linux-x64/.../runtime \
 *       -lnethost -ldl
 *
 * 备选方案: 使用 Mono 嵌入 (libmono) 或通过管道与 dotnet-script 通信。
 */

#include "../dlc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>

/* ─── 内部状态 ──────────────────────────────────────────────────── */

static FILE* g_cs_process = NULL;  /* dotnet-script 子进程管道 */
static int   g_cs_use_host = 0;    /* 是否使用 nethost */
static int   g_cs_counter = 0;

/* ─── .NET Hosting API 函数指针 (条件编译) ──────────────────────── */

#ifdef DLC_USE_NETHOST
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

static hostfxr_initialize_for_runtime_config_fn g_init_fxr = NULL;
static hostfxr_get_runtime_delegate_fn g_get_delegate = NULL;
static hostfxr_close_fn g_close_fxr = NULL;
static load_assembly_and_get_function_pointer_fn g_load_asm = NULL;
static hostfxr_handle g_host_ctx = NULL;
#endif

/* ─── 初始化 ────────────────────────────────────────────────────── */

static int dlc_cs_init(void* config) {
    (void)config;

#ifdef DLC_USE_NETHOST
    /* 加载 hostfxr */
    char hostfxr_path[512];
    size_t buf_size = sizeof(hostfxr_path);
    int rc = get_hostfxr_path(hostfxr_path, &buf_size, NULL);
    if (rc != 0) {
        fprintf(stderr, "[DLC:CS] get_hostfxr_path failed: %d\n", rc);
        goto fallback;
    }

    void* fxr = dlopen(hostfxr_path, RTLD_NOW);
    if (!fxr) {
        fprintf(stderr, "[DLC:CS] dlopen hostfxr failed: %s\n", dlerror());
        goto fallback;
    }

    g_init_fxr = (hostfxr_initialize_for_runtime_config_fn)dlsym(fxr, "hostfxr_initialize_for_runtime_config");
    g_get_delegate = (hostfxr_get_runtime_delegate_fn)dlsym(fxr, "hostfxr_get_runtime_delegate");
    g_close_fxr = (hostfxr_close_fn)dlsym(fxr, "hostfxr_close");

    if (!g_init_fxr || !g_get_delegate || !g_close_fxr) {
        fprintf(stderr, "[DLC:CS] Failed to resolve hostfxr symbols\n");
        dlclose(fxr);
        goto fallback;
    }

    /* 初始化运行时 (使用当前目录的 .runtimeconfig.json) */
    rc = g_init_fxr("dlc_cs.runtimeconfig.json", NULL, &g_host_ctx);
    if (rc != 0) {
        fprintf(stderr, "[DLC:CS] hostfxr_initialize failed: %d\n", rc);
        goto fallback;
    }

    rc = g_get_delegate(g_host_ctx, hdt_load_assembly_and_get_function_pointer,
                        (void**)&g_load_asm);
    if (rc != 0) {
        fprintf(stderr, "[DLC:CS] get_delegate failed: %d\n", rc);
        g_close_fxr(g_host_ctx);
        goto fallback;
    }

    g_cs_use_host = 1;
    printf("[DLC:CS] .NET Host initialized\n");
    return 0;

fallback:
#endif
    /* 备选方案: 使用 dotnet-script 或 csi 管道通信 */
    /* 尝试启动 dotnet-script */
    const char* cs_agent =
        "using System;\n"
        "using System.Text.Json;\n"
        "using System.Reflection;\n"
        "using Microsoft.CodeAnalysis.CSharp.Scripting;\n"
        "using Microsoft.CodeAnalysis.Scripting;\n"
        "\n"
        "var options = ScriptOptions.Default"
        ".WithImports(\"System\", \"System.Linq\", \"System.Collections.Generic\");\n"
        "var globals = new Dictionary<string, object>();\n"
        "\n"
        "while (true) {\n"
        "    var line = Console.ReadLine();\n"
        "    if (line == null) break;\n"
        "    try {\n"
        "        var req = JsonSerializer.Deserialize<Request>(line);\n"
        "        object result = null;\n"
        "        if (req.op == \"eval\") {\n"
        "            result = await CSharpScript.EvaluateAsync(req.code, options, globals: globals.GetType());\n"
        "        } else if (req.op == \"exec\") {\n"
        "            var state = await CSharpScript.RunAsync(req.code, options);\n"
        "            result = state.ReturnValue;\n"
        "        }\n"
        "        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, result }));\n"
        "    } catch (Exception ex) {\n"
        "        Console.WriteLine(JsonSerializer.Serialize(new { ok = false, error = ex.Message }));\n"
        "    }\n"
        "}\n"
        "record Request(string op, string code);\n";

    /* 简化: 直接使用 csi (C# Interactive) */
    FILE* p = popen("csi 2>/dev/null || dotnet-script 2>/dev/null", "w");
    if (!p) {
        fprintf(stderr, "[DLC:CS] .NET runtime not available. Install .NET SDK or Mono.\n");
        fprintf(stderr, "[DLC:CS] C# DLC will run in stub mode.\n");
        return 0;
    }
    g_cs_process = p;
    printf("[DLC:CS] .NET script engine connected\n");
    return 0;
}

/* ─── 终止 ──────────────────────────────────────────────────────── */

static void dlc_cs_finalize(void) {
#ifdef DLC_USE_NETHOST
    if (g_host_ctx) {
        g_close_fxr(g_host_ctx);
        g_host_ctx = NULL;
    }
#endif
    if (g_cs_process) {
        pclose(g_cs_process);
        g_cs_process = NULL;
    }
    g_cs_use_host = 0;
    printf("[DLC:CS] Finalized\n");
}

/* ─── 辅助: 发送请求到 C# 运行时 ────────────────────────────────── */

static DLC_Value* cs_send(const char* op, const char* code) {
    if (!g_cs_process) {
        return dlc_error("CS_NOT_AVAIL",
            "C# runtime not available. Install .NET SDK or Mono.");
    }

    /* 转义 code */
    char escaped[8192];
    int j = 0;
    for (int i = 0; code[i] && j < 8190; i++) {
        if (code[i] == '"') escaped[j++] = '\\';
        if (code[i] == '\\') escaped[j++] = '\\';
        if (code[i] == '\n') { escaped[j++] = '\\'; escaped[j++] = 'n'; }
        else if (code[i] == '\r') {}
        else escaped[j++] = code[i];
    }
    escaped[j] = '\0';

    char req[16384];
    snprintf(req, sizeof(req),
        "{\"op\":\"%s\",\"code\":\"%s\"}\n", op, escaped);
    fputs(req, g_cs_process);
    fflush(g_cs_process);

    char resp[16384];
    if (!fgets(resp, sizeof(resp), g_cs_process)) {
        return dlc_error("CS_PIPE_ERR", "Failed to read from C# runtime");
    }

    /* 解析 JSON 响应 */
    char* okp = strstr(resp, "\"ok\":true");
    if (okp) {
        char* rp = strstr(resp, "\"result\":");
        if (rp) {
            rp += 9;
            while (*rp == ' ' || *rp == ':') rp++;
            if (*rp == '"') {
                rp++;
                char* end = strrchr(rp, '"');
                if (end) *end = '\0';
                return dlc_string(rp);
            }
            if (strncmp(rp, "null", 4) == 0) return dlc_null();
            if (strncmp(rp, "true", 4) == 0) return dlc_bool(1);
            if (strncmp(rp, "false", 5) == 0) return dlc_bool(0);
            /* 尝试解析数字 */
            char* endp;
            long lv = strtol(rp, &endp, 10);
            if (*endp == ',' || *endp == '}' || *endp == '\n') return dlc_int(lv);
            double dv = strtod(rp, &endp);
            if (*endp == ',' || *endp == '}' || *endp == '\n') return dlc_double(dv);
            return dlc_string(rp);
        }
        return dlc_null();
    }

    char* errp = strstr(resp, "\"error\":\"");
    if (errp) {
        errp += 9;
        char* end = strchr(errp, '"');
        if (end) *end = '\0';
        return dlc_error("CS_EVAL_ERR", errp);
    }

    return dlc_error("CS_EVAL_ERR", "Unknown C# evaluation error");
}

/* ─── 执行 ──────────────────────────────────────────────────────── */

static DLC_Value* dlc_cs_eval(const char* code) {
    return cs_send("eval", code);
}

static DLC_Value* dlc_cs_exec(const char* code) {
    return cs_send("exec", code);
}

/* ─── 函数调用 ──────────────────────────────────────────────────── */

static DLC_Value* dlc_cs_call(const char* func_name, DLC_Value** args, int argc) {
    /* 构建 C# 调用表达式 */
    char code[4096];
    int pos = snprintf(code, sizeof(code), "%s(", func_name);
    for (int i = 0; i < argc; i++) {
        if (i > 0) code[pos++] = ',';
        char* json = dlc_to_json(args[i]);
        pos += snprintf(code + pos, sizeof(code) - pos, "%s", json);
        free(json);
    }
    snprintf(code + pos, sizeof(code) - pos, ")");
    return cs_send("eval", code);
}

/* ─── 全局变量 ──────────────────────────────────────────────────── */

static int dlc_cs_set_global(const char* name, DLC_Value* val) {
    char code[4096];
    char* json = dlc_to_json(val);
    snprintf(code, sizeof(code), "global %s = %s", name, json);
    free(json);
    DLC_Value* r = dlc_cs_eval(code);
    int ok = (r && r->type != DLC_ERROR);
    dlc_free(r);
    return ok ? 0 : -1;
}

static DLC_Value* dlc_cs_get_global(const char* name) {
    return dlc_cs_eval(name);
}

static void dlc_cs_free_val(DLC_Value* val) { dlc_free(val); }

/* ─── 导出连接器 ────────────────────────────────────────────────── */

static DLC_Connector g_cs_connector = {
    .language    = "csharp",
    .version     = ".NET 6.0+",
    .file_ext    = ".cs",
    .description = "C# connector via .NET Hosting API / dotnet-script",
    .init        = dlc_cs_init,
    .finalize    = dlc_cs_finalize,
    .eval        = dlc_cs_eval,
    .exec        = dlc_cs_exec,
    .call        = dlc_cs_call,
    .set_global  = dlc_cs_set_global,
    .get_global  = dlc_cs_get_global,
    .free_val    = dlc_cs_free_val,
    .native_handle = NULL,
};

#ifdef __cplusplus
extern "C" {
#endif

DLC_Connector* dlc_get_connector(void) {
    return &g_cs_connector;
}

#ifdef __cplusplus
}
#endif