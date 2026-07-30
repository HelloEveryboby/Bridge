/**
 * Bridge 数据桥接示例 — 用 C 语言编写桥接逻辑，连接多个文件
 *
 * 演示"一个语言（C）编写的文件，连接两个或多个文件之间的数据桥接"：
 *   - 从 2 个源文件读取数据
 *   - 用 C 回调 transform 转换（去重 + 排序 + 加行号）
 *   - 写入 2 个目标文件（一份纯文本，一份 JSON）
 *
 * 编译: gcc -O2 -o bridge_demo bridge_demo.c -I../../dlc -L../../dlc/build -ldlc_core -ldl
 * 运行: LD_LIBRARY_PATH=../../dlc/build ./bridge_demo
 */
#include "dlc_bridge.h"
#include "dlc_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── transform 回调：把多源文本行去重排序后加行号 ──────────────── */
/* 输入: DLC_ARRAY，每项为一个源文件的 DLC_STRING 内容 */
/* 输出: DLC_STRING，合并处理后的文本 */
static DLC_Value* transform_dedup_sort(DLC_Value* input) {
    if (!input || input->type != DLC_ARRAY) {
        return dlc_string("");
    }

    /* 1. 收集所有行 */
    int n = dlc_array_len(input);
    char** lines = NULL;
    int line_count = 0;
    int line_cap = 0;

    for (int i = 0; i < n; i++) {
        DLC_Value* src = dlc_array_get(input, i);
        if (!src || src->type != DLC_STRING || !src->data.string_val) continue;
        /* 按行切分 */
        const char* p = src->data.string_val;
        const char* line_start = p;
        while (*p) {
            if (*p == '\n') {
                int len = (int)(p - line_start);
                if (len > 0) {
                    if (line_count >= line_cap) {
                        line_cap = line_cap ? line_cap * 2 : 16;
                        lines = realloc(lines, line_cap * sizeof(char*));
                    }
                    lines[line_count] = malloc(len + 1);
                    memcpy(lines[line_count], line_start, len);
                    lines[line_count][len] = '\0';
                    line_count++;
                }
                line_start = p + 1;
            }
            p++;
        }
        /* 末尾无换行的剩余行 */
        if (*line_start) {
            int len = (int)(p - line_start);
            if (line_count >= line_cap) {
                line_cap = line_cap ? line_cap * 2 : 16;
                lines = realloc(lines, line_cap * sizeof(char*));
            }
            lines[line_count] = malloc(len + 1);
            memcpy(lines[line_count], line_start, len);
            lines[line_count][len] = '\0';
            line_count++;
        }
    }

    /* 2. 去重（保留首次出现） */
    int unique_count = 0;
    for (int i = 0; i < line_count; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(lines[i], lines[j]) == 0) { dup = 1; break; }
        }
        if (!dup) {
            if (unique_count != i) lines[unique_count] = lines[i];
            unique_count++;
        } else {
            free(lines[i]);
        }
    }
    line_count = unique_count;

    /* 3. 字典序排序 */
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = i + 1; j < line_count; j++) {
            if (strcmp(lines[i], lines[j]) > 0) {
                char* t = lines[i]; lines[i] = lines[j]; lines[j] = t;
            }
        }
    }

    /* 4. 加行号拼接输出 */
    /* 估算缓冲区大小（预留头部 + 每行行号开销） */
    size_t total = 128;
    for (int i = 0; i < line_count; i++) total += strlen(lines[i]) + 16;
    char* out = malloc(total);
    size_t pos = 0;
    pos += snprintf(out + pos, total - pos, "# 桥接结果：去重 + 排序 + 行号（共 %d 行）\n", line_count);
    for (int i = 0; i < line_count; i++) {
        pos += snprintf(out + pos, total - pos, "%3d. %s\n", i + 1, lines[i]);
        free(lines[i]);
    }
    free(lines);

    DLC_Value* result = dlc_string(out);
    free(out);
    return result;
}

