/**
 * DLC 数据桥接测试套件 — 端点类型 / transform 模式 / 运行时能力
 *
 * 覆盖：
 *   端点：FILE / MEM / STRING / STDIO
 *   transform：CALLBACK / LANG / PIPELINE / JSON_RULE
 *   运行时：重试 / 异步
 *   向后兼容：旧 add_source/add_sink/set_transform_cb 接口
 */

#include "dlc_bridge.h"
#include "dlc_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 测试框架（沿用 test_dlc.c 风格） ─────────────────────────── */

static int TP = 0, TF = 0;  /* 通过数 / 失败数 */

#define TEST(name) static void name(void)
#define ASSERT(cond, msg) do { \
    if (cond) { TP++; } \
    else { TF++; printf("  ❌ FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* ─── 通用 transform 回调 ──────────────────────────────────────── */

/* 转大写 */
static DLC_Value* tf_upper(DLC_Value* input) {
    if (!input || input->type != DLC_ARRAY) return dlc_string("");
    int n = dlc_array_len(input);
    DLC_Value* out = dlc_array_new(n);
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING && s->data.string_val) {
            char* dup = strdup(s->data.string_val);
            for (char* p = dup; *p; p++) *p = toupper((unsigned char)*p);
            dlc_array_push(out, dlc_string(dup));
            free(dup);
        } else {
            dlc_array_push(out, dlc_string(""));
        }
    }
    return out;
}

/* 透传 */
static DLC_Value* tf_passthrough(DLC_Value* input) {
    if (!input) return dlc_string("");
    if (input->type == DLC_ARRAY) {
        int n = dlc_array_len(input);
        DLC_Value* out = dlc_array_new(n);
        for (int i = 0; i < n; i++) {
            DLC_Value* s = dlc_array_get(input, i);
            dlc_array_push(out, dlc_string(s && s->type == DLC_STRING ? s->data.string_val : ""));
        }
        return out;
    }
    return dlc_string("");
}

/* 拼接所有源为一个字符串 */
static DLC_Value* tf_join(DLC_Value* input) {
    if (!input || input->type != DLC_ARRAY) return dlc_string("");
    int n = dlc_array_len(input);
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING) total += strlen(s->data.string_val);
    }
    char* buf = malloc(total);
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING) strcat(buf, s->data.string_val);
    }
    DLC_Value* out = dlc_string(buf);
    free(buf);
    return out;
}

/* 失败计数器（用于重试测试）：前 N 次返回 NULL，之后成功 */
static int fail_counter = 0;
static int fail_threshold = 0;
static DLC_Value* tf_fail_then_pass(DLC_Value* input) {
    fail_counter++;
    if (fail_counter <= fail_threshold) {
        return NULL;  /* 模拟失败 */
    }
    return tf_join(input);  /* 成功 */
}

/* ─── pipeline stage ──────────────────────────────────────────── */

/* stage1：拼接所有源为单字符串 */
static DLC_Value* stage_join(DLC_Value* input) {
    return tf_join(input);
}

/* stage2：把字符串转大写 */
static DLC_Value* stage_upper(DLC_Value* input) {
    if (!input || input->type != DLC_STRING || !input->data.string_val) return dlc_string("");
    char* dup = strdup(input->data.string_val);
    for (char* p = dup; *p; p++) *p = toupper((unsigned char)*p);
    DLC_Value* out = dlc_string(dup);
    free(dup);
    return out;
}

/* stage3：添加后缀 */
static DLC_Value* stage_suffix(DLC_Value* input) {
    if (!input || input->type != DLC_STRING || !input->data.string_val) return dlc_string("");
    size_t len = strlen(input->data.string_val);
    char* buf = malloc(len + 8);
    snprintf(buf, len + 8, "%s [DONE]", input->data.string_val);
    DLC_Value* out = dlc_string(buf);
    free(buf);
    return out;
}

/* ─── 测试用例 ────────────────────────────────────────────────── */

