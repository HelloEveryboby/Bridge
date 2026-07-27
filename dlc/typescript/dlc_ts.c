/**
 * DLC TypeScript 连接器
 *
 * TypeScript 是 JavaScript 的超集。此连接器将 TS 代码转译为 JS 后
 * 在 JavaScript DLC 引擎上执行。
 *
 * 转译方式:
 *   1. 优先使用 swc (Rust 编写的超快 TS 编译器)
 *   2. 备选: 使用 esbuild
 *   3. 再备选: 使用 TypeScript 编译器 (tsc)
 *   4. 最终备选: 简单剥离类型注解 (适合简单场景)
 *
 * 编译:
 *   gcc -shared -fPIC -O2 -o libdlc_ts.so dlc_ts.c -ldl
 */

#include "../dlc_protocol.h"
#include "../dlc_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

/* ─── 内部状态 ──────────────────────────────────────────────────── */

static int   g_ts_mode = 0;       /* 0=简单剥离, 1=swc, 2=esbuild, 3=tsc */
static char  g_ts_compiler[256] = "";
static DLC_Connector* g_js_connector = NULL;  /* 指向 JS DLC */

/* ─── 简单 TypeScript → JavaScript 转译 ─────────────────────────── */

/**
 * 简单剥离 TypeScript 类型注解。
 * 移除: 类型注解 (: Type), 接口定义, 类型别名, 泛型参数, as 断言
 * 这不是完整的 TS 编译器，但可以处理 80% 的常见场景。
 */
static char* strip_ts_types(const char* ts_code) {
    size_t len = strlen(ts_code);
    char* js = (char*)malloc(len + 1024);
    int j = 0;
    int in_string = 0;
    int in_template = 0;
    char string_char = 0;
    int depth = 0;  /* 括号深度 */

    for (size_t i = 0; i < len; i++) {
        char c = ts_code[i];

        /* 字符串处理 */
        if (in_string) {
            js[j++] = c;
            if (c == string_char && (i == 0 || ts_code[i - 1] != '\\'))
                in_string = 0;
            continue;
        }
        if (in_template) {
            js[j++] = c;
            if (c == '`' && (i == 0 || ts_code[i - 1] != '\\'))
                in_template = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = 1;
            string_char = c;
            js[j++] = c;
            continue;
        }
        if (c == '`') {
            in_template = 1;
            js[j++] = c;
            continue;
        }

        /* 跳过类型注解 `: Type` */
        if (c == ':' && depth == 0 && j > 0 && (
            js[j - 1] == ')' || js[j - 1] == ']' ||
            (js[j - 1] >= 'a' && js[j - 1] <= 'z') ||
            (js[j - 1] >= 'A' && js[j - 1] <= 'Z') ||
            js[j - 1] == '_')) {
            /* 跳过到下一个 ',' 或 '=' 或 ')' 或 ';' 或 '{' 或 '\n' */
            while (i + 1 < len && ts_code[i + 1] != ',' &&
                   ts_code[i + 1] != '=' && ts_code[i + 1] != ')' &&
                   ts_code[i + 1] != ';' && ts_code[i + 1] != '{' &&
                   ts_code[i + 1] != '\n' && ts_code[i + 1] != '}') {
                i++;
            }
            /* 如果后面是 '=' 或 ')'，保持冒号 */
            if (i + 1 < len && (ts_code[i + 1] == '=' || ts_code[i + 1] == ')')) {
                js[j++] = c;
            }
            continue;
        }

        /* 跳过泛型参数 <T, U> */
        if (c == '<' && depth == 0 && j > 0 && (
            (js[j - 1] >= 'a' && js[j - 1] <= 'z') ||
            (js[j - 1] >= 'A' && js[j - 1] <= 'Z'))) {
            int gt_depth = 1;
            i++;
            while (i < len && gt_depth > 0) {
                if (ts_code[i] == '<') gt_depth++;
                else if (ts_code[i] == '>') gt_depth--;
                i++;
            }
            i--;  /* 回退到 '>' */
            continue;
        }

        /* 跳过 `as Type` 断言 */
        if (c == ' ' && i + 3 < len && strncmp(ts_code + i + 1, "as ", 3) == 0) {
            i += 3;  /* 跳过 "as " */
            while (i + 1 < len && ts_code[i + 1] != ';' &&
                   ts_code[i + 1] != ',' && ts_code[i + 1] != ')' &&
                   ts_code[i + 1] != '\n' && ts_code[i + 1] != '}') {
                i++;
            }
            continue;
        }

        /* 跳过接口定义 (interface Xxx { ... }) */
        if (i + 9 < len && (ts_code[i] == 'i' || ts_code[i] == 'I') &&
            strncmp(ts_code + i, "interface", 9) == 0 &&
            (i == 0 || ts_code[i - 1] == '\n' || ts_code[i - 1] == ' ' || ts_code[i - 1] == '\t' || ts_code[i - 1] == ';' || ts_code[i - 1] == '}')) {
            /* 找到 '{' 并跳过整个块 */
            i += 9;
            while (i < len && ts_code[i] != '{') i++;
            int brace_depth = 1;
            i++;
            while (i < len && brace_depth > 0) {
                if (ts_code[i] == '{') brace_depth++;
                else if (ts_code[i] == '}') brace_depth--;
                i++;
            }
            i--;
            /* 放一个换行保持行号 */
            js[j++] = '\n';
            continue;
        }

        /* 跳过类型别名 (type Xxx = ...;) */
        if (i + 4 < len && (ts_code[i] == 't' || ts_code[i] == 'T') &&
            strncmp(ts_code + i, "type ", 5) == 0 &&
            (i == 0 || ts_code[i - 1] == '\n' || ts_code[i - 1] == ' ' || ts_code[i - 1] == '\t' || ts_code[i - 1] == ';' || ts_code[i - 1] == '}')) {
            while (i < len && ts_code[i] != ';' && ts_code[i] != '\n') i++;
            js[j++] = '\n';
            continue;
        }

        /* 跳过 implements */
        if (i + 10 < len && strncmp(ts_code + i, "implements", 10) == 0) {
            while (i + 1 < len && ts_code[i + 1] != '{') i++;
            continue;
        }

        js[j++] = c;
    }
    js[j] = '\0';
    return js;
}

