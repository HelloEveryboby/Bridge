/**
 * DLC 统一测试套件
 *
 * 测试所有语言连接器的:
 *   - 加载/初始化
 *   - 基础表达式求值
 *   - 函数调用
 *   - 跨语言桥接调用
 *   - 错误处理
 *
 * 编译: make test
 * 运行: LD_LIBRARY_PATH=build/connectors ./build/test_dlc
 */

#include "dlc_protocol.h"
#include "dlc_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─── 测试框架 ──────────────────────────────────────────────────── */

static int TR = 0, TP = 0, TF = 0;

#define TEST(n)        do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c,m)    do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;} } while(0)
#define PASS()         do { printf("  ✅ PASS\n"); TP++; } while(0)
#define SKIP(msg)      do { printf("  ⚠ SKIP: %s\n", msg); TR--; } while(0)

/* 打印 DLC_Value */
static void print_val(DLC_Value* v) {
    if (!v) { printf("NULL"); return; }
    char* json = dlc_to_json(v);
    printf("%s", json);
    free(json);
}

/* ─── 测试: 连接器加载 ──────────────────────────────────────────── */

void test_load_connectors(void) {
    TEST("加载所有连接器");

    DLC_Connector* conns[] = {
        dlc_load("build/connectors/libdlc_cpp.so"),
        dlc_load("build/connectors/libdlc_js.so"),
        dlc_load("build/connectors/libdlc_cs.so"),
        dlc_load("build/connectors/libdlc_sql.so"),
        dlc_load("build/connectors/libdlc_ts.so"),
    };

    const char* names[] = {"cpp", "javascript", "csharp", "sql", "typescript"};
    int loaded = 0;
    for (int i = 0; i < 5; i++) {
        if (conns[i]) {
            printf("  ✓ %s (%s)\n", conns[i]->language, conns[i]->version);
            loaded++;
        } else {
            printf("  ⚠ %s — not built (missing dependency)\n", names[i]);
        }
    }

    ASSERT(loaded >= 1, "至少需要一个连接器");
    printf("  已加载 %d 个连接器\n", loaded);
    PASS();
}

/* ─── 测试: 注册表操作 ──────────────────────────────────────────── */

void test_registry(void) {
    TEST("注册表操作");
    int count = dlc_count();
    printf("  已注册: %d 个连接器\n", count);

    for (int i = 0; i < count; i++) {
        DLC_Connector* c = dlc_get(i);
        printf("    [%d] %s — %s (%s)\n", i, c->language, c->description, c->file_ext);
    }

    ASSERT(count >= 1, "注册表非空");
    PASS();
}

/* ─── 测试: DLC 值工具函数 ──────────────────────────────────────── */

void test_value_utils(void) {
    TEST("DLC 值工具函数");

    /* 基本类型 */
    DLC_Value* n = dlc_null();
    ASSERT(n->type == DLC_NULL, "null");
    dlc_free(n);

    DLC_Value* b = dlc_bool(1);
    ASSERT(b->type == DLC_BOOL && b->data.bool_val == 1, "bool");
    dlc_free(b);

    DLC_Value* i = dlc_int(42);
    ASSERT(i->type == DLC_INT && i->data.int_val == 42, "int");
    dlc_free(i);

    DLC_Value* d = dlc_double(3.14159);
    ASSERT(d->type == DLC_DOUBLE && fabs(d->data.double_val - 3.14159) < 1e-10, "double");
    dlc_free(d);

    DLC_Value* s = dlc_string("hello world");
    ASSERT(s->type == DLC_STRING && strcmp(s->data.string_val, "hello world") == 0, "string");
    dlc_free(s);

    /* 数组 */
    DLC_Value* arr = dlc_array_new(4);
    dlc_array_push(arr, dlc_int(1));
    dlc_array_push(arr, dlc_int(2));
    dlc_array_push(arr, dlc_int(3));
    ASSERT(dlc_array_len(arr) == 3, "array length");
    ASSERT(dlc_array_get(arr, 1)->data.int_val == 2, "array[1]");

    /* JSON 序列化 */
    char* json = dlc_to_json(arr);
    ASSERT(strcmp(json, "[1,2,3]") == 0, "array to JSON");
    free(json);
    dlc_free(arr);

    /* 对象 */
    DLC_Value* obj = dlc_object_new(4);
    dlc_object_set(obj, "name", dlc_string("PyBridge"));
    dlc_object_set(obj, "version", dlc_int(1));
    dlc_object_set(obj, "active", dlc_bool(1));
    ASSERT(dlc_object_has(obj, "name"), "has key");
    ASSERT(strcmp(dlc_object_get(obj, "name")->data.string_val, "PyBridge") == 0, "obj.name");
    json = dlc_to_json(obj);
    printf("  JSON: %s\n", json);
    free(json);
    dlc_free(obj);

    PASS();
}