/* 向后兼容：文件端点 + CALLBACK（原有用例，确认未破坏） */
TEST(test_callback_single) {
    printf("\n── callback 单源单目标（转大写）──\n");
    const char* src = "/tmp/dlc_b_test_src1.txt";
    const char* dst = "/tmp/dlc_b_test_dst1.txt";
    FILE* f = fopen(src, "w"); fputs("hello world 42", f); fclose(f);

    DLC_Bridge* b = dlc_bridge_create("cb-single");
    dlc_bridge_add_source(b, src, NULL);
    dlc_bridge_add_sink(b, dst, NULL);
    dlc_bridge_set_transform_cb(b, tf_upper);
    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 返回 1 个源");

    /* 读取结果验证 */
    f = fopen(dst, "r");
    char buf[256] = {0};
    size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
    (void)rd;
    fclose(f);
    printf("  目标内容: %s\n", buf);
    ASSERT(strstr(buf, "HELLO WORLD 42") != NULL, "结果含大写内容");

    dlc_bridge_destroy(b);
    remove(src); remove(dst);
}

/* 多目标文件 */
TEST(test_callback_multi_sink) {
    printf("\n── callback 单源多目标（拼接为字符串）──\n");
    const char* src = "/tmp/dlc_b_test_src2.txt";
    const char* d1 = "/tmp/dlc_b_test_dst2a.txt";
    const char* d2 = "/tmp/dlc_b_test_dst2b.txt";
    FILE* f = fopen(src, "w"); fputs("data-xyz", f); fclose(f);

    DLC_Bridge* b = dlc_bridge_create("cb-multi-sink");
    dlc_bridge_add_source(b, src, NULL);
    dlc_bridge_add_sink(b, d1, NULL);
    dlc_bridge_add_sink(b, d2, NULL);
    dlc_bridge_set_transform_cb(b, tf_join);  /* 返回 STRING，直接写入 */
    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 返回 1");

    char b1[64] = {0}, b2[64] = {0};
    f = fopen(d1, "r"); size_t r1 = fread(b1, 1, 63, f); fclose(f);
    f = fopen(d2, "r"); size_t r2 = fread(b2, 1, 63, f); fclose(f);
    (void)r1; (void)r2;
    ASSERT(strcmp(b1, "data-xyz") == 0, "目标1正确");
    ASSERT(strcmp(b2, "data-xyz") == 0, "目标2正确");

    dlc_bridge_destroy(b);
    remove(src); remove(d1); remove(d2);
}

/* 内存源端点 + 内存目标端点（不经过文件） */
TEST(test_mem_endpoints) {
    printf("\n── 内存源 → 内存目标（不经文件）──\n");
    DLC_Bridge* b = dlc_bridge_create("mem");
    dlc_bridge_add_source_str(b, "abc", NULL);
    dlc_bridge_add_source_str(b, "def", NULL);
    dlc_bridge_add_sink_mem(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_join);

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 2, "run 返回 2 个源");

    int len = 0;
    const char* data = dlc_bridge_get_sink_mem(b, 0, &len);
    printf("  内存目标内容: %.*s (len=%d)\n", len, data ? data : "", len);
    ASSERT(data != NULL, "get_sink_mem 非空");
    ASSERT(len == 6, "结果长度 6");
    ASSERT(strncmp(data, "abcdef", 6) == 0, "结果为 abcdef");

    dlc_bridge_destroy(b);
}

/* 字符串源 + 字符串目标 */
TEST(test_str_endpoints) {
    printf("\n── 字符串源 → 字符串目标（转大写）──\n");
    DLC_Bridge* b = dlc_bridge_create("str");
    dlc_bridge_add_source_str(b, "hello", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_upper);

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 返回 1");

    const char* s = dlc_bridge_get_sink_str(b, 0);
    printf("  字符串目标: %s\n", s ? s : "(null)");
    /* STRING 直接写入，结果为数组的 JSON 表示 */
    ASSERT(s != NULL, "get_sink_str 非空");

    dlc_bridge_destroy(b);
}

/* 文件源 + 内存目标（混合端点） */
TEST(test_mixed_endpoints) {
    printf("\n── 文件源 → 内存目标（混合端点）──\n");
    const char* src = "/tmp/dlc_b_test_mix.txt";
    FILE* f = fopen(src, "w"); fputs("mixed-data", f); fclose(f);

    DLC_Bridge* b = dlc_bridge_create("mixed");
    dlc_bridge_add_source_file(b, src, NULL);
    dlc_bridge_add_sink_mem(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_join);

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 返回 1");

    int len = 0;
    const char* data = dlc_bridge_get_sink_mem(b, 0, &len);
    printf("  结果: %.*s\n", len, data ? data : "");
    ASSERT(data != NULL && len > 0, "内存目标有数据");

    dlc_bridge_destroy(b);
    remove(src);
}

