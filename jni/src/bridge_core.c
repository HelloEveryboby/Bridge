/**
 * Bridge Core — Bridge 多语言桥接框架伞形实现
 *
 * 本文件提供 Bridge 框架级通用 API：
 *   - 版本信息
 *   - 子组件注册与状态查询
 *   - 模块加载（包装 pb_loader）
 *
 * PyBridge（Python 桥接）作为子模块通过 br_register_pybridge_ready()
 * 报告自身就绪状态；具体 Python API 仍由 pybridge_python.c 的 pb_* 函数提供。
 *
 * 编译: gcc -shared -fPIC -o libbridge.so bridge_core.c pb_loader.c -ldl
 */
#include "bridge/bridge_core.h"
#include <string.h>

/* ── 全局子组件状态 ───────────────────────────────────────────── */

static struct {
    int pybridge_ready;
    int dlc_ready;
    int jni_ready;
} g_bridge_state = {0};

/* ── 版本与组件信息 ──────────────────────────────────────────── */

const char* br_version(void) {
    return BRIDGE_VERSION;
}

const char* br_component_name(BridgeComponent c) {
    switch (c) {
        case BRIDGE_COMPONENT_CORE:     return "BridgeCore";
        case BRIDGE_COMPONENT_PYBRIDGE: return "PyBridge";
        case BRIDGE_COMPONENT_DLC:      return "DLC";
        case BRIDGE_COMPONENT_JNI:      return "JNI";
        default:                        return "Unknown";
    }
}

int br_component_is_ready(BridgeComponent c) {
    switch (c) {
        case BRIDGE_COMPONENT_CORE:     return 1;  /* Core 永远就绪 */
        case BRIDGE_COMPONENT_PYBRIDGE: return g_bridge_state.pybridge_ready;
        case BRIDGE_COMPONENT_DLC:      return g_bridge_state.dlc_ready;
        case BRIDGE_COMPONENT_JNI:      return g_bridge_state.jni_ready;
        default:                        return 0;
    }
}

void br_register_pybridge_ready(int ready) {
    g_bridge_state.pybridge_ready = ready;
}

/* ── 模块加载（薄包装 pb_loader） ─────────────────────────────── */
/*
 * pb_load_module / pb_get_func / pb_unload_module / pb_unload_all
 * 在 pb_loader.c 中实现；这里仅做命名空间映射，便于上层使用 br_ 前缀。
 *
 * 注意：保留 pb_ 符号以维持 ABI 兼容；新代码可使用 br_ 别名。
 */

extern PB_Module* pb_load_module(const char* name, const char* path);
extern void*      pb_get_func(PB_Module* mod, const char* func_name);
extern void       pb_unload_module(PB_Module* mod);
extern void       pb_unload_all(void);

PB_Module* br_load_module(const char* name, const char* path) {
    return pb_load_module(name, path);
}

void* br_get_func(PB_Module* mod, const char* func_name) {
    return pb_get_func(mod, func_name);
}

void br_unload_module(PB_Module* mod) {
    pb_unload_module(mod);
}

void br_unload_all(void) {
    pb_unload_all();
}