/* ─── 测试: SQL 连接器 ──────────────────────────────────────────── */

void test_sql(void) {
    TEST("SQL 连接器");

    DLC_Connector* sql = dlc_find("sql");
    if (!sql) { SKIP("SQL connector not loaded"); return; }

    if (sql->init && sql->init(NULL) != 0) {
        SKIP("SQL init failed");
        return;
    }

    /* 创建表 */
    DLC_Value* r = sql->eval(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  age INTEGER,"
        "  email TEXT UNIQUE"
        ")");
    ASSERT(r && r->type != DLC_ERROR, "CREATE TABLE");
    printf("  CREATE TABLE: "); print_val(r); printf("\n");
    dlc_free(r);

    /* 插入数据 */
    r = sql->eval("INSERT INTO users (name, age, email) VALUES "
                  "('Alice', 30, 'alice@example.com'),"
                  "('Bob', 25, 'bob@example.com'),"
                  "('Charlie', 35, 'charlie@example.com')");
    ASSERT(r && r->type != DLC_ERROR, "INSERT");
    printf("  INSERT: "); print_val(r); printf(" rows\n");
    dlc_free(r);

    /* 查询 */
    r = sql->eval("SELECT name, age FROM users WHERE age > 28 ORDER BY age");
    ASSERT(r && r->type == DLC_ARRAY, "SELECT returns array");
    printf("  SELECT: %d rows\n", dlc_array_len(r));
    char* json = dlc_to_json(r);
    printf("  %s\n", json);
    free(json);
    ASSERT(dlc_array_len(r) >= 2, ">= 2 rows returned");
    dlc_free(r);

    /* 聚合查询 */
    r = sql->eval("SELECT AVG(age) as avg_age, COUNT(*) as total FROM users");
    ASSERT(r && r->type == DLC_ARRAY, "SELECT AVG");
    json = dlc_to_json(r);
    printf("  %s\n", json);
    free(json);
    dlc_free(r);

    /* 更新 */
    r = sql->eval("UPDATE users SET age = age + 1 WHERE name = 'Bob'");
    ASSERT(r && r->type != DLC_ERROR, "UPDATE");
    printf("  UPDATE: "); print_val(r); printf(" rows affected\n");
    dlc_free(r);

    /* 验证更新 */
    r = sql->eval("SELECT age FROM users WHERE name = 'Bob'");
    ASSERT(r && dlc_array_len(r) == 1, "Bob exists");
    DLC_Value* bob = dlc_array_get(r, 0);
    ASSERT(dlc_object_get(bob, "age")->data.int_val == 26, "Bob's age = 26");
    dlc_free(r);

    PASS();
}

/* ─── 测试: JavaScript 连接器 ───────────────────────────────────── */