/* PIPELINE：3 个 stage 串联 */
TEST(test_pipeline) {
    printf("\n── pipeline 3 stage（拼接→大写→加后缀）──\n");
    DLC_Bridge* b = dlc_bridge_create("pipeline");
    dlc_bridge_add_source_str(b, "foo", NULL);
    dlc_bridge_add_source_str(b, "bar", NULL);
    dlc_bridge_add_sink_str(b, NULL);

    dlc_bridge_add_stage(b, stage_join);    /* foo+bar → "foobar" */
    dlc_bridge_add_stage(b, stage_upper);   /* → "FOOBAR" */
    dlc_bridge_add_stage(b, stage_suffix);  /* → "FOOBAR [DONE]" */

    ASSERT(dlc_bridge_mode(b) == DLC_BRIDGE_MODE_PIPELINE, "模式为 PIPELINE");

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 2, "run 返回 2 个源");

    const char* s = dlc_bridge_get_sink_str(b, 0);
    printf("  pipeline 结果: %s\n", s ? s : "(null)");
    ASSERT(s != NULL, "结果非空");

    dlc_bridge_destroy(b);
}

/* PIPELINE 单 stage */
TEST(test_pipeline_single) {
    printf("\n── pipeline 单 stage ──\n");
    DLC_Bridge* b = dlc_bridge_create("pipe1");
    dlc_bridge_add_source_str(b, "test", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_add_stage(b, stage_upper);

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 返回 1");
    ASSERT(dlc_bridge_error(b) == NULL, "无错误");

    dlc_bridge_destroy(b);
}

/* 重试：前 2 次失败，第 3 次成功 */
TEST(test_retry_success) {
    printf("\n── 重试：前2次失败第3次成功 ──\n");
    fail_counter = 0;
    fail_threshold = 2;

    DLC_Bridge* b = dlc_bridge_create("retry");
    dlc_bridge_add_source_str(b, "retry-data", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_fail_then_pass);
    dlc_bridge_set_retry(b, 3);  /* 最多重试 3 次 */

    int rc = dlc_bridge_run(b);
    printf("  调用次数=%d, rc=%d\n", fail_counter, rc);
    ASSERT(rc == 1, "重试后成功，返回 1");
    ASSERT(fail_counter == 3, "共调用 3 次（2失败+1成功）");

    dlc_bridge_destroy(b);
}

/* 重试：全部失败 */
TEST(test_retry_all_fail) {
    printf("\n── 重试：全部失败 ──\n");
    fail_counter = 0;
    fail_threshold = 999;  /* 永远失败 */

    DLC_Bridge* b = dlc_bridge_create("retry-fail");
    dlc_bridge_add_source_str(b, "data", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_fail_then_pass);
    dlc_bridge_set_retry(b, 2);

    int rc = dlc_bridge_run(b);
    printf("  rc=%d, 调用次数=%d, 错误=%s\n", rc, fail_counter, dlc_bridge_error(b));
    ASSERT(rc < 0, "全部失败返回 <0");
    ASSERT(fail_counter == 3, "调用 3 次（1+2重试）");
    ASSERT(dlc_bridge_error(b) != NULL, "有错误信息");

    dlc_bridge_destroy(b);
}

/* 异步执行 */
static int async_done = 0;
static int async_result_val = -999;
static void async_cb(DLC_Bridge* bridge, int result, void* user_data) {
    (void)bridge; (void)user_data;
    async_result_val = result;
    async_done = 1;
}

TEST(test_async) {
    printf("\n── 异步执行 ──\n");
    async_done = 0;
    async_result_val = -999;

    DLC_Bridge* b = dlc_bridge_create("async");
    dlc_bridge_add_source_str(b, "async-test", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_join);

    int rc = dlc_bridge_run_async(b, async_cb, NULL);
    ASSERT(rc == 0, "run_async 启动成功");

    int wait_result = dlc_bridge_wait(b);
    printf("  wait 结果=%d, async_done=%d\n", wait_result, async_done);
    ASSERT(async_done == 1, "回调被触发");
    ASSERT(wait_result == 1, "wait 返回 1 个源");
    ASSERT(async_result_val == 1, "回调收到 result=1");

    dlc_bridge_destroy(b);
}

/* LANG 模式语言未注册（回退错误） */
TEST(test_lang_unregistered) {
    printf("\n── LANG 模式未注册语言 ──\n");
    DLC_Bridge* b = dlc_bridge_create("lang-fail");
    dlc_bridge_add_source_str(b, "x", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_lang(b, "python", "bridge_output = bridge_input");
    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "未注册语言返回 <0");
    const char* err = dlc_bridge_error(b);
    printf("  错误: %s\n", err ? err : "(null)");
    ASSERT(err != NULL, "有错误信息");
    ASSERT(strstr(err, "python") != NULL, "错误信息含语言名");

    dlc_bridge_destroy(b);
}

/* JSON_RULE 模式无 JS 连接器（错误） */
TEST(test_json_rule_no_js) {
    printf("\n── JSON_RULE 无 JS 连接器 ──\n");
    DLC_Bridge* b = dlc_bridge_create("rule-nojs");
    dlc_bridge_add_source_str(b, "data", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_rule(b, "{\"op\":\"field\",\"name\":\"x\"}");
    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "无 JS 连接器返回 <0");
    const char* err = dlc_bridge_error(b);
    printf("  错误: %s\n", err ? err : "(null)");
    ASSERT(err != NULL, "有错误信息");

    dlc_bridge_destroy(b);
}

/* 未设置 transform */
TEST(test_error_no_transform) {
    printf("\n── 未设置 transform ──\n");
    DLC_Bridge* b = dlc_bridge_create("no-tf");
    dlc_bridge_add_source_str(b, "x", NULL);
    dlc_bridge_add_sink_str(b, NULL);
    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "未设 transform 返回 <0");
    ASSERT(dlc_bridge_error(b) != NULL, "有错误信息");

    dlc_bridge_destroy(b);
}

