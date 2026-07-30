/**
 * DLC 数据桥接测试套件
 *
 * 测试:
 *   - C 回调 transform（单源单目标 / 单源多目标 / 多源单目标）
 *   - LANG 模式未注册语言回退
 *   - 未设置 transform 错误
 *
 * 编译: make test_bridge
 * 运行: ./build/test_bridge
 */

#include "dlc_bridge.h"
#include "dlc_protocol.h"
#include "dlc_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── 测试框架 ──────────────────────────────────────────────────── */

static int TR = 0, TP = 0, TF = 0;

#define TEST(n)        do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c,m)    do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;} } while(0)
#define PASS()         do { printf("  ✅ PASS\n"); TP++; } while(0)

/* ─── 文件辅助 ──────────────────────────────────────────────────── */

/* 写文件（覆盖）。成功返回 0 */
static int write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* 读文件（调用方负责 free）。失败返回 NULL */
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* ─── transform 回调 ────────────────────────────────────────────── */

/* 大写转换：把输入数组中所有字符串元素大写后拼接为单个字符串 */
static DLC_Value* cb_to_upper(DLC_Value* input) {
    int n = dlc_array_len(input);
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING && s->data.string_val)
            total += strlen(s->data.string_val);
    }
    char* buf = (char*)malloc(total);
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING && s->data.string_val) {
            for (const char* p = s->data.string_val; *p; p++)
                buf[pos++] = toupper((unsigned char)*p);
        }
    }
    buf[pos] = '\0';
    DLC_Value* r = dlc_string(buf);
    free(buf);
    return r;
}

/* 原样透传：返回第一个源文件内容 */
static DLC_Value* cb_passthrough(DLC_Value* input) {
    int n = dlc_array_len(input);
    if (n == 0) return dlc_string("");
    DLC_Value* s = dlc_array_get(input, 0);
    if (s && s->type == DLC_STRING && s->data.string_val)
        return dlc_string(s->data.string_val);
    return dlc_string("");
}

/* 拼接：把所有源文件内容顺序拼接为单个字符串 */
static DLC_Value* cb_join(DLC_Value* input) {
    int n = dlc_array_len(input);
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING && s->data.string_val)
            total += strlen(s->data.string_val);
    }
    char* buf = (char*)malloc(total);
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        DLC_Value* s = dlc_array_get(input, i);
        if (s && s->type == DLC_STRING && s->data.string_val) {
            size_t len = strlen(s->data.string_val);
            memcpy(buf + pos, s->data.string_val, len);
            pos += len;
        }
    }
    buf[pos] = '\0';
    DLC_Value* r = dlc_string(buf);
    free(buf);
    return r;
}

/* ─── 测试用例 ──────────────────────────────────────────────────── */

/* 1 源 → 1 目标，C 回调转大写 */
static void test_callback_single(void) {
    TEST("callback 单源单目标（转大写）");

    const char* src = "/tmp/dlc_bridge_test_src1.txt";
    const char* dst = "/tmp/dlc_bridge_test_dst1.txt";

    ASSERT(write_file(src, "hello world 42") == 0, "写源文件失败");

    DLC_Bridge* b = dlc_bridge_create("upper");
    ASSERT(b != NULL, "创建桥接器失败");
    ASSERT(dlc_bridge_add_source(b, src, NULL) == 0, "添加源失败");
    ASSERT(dlc_bridge_add_sink(b, dst, NULL) == 0, "添加目标失败");
    ASSERT(dlc_bridge_set_transform_cb(b, cb_to_upper) == 0, "设置回调失败");

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 应返回 1（源文件数）");
    ASSERT(dlc_bridge_error(b) == NULL, "不应有错误");

    char* out = read_file(dst);
    ASSERT(out != NULL, "读取目标文件失败");
    ASSERT(strcmp(out, "HELLO WORLD 42") == 0, "目标内容应为大写");
    printf("  目标内容: %s\n", out);
    free(out);

    dlc_bridge_destroy(b);
    remove(src); remove(dst);
    PASS();
}

/* 1 源 → 2 目标，C 回调原样透传 */
static void test_callback_multi_sink(void) {
    TEST("callback 单源多目标（透传）");

    const char* src  = "/tmp/dlc_bridge_test_src2.txt";
    const char* dst1 = "/tmp/dlc_bridge_test_dst2a.txt";
    const char* dst2 = "/tmp/dlc_bridge_test_dst2b.txt";

    ASSERT(write_file(src, "passthrough content") == 0, "写源文件失败");

    DLC_Bridge* b = dlc_bridge_create("pass");
    ASSERT(b != NULL, "创建桥接器失败");
    ASSERT(dlc_bridge_add_source(b, src, NULL) == 0, "添加源失败");
    ASSERT(dlc_bridge_add_sink(b, dst1, NULL) == 0, "添加目标1失败");
    ASSERT(dlc_bridge_add_sink(b, dst2, NULL) == 0, "添加目标2失败");
    ASSERT(dlc_bridge_set_transform_cb(b, cb_passthrough) == 0, "设置回调失败");

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 1, "run 应返回 1");

    char* o1 = read_file(dst1);
    char* o2 = read_file(dst2);
    ASSERT(o1 && strcmp(o1, "passthrough content") == 0, "目标1内容应等于源");
    ASSERT(o2 && strcmp(o2, "passthrough content") == 0, "目标2内容应等于源");
    printf("  目标1: %s\n  目标2: %s\n", o1, o2);
    free(o1); free(o2);

    dlc_bridge_destroy(b);
    remove(src); remove(dst1); remove(dst2);
    PASS();
}

