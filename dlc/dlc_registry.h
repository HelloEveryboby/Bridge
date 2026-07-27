/**
 * DLC 注册表 — 管理所有已加载的语言连接器
 *
 * 提供:
 *   - dlc_register()    注册连接器
 *   - dlc_load()        从 .so 动态加载
 *   - dlc_find()        按语言名查找
 *   - dlc_bridge_call() 跨语言路由调用
 */

#ifndef DLC_REGISTRY_H
#define DLC_REGISTRY_H

#include "dlc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最大注册的语言连接器数 */
#define DLC_MAX_CONNECTORS 32

/* 注册连接器 */
int dlc_register(DLC_Connector* connector);

/* 从 .so 文件动态加载语言连接器 */
DLC_Connector* dlc_load(const char* so_path);

/* 按语言名查找连接器 */
DLC_Connector* dlc_find(const char* language);

/* 获取已注册连接器数量 */
int dlc_count(void);

/* 获取第 n 个连接器 */
DLC_Connector* dlc_get(int index);

/* 卸载所有连接器 */
void dlc_unload_all(void);

/**
 * 跨语言桥接调用
 *
 * @param target_lang  目标语言名 ("python", "javascript", ...)
 * @param func_name    目标函数名
 * @param args         参数数组
 * @param argc         参数个数
 * @return 执行结果
 *
 * 示例:
 *   // 从 C 调用 JavaScript 函数
 *   DLC_Value* args[] = { dlc_int(3), dlc_int(4) };
 *   DLC_Value* r = dlc_bridge_call("javascript", "add", args, 2);
 *
 *   // 从 Python 调用 C# 函数
 *   DLC_Value* r = dlc_bridge_call("csharp", "Math.Sqrt", args, 1);
 */
DLC_Value* dlc_bridge_call(const char* target_lang, const char* func_name,
                           DLC_Value** args, int argc);

/**
 * 跨语言执行代码
 *
 * @param target_lang 目标语言名
 * @param code        代码字符串
 * @return 执行结果
 */
DLC_Value* dlc_bridge_eval(const char* target_lang, const char* code);
DLC_Value* dlc_bridge_exec(const char* target_lang, const char* code);

/**
 * 跨语言设置/获取全局变量
 */
int dlc_bridge_set_global(const char* target_lang, const char* name, DLC_Value* val);
DLC_Value* dlc_bridge_get_global(const char* target_lang, const char* name);

/**
 * 批量初始化所有已注册连接器
 */
int dlc_init_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DLC_REGISTRY_H */