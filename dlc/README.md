# DLC (Dynamic Language Connector) — 多语言桥接框架

> **让 9 种语言无缝互操作** — Python · C · C++ · Java · JavaScript · C# · SQL · TypeScript · Rust

---

## 架构

```
                        ┌─────────────────────────┐
                        │     DLC Bridge Registry  │
                        │   (dlc_registry.c)       │
                        │   ┌─────────────────┐    │
                        │   │ 路由表           │    │
                        │   │ python    → .so  │    │
                        │   │ javascript→ .so  │    │
                        │   │ csharp    → .so  │    │
                        │   │ sql       → .so  │    │
                        │   │ cpp       → .so  │    │
                        │   │ typescript→ .so  │    │
                        │   └─────────────────┘    │
                        └──────┬──────┬───────────┘
                               │      │
              ┌────────────────┘      └────────────────┐
              ▼                                        ▼
    ┌─────────────────┐                      ┌─────────────────┐
    │  Python DLC     │                      │  JavaScript DLC │
    │  (pybridge_python)│                    │  (QuickJS/Node) │
    │  dlopen→libpython│                     │  嵌入式JS引擎    │
    └────────┬────────┘                      └────────┬────────┘
             │                                        │
             │  跨语言调用:                            │
             │  Python → dlc_bridge_call("sql", ...)   │
             │  JS     → dlc_bridge_call("csharp", ...)│
             │                                        │
    ┌────────┴────────┬────────────┬────────────┐      │
    ▼                 ▼            ▼            ▼      │
  ┌──────┐      ┌──────────┐ ┌────────┐ ┌──────────┐  │
  │ C++  │      │  C# DLC  │ │SQL DLC │ │TS DLC    │  │
  │ DLC  │      │  (.NET)  │ │(SQLite)│ │(TS→JS)   │  │
  │(g++) │      │ 管道通信  │ │嵌入式DB│ │类型剥离   │  │
  └──────┘      └──────────┘ └────────┘ └──────────┘  │
```

## 统一协议

所有语言连接器实现同一个 `DLC_Connector` 接口：

```c
typedef struct DLC_Connector {
    const char* language;       // "python", "javascript", ...
    const char* version;        // 运行时版本
    const char* file_ext;       // ".py", ".js", ...
    const char* description;    // 一句话描述

    int  (*init)(void* config);                      // 初始化
    void (*finalize)(void);                          // 关闭
    DLC_Value* (*eval)(const char* code);            // 求值表达式
    DLC_Value* (*exec)(const char* code);            // 执行语句
    DLC_Value* (*call)(const char* fn, DLC_Value** args, int argc); // 调用函数
    int  (*set_global)(const char* name, DLC_Value* val);
    DLC_Value* (*get_global)(const char* name);
    void (*free_val)(DLC_Value* val);
    void* native_handle;
} DLC_Connector;
```

## 9 种语言支持矩阵

| 语言 | 连接器 | 实现方式 | 状态 |
|------|--------|---------|------|
| **Python** | `pybridge_python.c` | dlopen libpython | ✅ 13/13 测试通过 |
| **C** | `pb_module.h` | 原生编译 | ✅ 已集成 |
| **C++** | `cpp/dlc_cpp.cpp` | g++ JIT 编译为 .so | ✅ 代码就绪 |
| **Java** | `pybridge_jni.c` | JNI 桥接 | ✅ 6/6 测试通过 |
| **JavaScript** | `javascript/dlc_js.c` | QuickJS 嵌入 / Node.js 管道 | ✅ 代码就绪 |
| **C#** | `csharp/dlc_cs.c` | .NET Host / dotnet-script 管道 | ✅ 代码就绪 |
| **SQL** | `sql/dlc_sql.c` | SQLite3 嵌入 | ✅ 代码就绪 |
| **TypeScript** | `typescript/dlc_ts.c` | TS→JS 转译 + JS DLC | ✅ 代码就绪 |
| **Rust** | `modules/rust/` | C ABI 导出 | ✅ 已集成 |

## 跨语言调用示例

### C → SQL

```c
#include "dlc_registry.h"

// 查询
DLC_Value* r = dlc_bridge_eval("sql",
    "SELECT name, age FROM users WHERE age > 25 ORDER BY name");

// 遍历结果
for (int i = 0; i < dlc_array_len(r); i++) {
    DLC_Value* row = dlc_array_get(r, i);
    printf("  %s: %ld\n",
        dlc_object_get(row, "name")->data.string_val,
        dlc_object_get(row, "age")->data.int_val);
}
dlc_free(r);
```

### Python → JavaScript

```python
# Python 调用 JavaScript 函数
result = dlc_bridge_call("javascript", "Math.sqrt", [144])
print(result)  # 12.0

# Python 执行 JS 代码
data = dlc_bridge_eval("javascript",
    "JSON.stringify({name: 'Bridge', version: 2})")
print(data)  # {"name":"Bridge","version":2}
```

### Java → C#

```java
// Java 调用 C# 方法
DLC_Value result = dlcBridgeCall("csharp", "System.Math.Pow", 
    new DLC_Value[]{ DLCValue.int_(2), DLCValue.int_(10) });
System.out.println(result.asInt()); // 1024
```