/* 2 源 → 1 目标，C 回调拼接 */
static void test_callback_multi_source(void) {
    TEST("callback 多源单目标（拼接）");

    const char* src1 = "/tmp/dlc_bridge_test_src3a.txt";
    const char* src2 = "/tmp/dlc_bridge_test_src3b.txt";
    const char* dst  = "/tmp/dlc_bridge_test_dst3.txt";

    ASSERT(write_file(src1, "AAA") == 0, "写源1失败");
    ASSERT(write_file(src2, "BBB") == 0, "写源2失败");

    DLC_Bridge* b = dlc_bridge_create("join");
    ASSERT(b != NULL, "创建桥接器失败");
    ASSERT(dlc_bridge_add_source(b, src1, NULL) == 0, "添加源1失败");
    ASSERT(dlc_bridge_add_source(b, src2, NULL) == 0, "添加源2失败");
    ASSERT(dlc_bridge_add_sink(b, dst, NULL) == 0, "添加目标失败");
    ASSERT(dlc_bridge_set_transform_cb(b, cb_join) == 0, "设置回调失败");

    int rc = dlc_bridge_run(b);
    ASSERT(rc == 2, "run 应返回 2（源文件数）");

    char* out = read_file(dst);
    ASSERT(out != NULL, "读取目标文件失败");
    ASSERT(strcmp(out, "AAABBB") == 0, "目标应包含两源拼接内容");
    printf("  目标内容: %s\n", out);
    free(out);

    dlc_bridge_destroy(b);
    remove(src1); remove(src2); remove(dst);
    PASS();
}

/* LANG 模式但语言未注册 → run 返回 <0 且 error 非空 */
static void test_lang_transform_fallback(void) {
    TEST("LANG 模式未注册语言回退");

    const char* src = "/tmp/dlc_bridge_test_src4.txt";
    const char* dst = "/tmp/dlc_bridge_test_dst4.txt";

    ASSERT(write_file(src, "data") == 0, "写源文件失败");

    DLC_Bridge* b = dlc_bridge_create("lang");
    ASSERT(b != NULL, "创建桥接器失败");
    ASSERT(dlc_bridge_add_source(b, src, NULL) == 0, "添加源失败");
    ASSERT(dlc_bridge_add_sink(b, dst, NULL) == 0, "添加目标失败");
    ASSERT(dlc_bridge_set_transform_lang(b, "python",
            "bridge_output = bridge_input") == 0, "设置 lang transform 失败");

    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "run 应返回 <0（语言未注册）");
    ASSERT(dlc_bridge_error(b) != NULL, "应有错误信息");
    printf("  错误信息: %s\n", dlc_bridge_error(b));

    dlc_bridge_destroy(b);
    remove(src); remove(dst);
    PASS();
}

/* 未设置 transform 直接 run → 返回 <0 报错 */
static void test_error_no_transform(void) {
    TEST("未设置 transform 错误");

    const char* src = "/tmp/dlc_bridge_test_src5.txt";
    const char* dst = "/tmp/dlc_bridge_test_dst5.txt";

    ASSERT(write_file(src, "data") == 0, "写源文件失败");

    DLC_Bridge* b = dlc_bridge_create("notrans");
    ASSERT(b != NULL, "创建桥接器失败");
    ASSERT(dlc_bridge_add_source(b, src, NULL) == 0, "添加源失败");
    ASSERT(dlc_bridge_add_sink(b, dst, NULL) == 0, "添加目标失败");
    /* 故意不设置 transform */

    int rc = dlc_bridge_run(b);
    ASSERT(rc < 0, "run 应返回 <0");
    ASSERT(dlc_bridge_error(b) != NULL, "应有错误信息");
    printf("  错误信息: %s\n", dlc_bridge_error(b));

    dlc_bridge_destroy(b);
    remove(src); remove(dst);
    PASS();
}

/* ─── 主入口 ────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   DLC 数据桥接 — 测试套件                    ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    test_callback_single();
    test_callback_multi_sink();
    test_callback_multi_source();
    test_lang_transform_fallback();
    test_error_no_transform();

    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  结果: %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    return TF ? 1 : 0;
}
