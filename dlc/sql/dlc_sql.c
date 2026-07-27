/**
 * DLC SQL 连接器 (基于 SQLite3 嵌入式引擎)
 *
 * 将 SQLite 嵌入为跨语言数据查询引擎。
 * 所有语言通过此连接器执行 SQL 语句，结果以 DLC_Value 结构化返回。
 *
 * 编译:
 *   gcc -shared -fPIC -O2 -o libdlc_sql.so dlc_sql.c -lsqlite3
 *
 * 支持:
 *   - SQL 查询 (SELECT) → DLC_ARRAY of DLC_OBJECT
 *   - SQL 执行 (INSERT/UPDATE/DELETE/CREATE) → 影响行数
 *   - 多数据库连接管理
 *   - 参数化查询
 */

#include "../dlc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/* ─── 内部状态 ──────────────────────────────────────────────────── */

#define SQL_MAX_DBS 16
static struct {
    char*    name;
    sqlite3* db;
    int      is_memory;
} g_dbs[SQL_MAX_DBS];
static int g_db_count = 0;
static sqlite3* g_default_db = NULL;

/* ─── 辅助函数 ──────────────────────────────────────────────────── */

static sqlite3* find_db(const char* name) {
    if (!name || strcmp(name, "default") == 0) return g_default_db;
    for (int i = 0; i < g_db_count; i++) {
        if (strcmp(g_dbs[i].name, name) == 0)
            return g_dbs[i].db;
    }
    return NULL;
}

/* 将 SQLite 一行转为 DLC_Object */
static DLC_Value* row_to_object(sqlite3_stmt* stmt) {
    int cols = sqlite3_column_count(stmt);
    DLC_Value* obj = dlc_object_new(cols);

    for (int i = 0; i < cols; i++) {
        const char* name = sqlite3_column_name(stmt, i);
        int type = sqlite3_column_type(stmt, i);

        switch (type) {
        case SQLITE_INTEGER:
            dlc_object_set(obj, name, dlc_int(sqlite3_column_int64(stmt, i)));
            break;
        case SQLITE_FLOAT:
            dlc_object_set(obj, name, dlc_double(sqlite3_column_double(stmt, i)));
            break;
        case SQLITE_TEXT:
            dlc_object_set(obj, name, dlc_string((const char*)sqlite3_column_text(stmt, i)));
            break;
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, i);
            int len = sqlite3_column_bytes(stmt, i);
            DLC_Value* bv = (DLC_Value*)calloc(1, sizeof(DLC_Value));
            bv->type = DLC_BYTES;
            bv->data.string_val = (char*)malloc(len + 1);
            memcpy(bv->data.string_val, blob, len);
            bv->data.string_val[len] = '\0';
            dlc_object_set(obj, name, bv);
            break;
        }
        case SQLITE_NULL:
        default:
            dlc_object_set(obj, name, dlc_null());
            break;
        }
    }
    return obj;
}

/* ─── 初始化/终止 ───────────────────────────────────────────────── */

static int dlc_sql_init(void* config) {
    (void)config;

    /* 创建默认内存数据库 */
    int rc = sqlite3_open(":memory:", &g_default_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DLC:SQL] Cannot open default DB: %s\n", sqlite3_errmsg(g_default_db));
        return -1;
    }

    /* 启用扩展功能 */
    sqlite3_exec(g_default_db,
        "PRAGMA journal_mode=WAL;"
        "PRAGMA foreign_keys=ON;",
        NULL, NULL, NULL);

    g_dbs[g_db_count].name = strdup("default");
    g_dbs[g_db_count].db = g_default_db;
    g_dbs[g_db_count].is_memory = 1;
    g_db_count++;

    printf("[DLC:SQL] SQLite %s initialized (default: :memory:)\n", sqlite3_libversion());
    return 0;
}

static void dlc_sql_finalize(void) {
    for (int i = 0; i < g_db_count; i++) {
        sqlite3_close(g_dbs[i].db);
        free(g_dbs[i].name);
    }
    g_db_count = 0;
    g_default_db = NULL;
    printf("[DLC:SQL] Finalized\n");
}

/* ─── 执行 SQL ──────────────────────────────────────────────────── */

