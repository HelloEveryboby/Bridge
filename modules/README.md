# PyBridge 多语言模块示例

通过统一模块协议（PB_MODULE），用 C/Rust/Go 编写可被 PyBridge 加载的扩展模块。

## 模块协议

每个模块只需导出 2 个函数：

```c
PB_MODULE_INIT(rust) {
    // 模块初始化
}

PB_MODULE_CALL(rust) {
    // 处理函数调用
}
```

## 示例模块

| 语言 | 文件 | 函数数 | 说明 |
|------|------|--------|------|
| C | `c/cmath.c` | 14 | 数学函数（add、multiply、fibonacci 等） |
| Rust | `rust/rustmath.rs` | 13 | 数学函数 + 字符串处理 |
| Go | `go/gomath.go` | 16 | 数学函数 + 并发计算 |

## 构建

```bash
# 编译 C 模块
make all

# 编译 Rust 模块（需 Rust 工具链）
rustc --crate-type cdylib rustmath.rs -o librustmath.so

# 编译 Go 模块（需 Go 工具链）
go build -buildmode=c-shared -o libgomath.so gomath.go
```

## 测试

```bash
make test    # 运行跨语言测试
```