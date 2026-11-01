# Bridge 框架架构

Bridge 是一个统一的多语言互操作框架。**PyBridge 是 Bridge 框架的 Python 桥接子模块**，
并非独立项目。本文档定义 Bridge 的子模块层级、职责边界与符号约定。

> 版本：Bridge 2.0.0　|　默认 Python：3.13（运行时回退 3.12 / 3.11）

---

## 1. 子模块层级

```
Bridge (com.bridge.*)
├── Core      通用核心：版本 / 组件状态 / 模块加载基础设施
├── PyBridge  Python 桥接子模块（dlopen libpython + pb_* API）
├── JNI       Java/Kotlin ↔ C 互操作（Python 子模块的 JNI 封装）
├── DLC       多语言连接器协议（C/C++/Rust/Go/JS/TS/C#/SQL）
└── P2P       多语言后端点到点通信协议与 SDK（Brokerless TCP/UDS 直连）
```

| 子模块 | 标识 | 实现位置 | 头文件 | 命名前缀 |
|--------|------|----------|--------|----------|
| **Core** | `BRIDGE_COMPONENT_CORE` | `jni/src/bridge_core.c` | `include/bridge/bridge_core.h` | `br_*` |
| **PyBridge** | `BRIDGE_COMPONENT_PYBRIDGE` | `jni/src/pybridge_python.c` | `include/pybridge/pybridge_core.h` | `pb_*` |
| **JNI** | `BRIDGE_COMPONENT_JNI` | `jni/src/pybridge_jni.c`, `pybridge_static_proxy.c` | — | `Java_com_bridge_py_*` |
| **DLC** | `BRIDGE_COMPONENT_DLC` | `dlc/` | `dlc/dlc_protocol.h`、`dlc/dlc_bridge.h` | `dlc_*` |

---

## 2. Core 与 PyBridge 的关系

Bridge 框架采用「通用核心 + 语言子模块」的结构：

- **`bridge_core.c`（通用层）**：不依赖任何具体语言运行时（无 `Python.h` / `jni.h`）。
  提供框架版本（`br_version`）、子组件状态查询（`br_component_is_ready`）、
  以及模块加载的薄包装（`br_load_module` / `br_get_func` / `br_unload_module`）。

- **`pybridge_python.c`（Python 子模块）**：Python 专用实现。
  通过 `dlopen` 加载 `libpython3.13`（回退 3.12/3.11），`dlsym` 获取全部 CPython API，
  对外暴露 `pb_*` 前缀的函数（`pb_init`、`pb_import`、`pb_call`、`pb_eval` 等）。

两者通过就绪回调解耦：PyBridge 在 `pb_init()` 完成后调用
`br_register_pybridge_ready(1)` 通知框架，在 `pb_finalize()` 前调用
`br_register_pybridge_ready(0)`。框架上层可通过
`br_component_is_ready(BRIDGE_COMPONENT_PYBRIDGE)` 查询 Python 子模块是否可用。

---

## 3. 符号约定与 ABI 兼容

| 前缀 | 含义 | 兼容性 |
|------|------|--------|
| `br_*` | Bridge 框架级新增 API | Bridge 2.0+ |
| `pb_*` | PyBridge 历史符号（Python 桥接 + 模块加载） | **保留**，维持 ABI 兼容 |
| `dlc_*` | DLC 多语言连接器协议 | DLC 独立版本 |
| `Java_com_bridge_py_*` | JNI 入口（对应 `com.bridge.py.*` 包） | Bridge 2.0+ |

> **重要**：`pb_*` 符号（`pb_init`、`pb_import`、`pb_call`、`pb_load_module` 等）
> 在 Bridge 2.0 中完整保留。历史依赖 `libpybridge.so` 的代码无需修改即可运行。
> 新代码推荐使用 `br_*` 框架级 API；操作 Python 对象仍使用 `pb_*`。

---

## 4. 包名与构建标识

