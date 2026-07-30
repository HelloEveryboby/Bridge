# Bridge Multi-Language — 多语言桥接架构

> Bridge 是统一的多语言互操作框架；PyBridge 为其 Python 子模块。
> 框架子模块层级见 [BRIDGE.md](BRIDGE.md)。

## 核心思想

**所有非 JVM 语言与 Java/Kotlin 的通信都必须经过 JNI。**

区别只在于各语言如何暴露函数给 C 层调用：

```
Java/Kotlin
    │
    ▼ JNI
┌─────────────────────────────────────────────┐
│          Bridge Core (C)                     │
│          ├── bridge_core.c   (通用层 br_*)   │
│          └── pybridge_python.c (Python pb_*) │
│                                              │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐    │
│  │Python   │  │ Rust    │  │  Go     │    │
│  │Module   │  │ Module  │  │ Module  │    │
│  │         │  │         │  │         │    │
│  │ dlopen  │  │ dlopen  │  │ dlopen  │    │
│  │ .so     │  │ .so     │  │ .so     │    │
│  └─────────┘  └─────────┘  └─────────┘    │
│                                              │
│  ┌─────────────────────────────────────┐    │
│  │  统一模块系统 (pb_module_*)          │    │
│  │  - 加载: pb_load_module("rustmath") │    │
│  │  - 调用: pb_get_func("add")         │    │
│  │  - 类型: 统一类型转换层              │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

## 每种语言的接入方式

### C/C++ — 直接编译
```c
// mylib.c — 编译为 libmylib.so
#include "pb_module.h"
PB_FUNC(double, add, double a, double b) {
    return a + b;
}
```

### Rust — cdylib
```rust
// lib.rs — 编译为 librustmath.so
use pb_module::*;

#[pb_export]
fn add(a: f64, b: f64) -> f64 {
    a + b
}
```

### Go — cgo export
```go
// main.go — 编译为 libgomath.so
import "C"

//export add
func add(a C.double, b C.double) C.double {
    return a + b
}
```

### Python — 解释器嵌入
```python
# 通过 dlopen libpython 嵌入
def add(a, b):
    return a + b
```

## Java/Kotlin 统一调用

```kotlin
// 所有语言用同一套 API
val module = PyBridge.load("rustmath")  // 或 "gomath", "cmath", "pymath"
val result = module.call("add", 3.0, 4.0).toDouble()  // 7.0
```

## 模块注册协议

每个语言的 .so 导出统一的元数据函数：

```c
// 所有语言模块必须导出
const char* pb_module_info();     // 返回 JSON: {"name":"rustmath","version":"1.0","functions":[...]}
void* pb_module_func(const char* name);  // 按名获取函数指针
```

---

## 数据桥接（端点间数据桥接）

**用一个语言编写桥接逻辑，连接两个或多个端点之间的数据桥接。**
端点不限于文件，支持内存、字符串、标准流等多种类型；transform 不限于单一回调，
支持管道链式编排和声明式 JSON 规则。

这是 Bridge 框架在「跨语言函数调用」之上新增的「数据流」能力：从多个源端点读取
→ 用一种语言编写的 transform 逻辑转换 → 写入多个目标端点。该模块可脱离 Bridge
框架独立编译为 `libdlc_bridge.so`。

### 数据流

```
源端点 A ─┐
          ├─→ transform ─→ 目标端点 1
源端点 B ─┤               └→ 目标端点 2
源端点 C ─┘               └→ 目标端点 3
```

### 端点类型（不限于文件）

| 类型 | 源端点 API | 目标端点 API | 说明 |
|------|-----------|-------------|------|
| **FILE** | `add_source_file` | `add_sink_file` | 磁盘文件 |
| **MEM** | `add_source_mem(data,len)` | `add_sink_mem` + `get_sink_mem` | 内存 buffer |
| **STRING** | `add_source_str(str)` | `add_sink_str` + `get_sink_str` | 字符串 |
| **STDIO** | `add_source_stdio(stdin)` | `add_sink_stdio(stdout/stderr)` | 标准流 |

端点可任意混合（如文件源 → 内存目标、内存源 → 文件目标）。
`add_source` / `add_sink` 向后兼容（等价 FILE）。

### 四种 transform 模式

| 模式 | API | 适用场景 |
|------|-----|---------|
| **C 回调** | `set_transform_cb(b, fn)` | 永远可用，无需语言运行时；高性能纯 C 处理 |
| **DLC 语言脚本** | `set_transform_lang(b, lang, code)` | 用 Python/JS/C#/SQL 等语言编写；需连接器已注册 |
| **管道 PIPELINE** | `add_stage(b, fn)` ×N | 多 stage 链式编排：stage1 → stage2 → ... → stageN |
| **JSON 规则** | `set_transform_rule(b, rule_json)` | 声明式规则 map/filter/field/rename；委托 JS 连接器 |

### C 回调模式示例

```c
#include "dlc_bridge.h"

