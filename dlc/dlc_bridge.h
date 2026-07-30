/**
 * DLC 数据桥接 — 通用端点间数据转换与传递
 *
 * 用一种语言编写一个桥接逻辑，连接两个或多个端点之间的数据桥接：
 *   读取所有源端点 → transform 转换 → 写入所有目标端点
 *
 * ── 端点类型（不限于文件）──────────────────────────────────────
 *   - FILE:   文件端点（磁盘文件读写）
 *   - MEM:    内存缓冲端点（读写内存 buffer，适合进程内数据流）
 *   - STRING: 字符串端点（源=直接字符串数据；目标=分配字符串取出）
 *   - STDIO:  标准流端点（stdin/stdout/stderr）
 *
 * ── transform 模式 ────────────────────────────────────────────
 *   - CALLBACK:  内置 C 回调（永远可用，无需语言运行时）
 *   - LANG:      用指定 DLC 语言脚本（需对应连接器已注册）
 *   - PIPELINE:  多 stage 链式编排（stage1 → stage2 → ... → stageN）
 *   - JSON_RULE: 声明式 JSON 转换规则（map/filter/field/rename 等）
 *
 * ── 运行时能力 ────────────────────────────────────────────────
 *   - 重试：transform 失败时自动重试（可配置次数）
 *   - 异步：dlc_bridge_run_async 在后台线程执行，回调通知完成
 *
 * 独立性：本模块只依赖 dlc_value.c（值工具）+ dlc_registry.c（跨语言路由），
 *        可脱离 Bridge 框架（jni/pybridge）独立编译为 libdlc_bridge.so。
 *
 * 典型流程:
 *   DLC_Bridge* b = dlc_bridge_create("my_bridge");
 *   dlc_bridge_add_source(b, "input.txt", NULL);          // 文件端点
 *   dlc_bridge_add_source_mem(b, "inline data", 11, NULL); // 内存端点
 *   dlc_bridge_add_sink(b, "output.txt", NULL);
 *   dlc_bridge_set_transform_cb(b, my_transform);
 *   dlc_bridge_set_retry(b, 3);                           // 失败重试 3 次
 *   dlc_bridge_run(b);
 *   dlc_bridge_destroy(b);
 */

#ifndef DLC_BRIDGE_H
#define DLC_BRIDGE_H

#include "dlc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最大源/目标端点数 */
#define DLC_BRIDGE_MAX_ENDPOINTS 16
/* 最大 pipeline stage 数 */
#define DLC_BRIDGE_MAX_STAGES    16
/* 最大内存端点 buffer 容量（字节） */
#define DLC_BRIDGE_MEM_MAX       (16 * 1024 * 1024)

typedef struct DLC_Bridge DLC_Bridge;

/* ── 端点类型 ──────────────────────────────────────────────────── */

typedef enum {
    DLC_ENDPOINT_FILE   = 0,  /* 文件端点 */
    DLC_ENDPOINT_MEM    = 1,  /* 内存缓冲端点 */
    DLC_ENDPOINT_STRING = 2,  /* 字符串端点 */
    DLC_ENDPOINT_STDIO  = 3,  /* 标准流端点（stdin/stdout/stderr） */
} DLC_EndpointType;

/* 标准流标识（用于 STDIO 端点） */
typedef enum {
    DLC_STDIO_STDIN  = 0,
    DLC_STDIO_STDOUT = 1,
    DLC_STDIO_STDERR = 2,
} DLC_StdioType;

/* ── transform 模式 ────────────────────────────────────────────── */

typedef enum {
    DLC_BRIDGE_MODE_CALLBACK  = 0,  /* 内置 C 回调 transform */
    DLC_BRIDGE_MODE_LANG      = 1,  /* DLC 语言脚本 transform */
    DLC_BRIDGE_MODE_PIPELINE  = 2,  /* 多 stage 链式编排 */
    DLC_BRIDGE_MODE_JSON_RULE = 3,  /* 声明式 JSON 规则 */
} DLC_BridgeMode;

/* transform 回调函数签名：接收输入 DLC_Value*，返回转换后的 DLC_Value*
   （返回值由桥接器负责 dlc_free） */
typedef DLC_Value* (*DLC_TransformFn)(DLC_Value* input);

/* ── 创建/销毁 ────────────────────────────────────────────────── */

/* 创建桥接器。name 为桥接器名称（用于日志） */
DLC_Bridge* dlc_bridge_create(const char* name);

/* 销毁桥接器（释放所有端点、transform 配置、内存 buffer） */
void dlc_bridge_destroy(DLC_Bridge* b);

/* ── 源端点（向后兼容 + 新类型） ──────────────────────────────── */

/* 添加文件源端点（向后兼容：等价于 add_source_file） */
int dlc_bridge_add_source(DLC_Bridge* b, const char* file_path, const char* lang);

/* 添加文件源端点（显式命名） */
int dlc_bridge_add_source_file(DLC_Bridge* b, const char* file_path, const char* lang);

/* 添加内存源端点：data 为数据指针，len 为字节数（数据会被复制） */
int dlc_bridge_add_source_mem(DLC_Bridge* b, const char* data, int len, const char* lang);

/* 添加字符串源端点：str 以 \0 结尾的字符串（等价于 mem，但按 strlen 计长） */
int dlc_bridge_add_source_str(DLC_Bridge* b, const char* str, const char* lang);

