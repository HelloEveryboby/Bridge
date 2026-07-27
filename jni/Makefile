# PyBridge 编译和测试（dlopen 版，不需要 python-dev）
#
# 用法:
#   make          — 编译库和测试
#   make test     — 编译并运行测试
#   make clean    — 清理

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS = -ldl

SRC = src/pybridge_core.c
LIB = libpybridge.so
TEST_SRC = test/test_pybridge.c
TEST_BIN = test_pybridge

.PHONY: all test clean

all: $(LIB) $(TEST_BIN)

$(LIB): $(SRC) include/pybridge/pybridge_core.h
	$(CC) -shared -o $@ $(SRC) $(CFLAGS) $(LDFLAGS)
	@echo "✅ Built $(LIB)"

$(TEST_BIN): $(TEST_SRC) $(LIB) include/pybridge/pybridge_core.h
	$(CC) -o $@ $(TEST_SRC) -L. -lpybridge -Wl,-rpath,. $(CFLAGS) $(LDFLAGS) -lm
	@echo "✅ Built $(TEST_BIN)"

test: $(TEST_BIN)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  运行 PyBridge 测试"
	@echo "══════════════════════════════════════════"
	@echo ""
	./$(TEST_BIN)

clean:
	rm -f $(LIB) $(TEST_BIN)
