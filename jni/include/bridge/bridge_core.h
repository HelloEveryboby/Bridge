/**
 * Bridge Core — Bridge 多语言桥接框架伞形头文件
 *
 * Bridge 是统一的多语言互操作框架，由以下子模块组成：
 *   - Bridge Core   (本文件)     通用 dlopen / 模块加载基础设施
 *   - PyBridge      (pybridge_python.*) Python 桥接子模块（dlopen libpython + pb_* API）
 *   - JNI           (pybridge_jni.*)  Java/Kotlin ↔ C 互操作
 *   - DLC           (dlc/)            多语言连接器协议（C/C++/Rust/Go/JS/TS/C#/SQL）
 *
 * 设计要点：
 *   - 本头文件不依赖任何具体语言运行时（无 Python.h / jni.h）
 *   - pb_* C 符号保留以维持 ABI 兼容（PyBridge 历史命名）
 *   - br_* 前缀为 Bridge 框架级新增 API
 */
#ifndef BRIDGE_CORE_H
#define BRIDGE_CORE_H

#include "pybridge/pb_module.h"  /* 复用多语言模块协议 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Bridge 框架版本 ──────────────────────────────────────────── */

#define BRIDGE_VERSION "2.0.0"
#define BRIDGE_VERSION_MAJOR 2
#define BRIDGE_VERSION_MINOR 0
#define BRIDGE_VERSION_PATCH 0

/* ── Bridge 子组件标识 ───────────────────────────────────────── */

typedef enum {
    BRIDGE_COMPONENT_CORE     = 0,  /* 通用核心 */
    BRIDGE_COMPONENT_PYBRIDGE = 1,  /* Python 桥接子模块 */
    BRIDGE_COMPONENT_DLC      = 2,  /* 多语言连接器协议 */
    BRIDGE_COMPONENT_JNI      = 3,  /* Java/Kotlin ↔ C 桥接 */
} BridgeComponent;

/* ── 框架级 API ───────────────────────────────────────────────── */

/** Bridge 框架版本字符串 */
const char* br_version(void);

/** 子组件名称（用于日志/自省） */
const char* br_component_name(BridgeComponent c);

/** 子组件是否已初始化 */
int br_component_is_ready(BridgeComponent c);

/* ── 通用模块加载（re-export 自 pb_loader，br_ 命名空间） ─────── */

/** 加载 .so 模块到 Bridge 框架（等价于 pb_load_module） */
PB_Module* br_load_module(const char* name, const char* path);

/** 从已加载模块获取函数指针（等价于 pb_get_func） */
void* br_get_func(PB_Module* mod, const char* func_name);

/** 卸载模块（等价于 pb_unload_module） */
void br_unload_module(PB_Module* mod);

/** 卸载所有模块 */
void br_unload_all(void);

/* ── PyBridge 子组件就绪回调（由 pybridge_python.c 设置） ────── */

/** 内部使用：PyBridge 初始化完成后注册自身 */
void br_register_pybridge_ready(int ready);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_CORE_H */