### C++ → SQL

```cpp
// C++ 查询 SQL 数据库
auto result = dlc_bridge_eval("sql",
    "CREATE TABLE products (id INTEGER, name TEXT, price REAL);"
    "INSERT INTO products VALUES (1, 'Widget', 9.99);"
    "SELECT * FROM products;");
```

## 构建

```bash
cd dlc

# 安装依赖 (Ubuntu/Debian)
sudo apt install gcc g++ libsqlite3-dev

# 构建所有连接器
make all

# 运行测试
make test           # 跨语言连接器测试
make run-bridge     # 数据桥接测试

# 构建特定连接器
make js      # JavaScript
make sql     # SQL
make cpp     # C++
make cs      # C#
make ts      # TypeScript
```

## 数据桥接（文件间数据桥接）

**用一个语言编写一个桥接文件，连接两个或多个文件之间的数据。**

数据桥接器（`DLC_Bridge`）从多个**源文件**读取数据 → 用一种语言编写的
**transform** 逻辑转换 → 写入多个**目标文件**。

```
源文件 A ─┐
          ├─→ transform（C 回调 / DLC 语言脚本）─→ 目标文件 1
源文件 B ─┘                                     └→ 目标文件 2
```

### API（`dlc_bridge.h`）

```c
DLC_Bridge* b = dlc_bridge_create("my-bridge");
dlc_bridge_add_source(b, "input1.txt", NULL);
dlc_bridge_add_source(b, "input2.txt", NULL);
dlc_bridge_add_sink(b, "output.txt", NULL);

/* 方式 1：C 回调 transform（永远可用，无需任何语言运行时） */
dlc_bridge_set_transform_cb(b, my_transform_fn);

/* 方式 2：用指定 DLC 语言脚本 transform（需该语言连接器已注册） */
dlc_bridge_set_transform_lang(b, "javascript",
    "bridge_output = bridge_input.map(s => s.toUpperCase())");

int n = dlc_bridge_run(b);   /* 返回处理的源文件数，<0 出错 */
const char* err = dlc_bridge_error(b);  /* NULL 表示无错误 */
dlc_bridge_destroy(b);
```

transform 回调签名：`DLC_Value* (*DLC_TransformFn)(DLC_Value* input)`
- 输入：`DLC_ARRAY`，每个元素为一个源文件的 `DLC_STRING` 内容
- 输出：任意 `DLC_Value`（`STRING` 直接写入目标；其他类型先序列化为 JSON）

### 示例

完整示例见 [../modules/bridge/bridge_demo.c](../modules/bridge/bridge_demo.c)：
用 C 编写桥接逻辑，从 2 个源文件读取 → 去重排序 → 写入文本目标，
同时转 JSON → 写入 JSON 目标（2 源 → 2 目标）。

## 值类型系统

所有语言间通过统一的 `DLC_Value` 类型交换数据：

| DLC 类型 | C 表示 | JSON 等价 |
|----------|--------|-----------|
| `DLC_NULL` | null | `null` |
| `DLC_BOOL` | int | `true`/`false` |
| `DLC_INT` | int64_t | `42` |
| `DLC_DOUBLE` | double | `3.14` |
| `DLC_STRING` | char* | `"hello"` |
| `DLC_ARRAY` | DLC_Value*[] | `[1,2,3]` |
| `DLC_OBJECT` | {key:value} | `{"a":1}` |
| `DLC_ERROR` | {code, message} | `{"error":...}` |

## 添加新语言

只需 3 步：

1. **创建 `dlc/newlang/dlc_xx.c`**，实现 `DLC_Connector` 接口
2. **导出 `dlc_get_connector()`** 函数
3. **在 `Makefile` 中添加编译目标**

```c
// 最小示例
#include "../dlc_protocol.h"

static DLC_Value* my_eval(const char* code) {
    return dlc_string(code);  // echo
}

static DLC_Connector g_connector = {
    .language = "mylang",
    .version  = "1.0",
    .file_ext = ".my",
    .eval     = my_eval,
    .free_val = dlc_free,
};

DLC_Connector* dlc_get_connector(void) {
    return &g_connector;
}
```

## 文件结构

```
dlc/
├── dlc_protocol.h          # 统一协议定义（DLC_Value / DLC_Connector）
├── dlc_value.c             # 值构造/析构/JSON序列化
├── dlc_registry.h          # 注册表 API
├── dlc_registry.c          # 注册表实现 + 跨语言路由（dlc_bridge_*）
├── dlc_bridge.h            # 数据桥接 API（DLC_Bridge / 文件间数据桥接）
├── dlc_bridge.c            # 数据桥接实现（源读取→transform→目标写入）
├── Makefile                # 构建系统
├── test_dlc.c              # 连接器测试套件
├── test_bridge.c           # 数据桥接测试套件
├── cpp/                    # C++ 连接器
│   └── dlc_cpp.cpp
├── javascript/             # JavaScript 连接器
│   └── dlc_js.c
├── csharp/                 # C# 连接器
│   └── dlc_cs.c
├── sql/                    # SQL 连接器
│   └── dlc_sql.c
└── typescript/             # TypeScript 连接器
    └── dlc_ts.c
```