/* ─── 使用外部编译器转译 ────────────────────────────────────────── */

static char* transpile_external(const char* ts_code, const char* compiler) {
    /* 通过管道调用外部编译器 */
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/dlc_ts_%d.ts", getpid());

    FILE* f = fopen(tmpfile, "w");
    if (!f) return NULL;
    fputs(ts_code, f);
    fclose(f);

    char cmd[1024];
    char outfile[256];
    snprintf(outfile, sizeof(outfile), "/tmp/dlc_ts_%d.js", getpid());

    if (strcmp(compiler, "swc") == 0) {
        snprintf(cmd, sizeof(cmd),
            "npx -y @swc/cli compile %s --out-file %s --no-swcrc "
            "-C jsc.parser.syntax=typescript 2>/dev/null", tmpfile, outfile);
    } else if (strcmp(compiler, "esbuild") == 0) {
        snprintf(cmd, sizeof(cmd),
            "npx -y esbuild %s --outfile=%s --format=esm --loader=ts 2>/dev/null",
            tmpfile, outfile);
    } else {
        snprintf(cmd, sizeof(cmd),
            "npx -y tsc %s --outFile %s --module ESNext --target ES2020 2>/dev/null",
            tmpfile, outfile);
    }

    int rc = system(cmd);
    unlink(tmpfile);

    if (rc != 0) return NULL;

    /* 读取输出 */
    f = fopen(outfile, "r");
    if (!f) { unlink(outfile); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* js = (char*)malloc(size + 1);
    fread(js, 1, size, f);
    js[size] = '\0';
    fclose(f);
    unlink(outfile);
    return js;
}

/* ─── 转译入口 ──────────────────────────────────────────────────── */

static char* transpile_ts(const char* ts_code) {
    if (g_ts_mode >= 1) {
        char* result = transpile_external(ts_code, g_ts_compiler);
        if (result) return result;
        /* 外部编译器失败，降级 */
        fprintf(stderr, "[DLC:TS] External compiler failed, falling back to simple strip\n");
        g_ts_mode = 0;
    }
    return strip_ts_types(ts_code);
}

/* ─── 初始化/终止 ───────────────────────────────────────────────── */

static int dlc_ts_init(void* config) {
    (void)config;

    /* 查找 JS 连接器 */
    g_js_connector = dlc_find("javascript");
    if (!g_js_connector) {
        fprintf(stderr, "[DLC:TS] JavaScript DLC not found. "
                "TypeScript requires JavaScript runtime.\n");
        return -1;
    }

    /* 检测可用的 TS 编译器 */
    if (system("which npx >/dev/null 2>&1") == 0) {
        /* 检测 swc */
        if (system("npx -y @swc/cli --version >/dev/null 2>&1") == 0) {
            g_ts_mode = 1;
            strcpy(g_ts_compiler, "swc");
        }
    }
    if (g_ts_mode == 0) {
        printf("[DLC:TS] Using simple type-stripping transpiler (no external compiler)\n");
    } else {
        printf("[DLC:TS] TypeScript connector initialized (compiler: %s)\n", g_ts_compiler);
    }
    return 0;
}

static void dlc_ts_finalize(void) {
    g_js_connector = NULL;
    printf("[DLC:TS] Finalized\n");
}

/* ─── 执行 ──────────────────────────────────────────────────────── */

static DLC_Value* dlc_ts_eval(const char* code) {
    if (!g_js_connector)
        return dlc_error("TS_NO_JS", "JavaScript DLC not available");

    char* js = transpile_ts(code);
    if (!js)
        return dlc_error("TS_TRANSPILE_ERR", "Failed to transpile TypeScript");

    DLC_Value* result = g_js_connector->eval(js);
    free(js);
    return result;
}

static DLC_Value* dlc_ts_exec(const char* code) {
    if (!g_js_connector)
        return dlc_error("TS_NO_JS", "JavaScript DLC not available");

    char* js = transpile_ts(code);
    if (!js)
        return dlc_error("TS_TRANSPILE_ERR", "Failed to transpile TypeScript");

    DLC_Value* result = g_js_connector->exec(js);
    free(js);
    return result;
}

/* ─── 函数调用 ──────────────────────────────────────────────────── */

static DLC_Value* dlc_ts_call(const char* func_name, DLC_Value** args, int argc) {
    if (!g_js_connector)
        return dlc_error("TS_NO_JS", "JavaScript DLC not available");
    return g_js_connector->call(func_name, args, argc);
}

static int dlc_ts_set_global(const char* name, DLC_Value* val) {
    if (!g_js_connector) return -1;
    return g_js_connector->set_global(name, val);
}

static DLC_Value* dlc_ts_get_global(const char* name) {
    if (!g_js_connector) return NULL;
    return g_js_connector->get_global(name);
}

static void dlc_ts_free_val(DLC_Value* val) { dlc_free(val); }

/* ─── 导出连接器 ────────────────────────────────────────────────── */

static DLC_Connector g_ts_connector = {
    .language    = "typescript",
    .version     = "TS 5.x → JS",
    .file_ext    = ".ts",
    .description = "TypeScript connector via transpile-to-JS + JavaScript DLC",
    .init        = dlc_ts_init,
    .finalize    = dlc_ts_finalize,
    .eval        = dlc_ts_eval,
    .exec        = dlc_ts_exec,
    .call        = dlc_ts_call,
    .set_global  = dlc_ts_set_global,
    .get_global  = dlc_ts_get_global,
    .free_val    = dlc_ts_free_val,
    .native_handle = NULL,
};

#ifdef __cplusplus
extern "C" {
#endif

DLC_Connector* dlc_get_connector(void) {
    return &g_ts_connector;
}

#ifdef __cplusplus
}
#endif