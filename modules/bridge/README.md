# Bridge 数据桥接示例

演示**用一个语言（C）编写一个桥接文件，连接两个或多个文件之间的数据桥接**。

## 概念

数据桥接器（`DLC_Bridge`）从多个**源文件**读取数据 → 用一种语言编写的
**transform** 逻辑转换 → 写入多个**目标文件**。

```
源文件 A ─┐
          ├─→ transform（C 回调 / DLC 语言脚本）─→ 目标文件 1
源文件 B ─┘                                     └→ 目标文件 2
```

## 本示例

[bridge_demo.c](bridge_demo.c) 用 C 语言编写桥接逻辑，完成两次桥接：

| 桥接 | 源 | transform | 目标 |
|------|----|-----------|------|
| 1 | 2 个文本文件 | 去重 + 字典序排序 + 加行号 | 1 个文本文件 |
| 2 | 同上 | 转为 JSON 数组 | 1 个 JSON 文件 |

## 编译运行

```bash
# 先编译 DLC 核心库（含数据桥接）
cd dlc && make core && cd ..

# 编译示例
cd modules/bridge
gcc -O2 -o bridge_demo bridge_demo.c -I../../dlc -L../../dlc/build -ldlc_core -ldl

# 运行
LD_LIBRARY_PATH=../../dlc/build ./bridge_demo
```

预期输出：2 个源文件（含重复 `apple`）经去重排序后输出 5 行，并生成 JSON 数组。

## API

数据桥接 API 定义于 [dlc/dlc_bridge.h](../../dlc/dlc_bridge.h)：

```c
DLC_Bridge* b = dlc_bridge_create("my-bridge");
dlc_bridge_add_source(b, "input1.txt", NULL);
dlc_bridge_add_source(b, "input2.txt", NULL);
dlc_bridge_add_sink(b, "output.txt", NULL);

/* 方式 1：C 回调 transform（永远可用） */
dlc_bridge_set_transform_cb(b, my_transform_fn);

/* 方式 2：用指定 DLC 语言脚本 transform（需该语言连接器已注册） */
dlc_bridge_set_transform_lang(b, "javascript", "bridge_output = bridge_input.map(...)");

int n = dlc_bridge_run(b);  /* 返回处理的源文件数，<0 出错 */
dlc_bridge_destroy(b);
```

transform 接收 `DLC_ARRAY`（每个元素为一个源文件的 `DLC_STRING` 内容），
返回任意 `DLC_Value`（STRING 直接写入目标；其他类型先序列化为 JSON）。
