# Bridge 框架编译和测试（dlopen 版，不需要 python-dev）
#
# Bridge = 通用层 (bridge_core.c) + Python 子模块 (pybridge_python.c)
# pb_* 符号保留以维持 ABI 兼容。
#
# 用法:
#   make          — 编译库和测试
#   make test     — 编译并运行测试
#   make clean    — 清理

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS = -ldl

# 通用层 + Python 子模块 + 模块加载器（pb_* 实现于此）
SRC = src/bridge_core.c src/pybridge_python.c src/pb_loader.c
LIB = libpybridge.so
TEST_SRC = test/test_pybridge.c
TEST_BIN = test_pybridge

.PHONY: all test clean

all: $(LIB) $(TEST_BIN)

$(LIB): $(SRC) include/bridge/bridge_core.h include/pybridge/pybridge_core.h
	$(CC) -shared -o $@ $(SRC) $(CFLAGS) $(LDFLAGS)
	@echo "✅ Built $(LIB) (Bridge 2.0)"

$(TEST_BIN): $(TEST_SRC) $(LIB) include/pybridge/pybridge_core.h
	$(CC) -o $@ $(TEST_SRC) -L. -lpybridge -Wl,-rpath,. $(CFLAGS) $(LDFLAGS) -lm
	@echo "✅ Built $(TEST_BIN)"

test: $(TEST_BIN)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  运行 Bridge / PyBridge 测试"
	@echo "══════════════════════════════════════════"
	@echo ""
	./$(TEST_BIN)

clean:
	rm -f $(LIB) $(TEST_BIN)