/* 无源端点 */
TEST(test_error_no_source) {
    printf("\n── 无源端点 ──\n");
    DLC_Bridge* b = dlc_bridge_create("no-src");
    dlc_bridge_add_sink_str(b, NULL);
    dlc_bridge_set_transform_cb(b, tf_passthrough);
    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "无源返回 <0");

    dlc_bridge_destroy(b);
}

/* 端点数上限 */
TEST(test_endpoint_limit) {
    printf("\n── 端点数上限 ──\n");
    DLC_Bridge* b = dlc_bridge_create("limit");
    int ok = 0;
    for (int i = 0; i < DLC_BRIDGE_MAX_ENDPOINTS + 2; i++) {
        if (dlc_bridge_add_source_str(b, "x", NULL) == 0) ok++;
    }
    printf("  成功添加 %d 个源（上限 %d）\n", ok, DLC_BRIDGE_MAX_ENDPOINTS);
    ASSERT(ok == DLC_BRIDGE_MAX_ENDPOINTS, "达上限后拒绝添加");

    dlc_bridge_destroy(b);
}

/* ─── 主入口 ──────────────────────────────────────────────────── */

int main(void) {
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  DLC 数据桥接测试套件                            ║\n");
    printf("║  端点类型 / transform 模式 / 运行时能力          ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    /* 向后兼容 + 文件端点 */
    test_callback_single();
    test_callback_multi_sink();

    /* 新端点类型 */
    test_mem_endpoints();
    test_str_endpoints();
    test_mixed_endpoints();

    /* PIPELINE */
    test_pipeline();
    test_pipeline_single();

    /* 重试 */
    test_retry_success();
    test_retry_all_fail();

    /* 异步 */
    test_async();

    /* LANG / JSON_RULE 回退 */
    test_lang_unregistered();
    test_json_rule_no_js();

    /* 错误处理 */
    test_error_no_transform();
    test_error_no_source();
    test_endpoint_limit();

    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║  结果: %d/%d 通过 %s                          ║\n",
        TP, TP + TF, TF == 0 ? "✅" : "❌");
    printf("╚══════════════════════════════════════════════════╝\n");

    return TF ? 1 : 0;
}