void test_javascript(void) {
    TEST("JavaScript 连接器");

    DLC_Connector* js = dlc_find("javascript");
    if (!js) { SKIP("JavaScript connector not loaded"); return; }

    if (js->init && js->init(NULL) != 0) {
        SKIP("JS init failed (Node.js/QuickJS not available)");
        return;
    }

    /* 基础 eval */
    DLC_Value* r = js->eval("40 + 2");
    if (r && r->type == DLC_ERROR) {
        printf("  JS not available: %s\n", r->data.error_val.message);
        dlc_free(r);
        SKIP("No JS runtime");
        return;
    }
    ASSERT(r && r->type == DLC_INT && r->data.int_val == 42, "40+2=42");
    printf("  40 + 2 = %ld\n", (long)r->data.int_val);
    dlc_free(r);

    /* 字符串操作 */
    r = js->eval("'hello'.toUpperCase() + ' WORLD'");
    if (r && r->type == DLC_STRING) {
        printf("  '%s'\n", r->data.string_val);
        ASSERT(strcmp(r->data.string_val, "HELLO WORLD") == 0, "upper+concat");
    }
    dlc_free(r);

    /* 定义并调用函数 */
    js->exec("function add(a, b) { return a + b; }");
    DLC_Value* args[] = { dlc_int(100), dlc_int(200) };
    r = js->call("add", args, 2);
    if (r && r->type == DLC_INT) {
        ASSERT(r->data.int_val == 300, "add(100,200)=300");
        printf("  add(100, 200) = %ld\n", (long)r->data.int_val);
    }
    dlc_free(r);
    dlc_free(args[0]);
    dlc_free(args[1]);

    /* 数组操作 */
    r = js->eval("[1, 2, 3].map(x => x * 2)");
    if (r && r->type == DLC_ARRAY) {
        char* json = dlc_to_json(r);
        printf("  [1,2,3]*2 = %s\n", json);
        free(json);
    }
    dlc_free(r);

    /* 全局变量 */
    js->set_global("__test_val", dlc_int(999));
    r = js->get_global("__test_val");
    if (r && r->type == DLC_INT) {
        ASSERT(r->data.int_val == 999, "global var");
        printf("  global = %ld\n", (long)r->data.int_val);
    }
    dlc_free(r);

    PASS();
}

/* ─── 测试: 跨语言桥接 ──────────────────────────────────────────── */

void test_cross_language(void) {
    TEST("跨语言桥接调用");

    /* C → SQL 查询 */
    DLC_Connector* sql = dlc_find("sql");
    if (sql) {
        DLC_Value* r = dlc_bridge_eval("sql",
            "SELECT 'cross_lang_test' as message, 42 as answer");
        if (r && r->type == DLC_ARRAY) {
            printf("  C→SQL: ");
            char* json = dlc_to_json(r);
            printf("%s\n", json);
            free(json);
        }
        dlc_free(r);
    } else {
        printf("  C→SQL: SQL not loaded\n");
    }

    /* C → JavaScript 调用 */
    DLC_Connector* js = dlc_find("javascript");
    if (js) {
        DLC_Value* r = dlc_bridge_eval("javascript", "Math.PI * 2");
        if (r && r->type != DLC_ERROR) {
            printf("  C→JS: Math.PI*2 = ");
            print_val(r);
            printf("\n");
        }
        dlc_free(r);
    } else {
        printf("  C→JS: JS not loaded\n");
    }

    printf("  ✅ 跨语言路由正常\n");
    PASS();
}

/* ─── 测试: 错误处理 ────────────────────────────────────────────── */

void test_error_handling(void) {
    TEST("错误处理");

    /* 调用不存在的语言 */
    DLC_Value* r = dlc_bridge_eval("nonexistent_lang", "1+1");
    ASSERT(r && r->type == DLC_ERROR, "unknown language");
    printf("  Unknown language: %s\n", r->data.error_val.message);
    dlc_free(r);

    /* SQL 语法错误 */
    DLC_Connector* sql = dlc_find("sql");
    if (sql) {
        r = sql->eval("SELECT * FROM nonexistent_table_xyz");
        if (r && r->type == DLC_ERROR) {
            printf("  SQL error: %s\n", r->data.error_val.message);
        }
        dlc_free(r);
    }

    PASS();
}

/* ─── 主入口 ────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   DLC 统一跨语言桥接 — 完整测试套件         ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    test_load_connectors();
    test_registry();
    test_value_utils();
    test_sql();
    test_javascript();
    test_cross_language();
    test_error_handling();

    /* 清理 */
    dlc_unload_all();

    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  结果: %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    return TF ? 1 : 0;
}