/* 添加标准流源端点：which 指定 stdin */
int dlc_bridge_add_source_stdio(DLC_Bridge* b, DLC_StdioType which, const char* lang);

/* ── 目标端点（向后兼容 + 新类型） ────────────────────────────── */

/* 添加文件目标端点（向后兼容：等价于 add_sink_file） */
int dlc_bridge_add_sink(DLC_Bridge* b, const char* file_path, const char* lang);

/* 添加文件目标端点（显式命名） */
int dlc_bridge_add_sink_file(DLC_Bridge* b, const char* file_path, const char* lang);

/* 添加内存目标端点：run 后用 dlc_bridge_get_sink_mem 取出写入的数据 */
int dlc_bridge_add_sink_mem(DLC_Bridge* b, const char* lang);

/* 添加字符串目标端点：run 后用 dlc_bridge_get_sink_str 取出写入的字符串 */
int dlc_bridge_add_sink_str(DLC_Bridge* b, const char* lang);

/* 添加标准流目标端点：which 指定 stdout/stderr */
int dlc_bridge_add_sink_stdio(DLC_Bridge* b, DLC_StdioType which, const char* lang);

/* ── 取出内存/字符串目标端点数据 ──────────────────────────────── */

/* 取出第 idx 个内存目标端点的数据（返回指针，桥接器拥有，勿 free；*len 输出长度）。
   idx 是 add_sink_mem/add_sink_str 的添加顺序索引（从 0 开始，按所有目标端点计） */
const char* dlc_bridge_get_sink_mem(DLC_Bridge* b, int idx, int* len);

/* 取出第 idx 个字符串目标端点的字符串（C 字符串，桥接器拥有，勿 free） */
const char* dlc_bridge_get_sink_str(DLC_Bridge* b, int idx);

/* ── transform 设置 ───────────────────────────────────────────── */

/* 设置 transform 为内置 C 回调（mode 自动设为 CALLBACK） */
int dlc_bridge_set_transform_cb(DLC_Bridge* b, DLC_TransformFn fn);

/* 设置 transform 为指定语言的脚本代码（mode 自动设为 LANG）。
   执行时：set_global("bridge_input", input) → exec(code) → get_global("bridge_output") */
int dlc_bridge_set_transform_lang(DLC_Bridge* b, const char* lang, const char* code);

/* 添加一个 pipeline stage（mode 自动设为 PIPELINE）。
   多次调用添加多个 stage，run 时按顺序串联执行：
   input → stage1 → stage2 → ... → stageN → output
   每个 stage 接收上一个 stage 的输出，返回转换结果 */
int dlc_bridge_add_stage(DLC_Bridge* b, DLC_TransformFn stage);

/* 设置声明式 JSON 转换规则（mode 自动设为 JSON_RULE）。
   rule_json 为 JSON 字符串，描述转换操作，支持：
     {"op":"map","expr":"<JS 表达式>"}            对数组每元素求值
     {"op":"filter","expr":"<JS 表达式>"}          过滤数组元素
     {"op":"field","name":"<字段名>"}              提取对象字段
     {"op":"rename","from":"<旧名>","to":"<新名>"} 重命名对象字段
   实际执行委托给 javascript DLC 连接器（需已注册）；无 JS 连接器时返回错误 */
int dlc_bridge_set_transform_rule(DLC_Bridge* b, const char* rule_json);

/* ── 运行时配置 ───────────────────────────────────────────────── */

/* 设置 transform 失败重试次数（默认 0=不重试）。
   transform 返回 NULL 或 DLC_ERROR 时触发重试，最多重试 max_retries 次 */
int dlc_bridge_set_retry(DLC_Bridge* b, int max_retries);

/* ── 执行 ─────────────────────────────────────────────────────── */

/* 执行桥接（同步）：读取所有源端点 → 合并为 DLC_ARRAY → transform → 写入所有目标端点。
   返回处理的源端点数；<0 表示出错（用 dlc_bridge_error 取错误信息） */
int dlc_bridge_run(DLC_Bridge* b);

/* 异步执行回调签名：bridge 为桥接器，result 为运行结果（同 dlc_bridge_run 返回值），
   user_data 为 run_async 传入的用户数据 */
typedef void (*DLC_BridgeCallback)(DLC_Bridge* bridge, int result, void* user_data);

/* 异步执行桥接（后台线程）：立即返回 0 表示已启动，非 0 表示启动失败。
   完成后调用 callback 通知结果。注意：callback 在后台线程执行，勿在 callback 中
   操作 bridge（可读取 result 和 user_data） */
int dlc_bridge_run_async(DLC_Bridge* b, DLC_BridgeCallback callback, void* user_data);

/* 等待异步执行完成（阻塞直到 callback 触发）。返回运行结果。
   若无异步任务在执行，立即返回 0 */
int dlc_bridge_wait(DLC_Bridge* b);

/* ── 错误信息 ─────────────────────────────────────────────────── */

/* 获取最后一次错误信息（NULL 表示无错误） */
const char* dlc_bridge_error(DLC_Bridge* b);

/* 获取当前 transform 模式 */
DLC_BridgeMode dlc_bridge_mode(DLC_Bridge* b);

#ifdef __cplusplus
}
#endif

#endif /* DLC_BRIDGE_H */