/* transform：接收所有源端点内容，返回转换结果 */
DLC_Value* my_transform(DLC_Value* input) {
    /* input 是 DLC_ARRAY，每项为源端点的 DLC_STRING 内容 */
    int n = dlc_array_len(input);
    DLC_Value* joined = dlc_string("");
    for (int i = 0; i < n; i++) {
        DLC_Value* src = dlc_array_get(input, i);
        /* ... 处理 src->data.string_val ... */
    }
    return joined;
}

DLC_Bridge* b = dlc_bridge_create("etl");
dlc_bridge_add_source_file(b, "users.csv", NULL);
dlc_bridge_add_source_str(b, "inline config", NULL);  /* 内存源 */
dlc_bridge_add_sink_mem(b, NULL);                     /* 内存目标 */
dlc_bridge_add_sink_file(b, "out.json", NULL);
dlc_bridge_set_transform_cb(b, my_transform);
dlc_bridge_run(b);
const char* result = dlc_bridge_get_sink_mem(b, 0, &len);  /* 取内存目标 */
dlc_bridge_destroy(b);
```

### 管道 PIPELINE 模式示例

```c
/* 多 stage 串联：拼接 → 转大写 → 加后缀 */
dlc_bridge_add_stage(b, stage_join);
dlc_bridge_add_stage(b, stage_upper);
dlc_bridge_add_stage(b, stage_suffix);
```

### 运行时能力

- **重试**：`dlc_bridge_set_retry(b, N)` — transform 失败时自动重试 N 次
- **异步**：`dlc_bridge_run_async(b, callback, user_data)` + `dlc_bridge_wait(b)` — 后台线程执行

### 独立化

数据桥接模块只依赖 `dlc_value.c` + `dlc_registry.c`，不依赖 jni/pybridge：

```bash
cd dlc && make bridge-so    # 生成 build/libdlc_bridge.so
```

### 输出写入规则

transform 返回值写入目标端点的方式：
- `DLC_STRING` → 直接写入文本
- 其他类型（数组/对象/数字…）→ 先 `dlc_to_json` 序列化为 JSON 再写入

### 完整示例

见 [modules/bridge/bridge_demo.c](../modules/bridge/bridge_demo.c)：
用 C 编写桥接逻辑，2 个源文件 → 去重排序 → 文本目标，同时 → JSON 目标。

### API 速查

| 函数 | 作用 |
|------|------|
| `dlc_bridge_create(name)` | 创建桥接器 |
| `dlc_bridge_add_source*(b, ...)` | 添加源端点（file/mem/str/stdio） |
| `dlc_bridge_add_sink*(b, ...)` | 添加目标端点（file/mem/str/stdio） |
| `dlc_bridge_get_sink_mem/str(b, idx)` | 取出内存/字符串目标数据 |
| `dlc_bridge_set_transform_cb(b, fn)` | C 回调 transform |
| `dlc_bridge_set_transform_lang(b, lang, code)` | 语言脚本 transform |
| `dlc_bridge_add_stage(b, fn)` | 添加 pipeline stage |
| `dlc_bridge_set_transform_rule(b, json)` | JSON 规则 transform |
| `dlc_bridge_set_retry(b, n)` | 设置失败重试次数 |
| `dlc_bridge_run(b)` | 同步执行，返回源端点数（<0 出错） |
| `dlc_bridge_run_async(b, cb, ud)` | 异步执行 |
| `dlc_bridge_wait(b)` | 等待异步完成 |
| `dlc_bridge_error(b)` | 获取错误信息 |
| `dlc_bridge_destroy(b)` | 销毁桥接器 |
