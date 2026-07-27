/**
 * DLC 值工具函数 — 值构造/析构/JSON 序列化
 */

#include "dlc_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 值构造 ────────────────────────────────────────────────────── */

DLC_Value* dlc_null(void) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_NULL;
    return v;
}

DLC_Value* dlc_bool(int val) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_BOOL;
    v->data.bool_val = val ? 1 : 0;
    return v;
}

DLC_Value* dlc_int(int64_t val) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_INT;
    v->data.int_val = val;
    return v;
}

DLC_Value* dlc_double(double val) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_DOUBLE;
    v->data.double_val = val;
    return v;
}

DLC_Value* dlc_string(const char* val) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_STRING;
    v->data.string_val = val ? strdup(val) : NULL;
    return v;
}

DLC_Value* dlc_error(const char* code, const char* msg) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_ERROR;
    v->data.error_val.code = code ? strdup(code) : NULL;
    v->data.error_val.message = msg ? strdup(msg) : NULL;
    return v;
}

DLC_Value* dlc_array_new(int capacity) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_ARRAY;
    v->data.array_val = (DLC_Array*)calloc(1, sizeof(DLC_Array));
    if (capacity < 4) capacity = 4;
    v->data.array_val->items = (DLC_Value**)calloc(capacity, sizeof(DLC_Value*));
    v->data.array_val->capacity = capacity;
    v->data.array_val->length = 0;
    return v;
}

DLC_Value* dlc_object_new(int capacity) {
    DLC_Value* v = (DLC_Value*)calloc(1, sizeof(DLC_Value));
    v->type = DLC_OBJECT;
    v->data.object_val = (DLC_Object*)calloc(1, sizeof(DLC_Object));
    if (capacity < 4) capacity = 4;
    v->data.object_val->pairs = (DLC_KVPair*)calloc(capacity, sizeof(DLC_KVPair));
    v->data.object_val->capacity = capacity;
    v->data.object_val->count = 0;
    return v;
}

/* ─── 数组操作 ──────────────────────────────────────────────────── */

void dlc_array_push(DLC_Value* arr, DLC_Value* item) {
    if (!arr || arr->type != DLC_ARRAY) return;
    DLC_Array* a = arr->data.array_val;
    if (a->length >= a->capacity) {
        a->capacity *= 2;
        a->items = (DLC_Value**)realloc(a->items, a->capacity * sizeof(DLC_Value*));
    }
    a->items[a->length++] = item;
}

DLC_Value* dlc_array_get(DLC_Value* arr, int index) {
    if (!arr || arr->type != DLC_ARRAY) return NULL;
    DLC_Array* a = arr->data.array_val;
    if (index < 0 || index >= a->length) return NULL;
    return a->items[index];
}

int dlc_array_len(DLC_Value* arr) {
    if (!arr || arr->type != DLC_ARRAY) return 0;
    return arr->data.array_val->length;
}

/* ─── 对象操作 ──────────────────────────────────────────────────── */

void dlc_object_set(DLC_Value* obj, const char* key, DLC_Value* val) {
    if (!obj || obj->type != DLC_OBJECT || !key) return;
    DLC_Object* o = obj->data.object_val;

    /* 检查是否已存在该 key */
    for (int i = 0; i < o->count; i++) {
        if (strcmp(o->pairs[i].key, key) == 0) {
            dlc_free(o->pairs[i].value);
            o->pairs[i].value = val;
            return;
        }
    }

    /* 扩容 */
    if (o->count >= o->capacity) {
        o->capacity *= 2;
        o->pairs = (DLC_KVPair*)realloc(o->pairs, o->capacity * sizeof(DLC_KVPair));
    }

    o->pairs[o->count].key = strdup(key);
    o->pairs[o->count].value = val;
    o->count++;
}

DLC_Value* dlc_object_get(DLC_Value* obj, const char* key) {
    if (!obj || obj->type != DLC_OBJECT || !key) return NULL;
    DLC_Object* o = obj->data.object_val;
    for (int i = 0; i < o->count; i++) {
        if (strcmp(o->pairs[i].key, key) == 0)
            return o->pairs[i].value;
    }
    return NULL;
}

int dlc_object_has(DLC_Value* obj, const char* key) {
    return dlc_object_get(obj, key) != NULL;
}

/* ─── 释放 ──────────────────────────────────────────────────────── */

void dlc_free(DLC_Value* val) {
    if (!val) return;
    switch (val->type) {
    case DLC_STRING:
        free(val->data.string_val);
        break;
    case DLC_BYTES:
        free(val->data.string_val);
        break;
    case DLC_ARRAY:
        if (val->data.array_val) {
            for (int i = 0; i < val->data.array_val->length; i++)
                dlc_free(val->data.array_val->items[i]);
            free(val->data.array_val->items);
            free(val->data.array_val);
        }
        break;
    case DLC_OBJECT:
        if (val->data.object_val) {
            for (int i = 0; i < val->data.object_val->count; i++) {
                free(val->data.object_val->pairs[i].key);
                dlc_free(val->data.object_val->pairs[i].value);
            }
            free(val->data.object_val->pairs);
            free(val->data.object_val);
        }
        break;
    case DLC_ERROR:
        free(val->data.error_val.code);
        free(val->data.error_val.message);
        break;
    default:
        break;
    }
    free(val);
}