/* ── transform 回调：把输入转成 JSON 数组（演示非 STRING 输出） ──── */
static DLC_Value* transform_to_json(DLC_Value* input) {
    if (!input || input->type != DLC_ARRAY) return dlc_array_new(0);
    /* 直接把每个源文件内容作为字符串元素放入新数组 */
    DLC_Value* arr = dlc_array_new(dlc_array_len(input));
    for (int i = 0; i < dlc_array_len(input); i++) {
        DLC_Value* src = dlc_array_get(input, i);
        if (src && src->type == DLC_STRING) {
            dlc_array_push(arr, dlc_string(src->data.string_val));
        }
    }
    return arr;
}

int main(void) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Bridge 数据桥接示例\n");
    printf("  用 C 语言编写桥接逻辑，连接多个文件之间的数据\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    /* 准备示例源文件 */
    const char* src_a = "/tmp/bridge_demo_src_a.txt";
    const char* src_b = "/tmp/bridge_demo_src_b.txt";
    FILE* fa = fopen(src_a, "w");
    FILE* fb = fopen(src_b, "w");
    fprintf(fa, "banana\napple\ncherry\napple\n");      /* 含重复 apple */
    fprintf(fb, "date\napple\nelderberry\n");            /* 又一个 apple */
    fclose(fa); fclose(fb);
    printf("源文件 A: %s\n源文件 B: %s\n\n", src_a, src_b);

    /* ── 桥接 1：去重排序 → 写入文本目标 ── */
    printf("── 桥接 1：去重 + 排序 + 行号 → 文本目标 ──\n");
    DLC_Bridge* b1 = dlc_bridge_create("dedup-sort");
    dlc_bridge_add_source(b1, src_a, NULL);
    dlc_bridge_add_source(b1, src_b, NULL);
    dlc_bridge_add_sink(b1, "/tmp/bridge_demo_out_text.txt", NULL);
    dlc_bridge_set_transform_cb(b1, transform_dedup_sort);

    int rc = dlc_bridge_run(b1);
    if (rc < 0) {
        printf("❌ 桥接失败: %s\n", dlc_bridge_error(b1));
    } else {
        printf("✅ 处理 %d 个源文件，已写入 /tmp/bridge_demo_out_text.txt\n", rc);
        /* 显示结果 */
        FILE* f = fopen("/tmp/bridge_demo_out_text.txt", "r");
        char buf[512];
        printf("── 目标文件内容 ──\n");
        while (fgets(buf, sizeof(buf), f)) printf("  %s", buf);
        fclose(f);
    }
    dlc_bridge_destroy(b1);

    /* ── 桥接 2：转 JSON → 写入 JSON 目标 ── */
    printf("\n── 桥接 2：转 JSON 数组 → JSON 目标 ──\n");
    DLC_Bridge* b2 = dlc_bridge_create("to-json");
    dlc_bridge_add_source(b2, src_a, NULL);
    dlc_bridge_add_source(b2, src_b, NULL);
    dlc_bridge_add_sink(b2, "/tmp/bridge_demo_out_json.json", NULL);
    dlc_bridge_set_transform_cb(b2, transform_to_json);

    rc = dlc_bridge_run(b2);
    if (rc < 0) {
        printf("❌ 桥接失败: %s\n", dlc_bridge_error(b2));
    } else {
        printf("✅ 处理 %d 个源文件，已写入 /tmp/bridge_demo_out_json.json\n", rc);
        FILE* f = fopen("/tmp/bridge_demo_out_json.json", "r");
        char buf[512];
        printf("── 目标文件内容 ──\n");
        while (fgets(buf, sizeof(buf), f)) printf("  %s", buf);
        fclose(f);
    }
    dlc_bridge_destroy(b2);

    /* 清理示例文件 */
    remove(src_a); remove(src_b);
    remove("/tmp/bridge_demo_out_text.txt");
    remove("/tmp/bridge_demo_out_json.json");

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  示例完成：单语言(C)桥接文件连接了 2 源 → 2 目标\n");
    printf("═══════════════════════════════════════════════════════\n");
    return 0;
}
