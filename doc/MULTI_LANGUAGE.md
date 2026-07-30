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

## 数据桥接（文件间数据桥接）

**用一个语言编写一个桥接文件，连接两个或多个文件之间的数据。**

这是 Bridge 框架在「跨语言函数调用」之上新增的「数据流」能力：从多个源文件读取数据
→ 用一种语言编写的 transform 逻辑转换 → 写入多个目标文件。

### 数据流

```
源文件 A ─┐
          ├─→ transform ─→ 目标文件 1
源文件 B ─┤               └→ 目标文件 2
源文件 C ─┘               └→ 目标文件 3
```

### 两种 transform 模式

| 模式 | API | 适用场景 |
|------|-----|---------|
| **C 回调** | `dlc_bridge_set_transform_cb(b, fn)` | 永远可用，无需任何语言运行时；高性能纯 C 处理 |
| **DLC 语言脚本** | `dlc_bridge_set_transform_lang(b, lang, code)` | 用 Python/JS/C#/SQL 等语言编写转换逻辑；需对应连接器已注册 |

### C 回调模式示例

```c
#include "dlc_bridge.h"

/* transform：接收所有源文件内容，返回转换结果 */
DLC_Value* my_transform(DLC_Value* input) {
    /* input 是 DLC_ARRAY，每项为源文件的 DLC_STRING 内容 */
    int n = dlc_array_len(input);
    DLC_Value* joined = dlc_string("");  /* 简单拼接示例 */
    for (int i = 0; i < n; i++) {
        DLC_Value* src = dlc_array_get(input, i);
        /* ... 处理 src->data.string_val ... */
    }
    return joined;
}

DLC_Bridge* b = dlc_bridge_create("etl");
dlc_bridge_add_source(b, "users.csv", NULL);
dlc_bridge_add_source(b, "orders.csv", NULL);
dlc_bridge_add_sink(b, "merged.json", NULL);
dlc_bridge_set_transform_cb(b, my_transform);
dlc_bridge_run(b);
dlc_bridge_destroy(b);
```

### DLC 语言脚本模式示例

```c
/* 用 JavaScript 编写 transform：把输入数组每个元素转大写 */
dlc_bridge_set_transform_lang(b, "javascript",
    "bridge_output = bridge_input.map(function(s){ return s.toUpperCase(); })");
```

执行时桥接器会：
1. `dlc_bridge_set_global(lang, "bridge_input", input_array)` 注入数据
2. `dlc_bridge_exec(lang, code)` 执行用户脚本
3. `dlc_bridge_get_global(lang, "bridge_output")` 取回结果

### 输出写入规则

transform 返回值写入目标文件的方式：
- `DLC_STRING` → 直接写入文本
- 其他类型（数组/对象/数字…）→ 先 `dlc_to_json` 序列化为 JSON 再写入

### 完整示例

见 [modules/bridge/bridge_demo.c](../modules/bridge/bridge_demo.c)：
用 C 编写桥接逻辑，2 个源文件 → 去重排序 → 文本目标，同时 → JSON 目标。

### API 速查

| 函数 | 作用 |
|------|------|
| `dlc_bridge_create(name)` | 创建桥接器 |
| `dlc_bridge_add_source(b, path, lang)` | 添加源文件 |
| `dlc_bridge_add_sink(b, path, lang)` | 添加目标文件 |
| `dlc_bridge_set_transform_cb(b, fn)` | 设置 C 回调 transform |
| `dlc_bridge_set_transform_lang(b, lang, code)` | 设置语言脚本 transform |
| `dlc_bridge_run(b)` | 执行桥接，返回源文件数（<0 出错） |
| `dlc_bridge_error(b)` | 获取错误信息 |
| `dlc_bridge_destroy(b)` | 销毁桥接器 |