static DLC_Value* dlc_sql_eval(const char* code) {
    /* SQL eval = 执行 SELECT 并返回结果集 */
    sqlite3* db = g_default_db;
    if (!db) return dlc_error("SQL_NO_DB", "No database connected");

    /* 检测是否为 SELECT 语句 */
    const char* trimmed = code;
    while (*trimmed == ' ' || *trimmed == '\n' || *trimmed == '\t') trimmed++;

    if (strncasecmp(trimmed, "SELECT", 6) == 0 ||
        strncasecmp(trimmed, "PRAGMA", 6) == 0 ||
        strncasecmp(trimmed, "EXPLAIN", 7) == 0 ||
        strncasecmp(trimmed, "WITH", 4) == 0) {

        sqlite3_stmt* stmt = NULL;
        int rc = sqlite3_prepare_v2(db, code, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            return dlc_error("SQL_PREPARE_ERR", sqlite3_errmsg(db));
        }

        DLC_Value* result = dlc_array_new(64);
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            dlc_array_push(result, row_to_object(stmt));
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            dlc_free(result);
            return dlc_error("SQL_EXEC_ERR", sqlite3_errmsg(db));
        }

        return result;
    }

    /* 非 SELECT 语句: 执行并返回影响行数 */
    char* err = NULL;
    int rc = sqlite3_exec(db, code, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        DLC_Value* v = dlc_error("SQL_EXEC_ERR", err);
        sqlite3_free(err);
        return v;
    }

    return dlc_int(sqlite3_changes(db));
}

static DLC_Value* dlc_sql_exec(const char* code) {
    return dlc_sql_eval(code);
}

/* ─── SQL 函数调用 ──────────────────────────────────────────────── */

static DLC_Value* dlc_sql_call(const char* func_name, DLC_Value** args, int argc) {
    /* 内置函数: open, close, exec, query, tables */
    if (strcmp(func_name, "open") == 0 && argc >= 1) {
        /* 打开数据库文件 */
        const char* path = (argc > 0 && args[0]->type == DLC_STRING)
            ? args[0]->data.string_val : ":memory:";
        const char* name = (argc > 1 && args[1]->type == DLC_STRING)
            ? args[1]->data.string_val : path;

        if (g_db_count >= SQL_MAX_DBS) {
            return dlc_error("SQL_MAX_DBS", "Maximum database connections reached");
        }

        sqlite3* db;
        int rc = sqlite3_open(path, &db);
        if (rc != SQLITE_OK) {
            return dlc_error("SQL_OPEN_ERR", sqlite3_errmsg(db));
        }

        g_dbs[g_db_count].name = strdup(name);
        g_dbs[g_db_count].db = db;
        g_dbs[g_db_count].is_memory = (strcmp(path, ":memory:") == 0);
        g_db_count++;

        DLC_Value* info = dlc_object_new(4);
        dlc_object_set(info, "name", dlc_string(name));
        dlc_object_set(info, "path", dlc_string(path));
        dlc_object_set(info, "memory", dlc_bool(g_dbs[g_db_count - 1].is_memory));
        dlc_object_set(info, "status", dlc_string("connected"));
        return info;
    }

    if (strcmp(func_name, "tables") == 0) {
        /* 列出所有表 */
        return dlc_sql_eval(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
    }

    if (strcmp(func_name, "schema") == 0 && argc >= 1) {
        /* 获取表结构 */
        char sql[256];
        const char* table = args[0]->data.string_val;
        snprintf(sql, sizeof(sql),
            "PRAGMA table_info('%s')", table ? table : "");
        return dlc_sql_eval(sql);
    }

    if (strcmp(func_name, "query") == 0 && argc >= 1) {
        /* 执行查询 */
        const char* sql = args[0]->data.string_val;
        return sql ? dlc_sql_eval(sql) : dlc_error("SQL_NO_SQL", "No SQL provided");
    }

    /* 默认: 当作 SQL 语句执行 */
    return dlc_sql_eval(func_name);
}

/* ─── 全局变量 ──────────────────────────────────────────────────── */

static int dlc_sql_set_global(const char* name, DLC_Value* val) {
    (void)name; (void)val;
    return -1;
}

static DLC_Value* dlc_sql_get_global(const char* name) {
    (void)name;
    return NULL;
}

static void dlc_sql_free_val(DLC_Value* val) { dlc_free(val); }

/* ─── 导出连接器 ────────────────────────────────────────────────── */

static DLC_Connector g_sql_connector = {
    .language    = "sql",
    .version     = "SQLite 3.x",
    .file_ext    = ".sql",
    .description = "SQL connector via embedded SQLite3 engine",
    .init        = dlc_sql_init,
    .finalize    = dlc_sql_finalize,
    .eval        = dlc_sql_eval,
    .exec        = dlc_sql_exec,
    .call        = dlc_sql_call,
    .set_global  = dlc_sql_set_global,
    .get_global  = dlc_sql_get_global,
    .free_val    = dlc_sql_free_val,
    .native_handle = NULL,
};

#ifdef __cplusplus
extern "C" {
#endif

DLC_Connector* dlc_get_connector(void) {
    return &g_sql_connector;
}

#ifdef __cplusplus
}
#endif