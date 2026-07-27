# PyBridge Multi-Language — 多语言桥接架构

## 核心思想

**所有非 JVM 语言与 Java/Kotlin 的通信都必须经过 JNI。**

区别只在于各语言如何暴露函数给 C 层调用：

```
Java/Kotlin
    │
    ▼ JNI
┌─────────────────────────────────────────────┐
│          PyBridge Core (C)                   │
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
│  │  - 加载: pb_module_load("rustmath") │    │
│  │  - 调用: pb_module_call("add",args) │    │
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
