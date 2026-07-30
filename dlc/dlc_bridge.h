/**
 * DLC 数据桥接 — 文件间数据转换与传递
 *
 * 用一种语言编写一个桥接文件，连接两个或多个文件之间的数据桥接：
 *   读取所有源文件 → transform 转换 → 写入所有目标文件
 *
 * 支持两种 transform 模式：
 *   - CALLBACK: 内置 C 回调函数（永远可用，无需语言运行时）
 *   - LANG:     用指定 DLC 语言脚本执行转换（需对应语言连接器已注册）
 *
 * 典型流程:
 *   DLC_Bridge* b = dlc_bridge_create("my_bridge");
 *   dlc_bridge_add_source(b, "input.txt", NULL);
 *   dlc_bridge_add_sink(b, "output.txt", NULL);
 *   dlc_bridge_set_transform_cb(b, my_transform);
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

typedef struct DLC_Bridge DLC_Bridge;

/* 桥接模式 */
typedef enum {
    DLC_BRIDGE_MODE_CALLBACK = 0,  /* 内置 C 回调 transform（永远可用） */
    DLC_BRIDGE_MODE_LANG     = 1,  /* 用指定 DLC 语言脚本 transform */
} DLC_BridgeMode;

/* transform 回调函数签名：接收输入 DLC_Value*，返回转换后的 DLC_Value*（调用方负责 dlc_free） */
typedef DLC_Value* (*DLC_TransformFn)(DLC_Value* input);

/* 创建/销毁桥接器。name 为桥接器名称（用于日志） */
DLC_Bridge* dlc_bridge_create(const char* name);
void        dlc_bridge_destroy(DLC_Bridge* b);

/* 添加源文件端点。file_path 文件路径，lang 为该文件对应 DLC 语言（用于日志/解析，可传 NULL 表示纯文本） */
int dlc_bridge_add_source(DLC_Bridge* b, const char* file_path, const char* lang);

/* 添加目标文件端点。file_path 文件路径，lang 同上 */
int dlc_bridge_add_sink(DLC_Bridge* b, const char* file_path, const char* lang);

/* 设置 transform 为内置 C 回调（mode 自动设为 CALLBACK） */
int dlc_bridge_set_transform_cb(DLC_Bridge* b, DLC_TransformFn fn);

/* 设置 transform 为指定语言的脚本代码（mode 自动设为 LANG）。
   执行时：set_global("bridge_input", input) → exec(code) → get_global("bridge_output") */
int dlc_bridge_set_transform_lang(DLC_Bridge* b, const char* lang, const char* code);

/* 执行桥接：读取所有源文件内容 → 合并为 DLC_ARRAY（每项为源文件 DLC_STRING 内容）→
   transform 转换 → 把结果写入所有目标文件。
   返回处理的源文件数；<0 表示出错（用 dlc_bridge_error 取错误信息） */
int dlc_bridge_run(DLC_Bridge* b);

/* 获取最后一次错误信息（NULL 表示无错误） */
const char* dlc_bridge_error(DLC_Bridge* b);

#ifdef __cplusplus
}
#endif

#endif /* DLC_BRIDGE_H */