| 维度 | Bridge 2.0 | 历史（PyBridge 1.x） |
|------|-----------|----------------------|
| Java/Kotlin 包 | `com.bridge.py.*` | `com.pybridge.*` |
| Gradle 插件 ID | `com.bridge.python` | `com.pybridge.python` |
| Gradle group | `com.bridge` | `com.pybridge` |
| 原生库名 | `libpybridge.so`（保留） | `libpybridge.so` |
| 框架版本 | `2.0.0` | `1.0.0` |

原生库名 `libpybridge.so` 故意保留不变，以维持 AGP 打包与历史 `System.loadLibrary`
调用的兼容性；其内部已包含 Bridge Core + PyBridge + 模块加载器三层实现。

---

## 5. 目录映射

```
jni/
├── include/
│   ├── bridge/bridge_core.h       ← Bridge Core 伞形头文件（br_* API）
│   └── pybridge/
│       ├── pybridge_core.h        ← PyBridge pb_* API 声明
│       └── pb_module.h            ← 多语言模块协议 + PB_Module 不透明类型
└── src/
    ├── bridge_core.c              ← Core 通用层
    ├── pybridge_python.c          ← PyBridge Python 子模块（原 pybridge_core.c）
    ├── pybridge_jni.c             ← JNI 封装（com.bridge.py.core.Python）
    ├── pybridge_static_proxy.c    ← 静态代理 JNI（com.bridge.py.proxy）
    └── pb_loader.c                ← 模块动态加载器（pb_load_module 等）
```

DLC 子模块（`dlc/`）独立目录：

```
dlc/
├── dlc_protocol.h             ← 统一值类型 DLC_Value + 连接器接口 DLC_Connector
├── dlc_value.c                ← 值构造/析构/JSON 序列化工具函数
├── dlc_registry.h/.c          ← 连接器注册表 + 跨语言路由（dlc_bridge_call/eval/exec）
├── dlc_bridge.h/.c            ← 数据桥接（文件间数据桥接：源→transform→目标）
├── test_dlc.c / test_bridge.c ← 连接器测试 / 数据桥接测试
└── {cpp,javascript,csharp,sql,typescript}/  ← 各语言 DLC 连接器
```

---

## 6. DLC 数据桥接

DLC 子模块提供两层能力：

1. **跨语言函数调用**（`dlc_bridge_call/eval/exec`）：在已注册的语言连接器之间
   同步路由调用，数据通过统一 `DLC_Value` 交换。
2. **文件间数据桥接**（`DLC_Bridge`）：用一个语言编写一个桥接文件，连接两个或多个
   文件之间的数据——从多个源文件读取 → transform 转换 → 写入多个目标文件。

transform 支持两种模式：
- **C 回调**（`dlc_bridge_set_transform_cb`）：永远可用，无需任何语言运行时。
- **DLC 语言脚本**（`dlc_bridge_set_transform_lang`）：用 Python/JS/C#/SQL 等语言
  编写转换逻辑，复用已注册的 DLC 连接器执行。

详见 [MULTI_LANGUAGE.md](MULTI_LANGUAGE.md)「数据桥接」章节与
[dlc/README.md](../dlc/README.md)。

---

## 7. 扩展新的语言子模块

Bridge 的设计允许在 Core 之上挂载新的语言子模块（未来可新增 `bridge_javascript.c`、
`bridge_ruby.c` 等）。约定：

1. 在 `bridge_core.h` 的 `BridgeComponent` 枚举中注册新组件标识。
2. 在 `bridge_core.c` 中维护其就绪状态，并提供对应的 `br_register_<lang>_ready()`。
3. 子模块实现通过 `dlopen` 加载目标语言运行时，对外暴露统一的 `pb_*` 或新前缀 API。
4. JNI 层（如需 Java 互操作）放在 `com.bridge.py.<lang>` 或独立包下。

详见 [DESIGN.md](DESIGN.md) 与 [MULTI_LANGUAGE.md](MULTI_LANGUAGE.md)。