/* ─── JSON 序列化 ───────────────────────────────────────────────── */

static char* json_escape(const char* s) {
    if (!s) return strdup("null");
    size_t len = strlen(s);
    size_t cap = len * 2 + 4;
    char* out = (char*)malloc(cap);
    size_t pos = 0;
    out[pos++] = '"';
    for (size_t i = 0; i < len; i++) {
        if (pos + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        switch (s[i]) {
        case '"':  out[pos++] = '\\'; out[pos++] = '"'; break;
        case '\\': out[pos++] = '\\'; out[pos++] = '\\'; break;
        case '\n': out[pos++] = '\\'; out[pos++] = 'n'; break;
        case '\r': out[pos++] = '\\'; out[pos++] = 'r'; break;
        case '\t': out[pos++] = '\\'; out[pos++] = 't'; break;
        default:   out[pos++] = s[i]; break;
        }
    }
    out[pos++] = '"';
    out[pos] = '\0';
    return out;
}

static void json_append(char** buf, size_t* len, size_t* cap, const char* s) {
    size_t slen = strlen(s);
    if (*len + slen + 1 > *cap) {
        *cap = (*len + slen + 1) * 2;
        *buf = (char*)realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void dlc_to_json_buf(DLC_Value* val, char** buf, size_t* len, size_t* cap);

static void dlc_to_json_buf(DLC_Value* val, char** buf, size_t* len, size_t* cap) {
    if (!val) { json_append(buf, len, cap, "null"); return; }

    char tmp[128];
    switch (val->type) {
    case DLC_NULL:
        json_append(buf, len, cap, "null");
        break;
    case DLC_BOOL:
        json_append(buf, len, cap, val->data.bool_val ? "true" : "false");
        break;
    case DLC_INT:
        snprintf(tmp, sizeof(tmp), "%ld", (long)val->data.int_val);
        json_append(buf, len, cap, tmp);
        break;
    case DLC_DOUBLE:
        snprintf(tmp, sizeof(tmp), "%g", val->data.double_val);
        json_append(buf, len, cap, tmp);
        break;
    case DLC_STRING: {
        char* esc = json_escape(val->data.string_val);
        json_append(buf, len, cap, esc);
        free(esc);
        break;
    }
    case DLC_ARRAY:
        json_append(buf, len, cap, "[");
        if (val->data.array_val) {
            for (int i = 0; i < val->data.array_val->length; i++) {
                if (i > 0) json_append(buf, len, cap, ",");
                dlc_to_json_buf(val->data.array_val->items[i], buf, len, cap);
            }
        }
        json_append(buf, len, cap, "]");
        break;
    case DLC_OBJECT:
        json_append(buf, len, cap, "{");
        if (val->data.object_val) {
            for (int i = 0; i < val->data.object_val->count; i++) {
                if (i > 0) json_append(buf, len, cap, ",");
                char* key = json_escape(val->data.object_val->pairs[i].key);
                json_append(buf, len, cap, key);
                free(key);
                json_append(buf, len, cap, ":");
                dlc_to_json_buf(val->data.object_val->pairs[i].value, buf, len, cap);
            }
        }
        json_append(buf, len, cap, "}");
        break;
    case DLC_ERROR: {
        /* 序列化为 {error: {code, message}} */
        json_append(buf, len, cap, "{\"error\":{\"code\":");
        char* code = json_escape(val->data.error_val.code ? val->data.error_val.code : "unknown");
        json_append(buf, len, cap, code); free(code);
        json_append(buf, len, cap, ",\"message\":");
        char* msg = json_escape(val->data.error_val.message ? val->data.error_val.message : "");
        json_append(buf, len, cap, msg); free(msg);
        json_append(buf, len, cap, "}}");
        break;
    }
    default:
        json_append(buf, len, cap, "null");
        break;
    }
}

char* dlc_to_json(DLC_Value* val) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    buf[0] = '\0';
    dlc_to_json_buf(val, &buf, &len, &cap);
    return buf;
}

/* ─── 类型名称 ──────────────────────────────────────────────────── */

const char* dlc_type_name(DLC_Type t) {
    switch (t) {
    case DLC_NULL:     return "null";
    case DLC_BOOL:     return "bool";
    case DLC_INT:      return "int";
    case DLC_DOUBLE:   return "double";
    case DLC_STRING:   return "string";
    case DLC_BYTES:    return "bytes";
    case DLC_ARRAY:    return "array";
    case DLC_OBJECT:   return "object";
    case DLC_FUNCTION: return "function";
    case DLC_ERROR:    return "error";
    case DLC_DATE:     return "date";
    default:           return "unknown";
    }
}