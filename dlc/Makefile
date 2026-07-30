# DLC (Dynamic Language Connector) — 统一构建系统
#
# 构建所有语言连接器为共享库 (.so)
#
# 用法:
#   make all       — 构建所有连接器
#   make test      — 构建并运行测试
#   make cpp       — 仅构建 C++ 连接器
#   make js        — 仅构建 JavaScript 连接器
#   make cs        — 仅构建 C# 连接器
#   make sql       — 仅构建 SQL 连接器
#   make ts        — 仅构建 TypeScript 连接器
#   make clean     — 清理所有构建产物

CC      = gcc
CXX     = g++
CFLAGS  = -Wall -Wextra -O2 -fPIC
CXXFLAGS = -std=c++17 -Wall -O2 -fPIC
LDFLAGS = -ldl -shared

# 自动检测依赖
HAS_SQLITE3  := $(shell pkg-config --exists sqlite3 && echo 1 || echo 0)
HAS_NODE     := $(shell which node >/dev/null 2>&1 && echo 1 || echo 0)
HAS_DOTNET   := $(shell which dotnet >/dev/null 2>&1 && echo 1 || echo 0)
HAS_QUICKJS  := $(shell test -d javascript/quickjs && echo 1 || echo 0)

# 输出目录
BUILD_DIR = build
SO_DIR    = $(BUILD_DIR)/connectors

# 核心库
CORE_SRCS  = dlc_value.c dlc_registry.c dlc_bridge.c
CORE_OBJS  = $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)

# 连接器 .so 目标
CPP_SO  = $(SO_DIR)/libdlc_cpp.so
JS_SO   = $(SO_DIR)/libdlc_js.so
CS_SO   = $(SO_DIR)/libdlc_cs.so
SQL_SO  = $(SO_DIR)/libdlc_sql.so
TS_SO   = $(SO_DIR)/libdlc_ts.so

# 测试程序
TEST_BIN       = $(BUILD_DIR)/test_dlc
TEST_BRIDGE_BIN = $(BUILD_DIR)/test_bridge

# 独立数据桥接共享库（脱离 Bridge 框架可用）
BRIDGE_SO = $(BUILD_DIR)/libdlc_bridge.so

.PHONY: all clean test cpp js cs sql ts core test_bridge run-bridge bridge-so

all: core cpp js cs sql ts
	@echo ""
	@echo "╔══════════════════════════════════════════════╗"
	@echo "║  DLC 构建完成!                              ║"
	@echo "║  连接器目录: $(SO_DIR)/                      ║"
	@echo "║  运行测试: make test                        ║"
	@echo "╚══════════════════════════════════════════════╝"

# ─── 核心库 ────────────────────────────────────────────────────────

core: $(CORE_OBJS)
	@echo "  CORE   libdlc_core.a"
	@ar rcs $(BUILD_DIR)/libdlc_core.a $(CORE_OBJS)

$(BUILD_DIR)/%.o: %.c dlc_protocol.h dlc_registry.h dlc_bridge.h
	@mkdir -p $(BUILD_DIR) $(SO_DIR)
	@echo "  CC     $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ─── C++ 连接器 ────────────────────────────────────────────────────

cpp: $(CPP_SO)

$(CPP_SO): cpp/dlc_cpp.cpp dlc_protocol.h
	@mkdir -p $(SO_DIR)
	@echo "  CXX    cpp/dlc_cpp.cpp"
	@$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ cpp/dlc_cpp.cpp -ldl

# ─── JavaScript 连接器 ─────────────────────────────────────────────

js: $(JS_SO)

$(JS_SO): javascript/dlc_js.c dlc_protocol.h
	@mkdir -p $(SO_DIR)
	@echo "  CC     javascript/dlc_js.c"
ifeq ($(HAS_QUICKJS), 1)
	@echo "  [QuickJS mode]"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ javascript/dlc_js.c \
		-DDLC_USE_QUICKJS \
		-Ijavascript/quickjs \
		-Ljavascript/quickjs -lquickjs \
		-ldl -lm -lpthread
else
	@echo "  [Node.js fallback mode]"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ javascript/dlc_js.c -ldl
endif

# ─── C# 连接器 ─────────────────────────────────────────────────────

cs: $(CS_SO)

$(CS_SO): csharp/dlc_cs.c dlc_protocol.h
	@mkdir -p $(SO_DIR)
	@echo "  CC     csharp/dlc_cs.c [pipe mode]"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ csharp/dlc_cs.c -ldl

# ─── SQL 连接器 ────────────────────────────────────────────────────

sql: $(SQL_SO)

$(SQL_SO): sql/dlc_sql.c dlc_protocol.h
	@mkdir -p $(SO_DIR)
ifeq ($(HAS_SQLITE3), 1)
	@echo "  CC     sql/dlc_sql.c [SQLite3]"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ sql/dlc_sql.c \
		$(shell pkg-config --cflags --libs sqlite3) -ldl
else
	@echo "  [WARNING] SQLite3 not found. Install: sudo apt install libsqlite3-dev"
	@echo "  CC     sql/dlc_sql.c [stub - no SQLite]"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ sql/dlc_sql.c -ldl -DNO_SQLITE
endif

# ─── TypeScript 连接器 ─────────────────────────────────────────────

ts: $(TS_SO)

$(TS_SO): typescript/dlc_ts.c dlc_protocol.h dlc_registry.h
	@mkdir -p $(SO_DIR)
	@echo "  CC     typescript/dlc_ts.c"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ typescript/dlc_ts.c -ldl

# ─── 测试 ──────────────────────────────────────────────────────────

test: $(TEST_BIN)
	@echo ""
	@LD_LIBRARY_PATH=$(SO_DIR) $(TEST_BIN)

$(TEST_BIN): test_dlc.c $(CORE_OBJS) $(CPP_SO) $(JS_SO) $(CS_SO) $(SQL_SO) $(TS_SO)
	@echo "  CC     test_dlc.c"
	@$(CC) $(CFLAGS) -rdynamic -o $@ test_dlc.c $(CORE_OBJS) -ldl -I.

# ─── 数据桥接测试 ──────────────────────────────────────────────────

test_bridge: $(TEST_BRIDGE_BIN)

$(TEST_BRIDGE_BIN): test_bridge.c $(CORE_OBJS) dlc_bridge.h
	@echo "  CC     test_bridge.c"
	@$(CC) $(CFLAGS) -o $@ test_bridge.c $(CORE_OBJS) -ldl -lpthread -I.

run-bridge: test_bridge
	@echo ""
	@$(TEST_BRIDGE_BIN)

# ─── 独立数据桥接共享库（脱离 Bridge 框架） ─────────────────────────

bridge-so: $(BRIDGE_SO)

$(BRIDGE_SO): dlc_bridge.c dlc_bridge.h dlc_value.c dlc_registry.c dlc_protocol.h dlc_registry.h
	@mkdir -p $(BUILD_DIR)
	@echo "  SO     libdlc_bridge.so (独立共享库)"
	@$(CC) $(CFLAGS) -shared -o $@ dlc_value.c dlc_registry.c dlc_bridge.c -ldl -lpthread -I.

# ─── 清理 ──────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned DLC build artifacts"

# ─── 帮助 ──────────────────────────────────────────────────────────

help:
	@echo "DLC Build System"
	@echo "================"
	@echo ""
	@echo "Targets:"
	@echo "  make all      构建所有连接器"
	@echo "  make test     运行测试"
	@echo "  make cpp      C++ 连接器"
	@echo "  make js       JavaScript 连接器 (QuickJS/Node)"
	@echo "  make cs       C# 连接器 (.NET)"
	@echo "  make sql      SQL 连接器 (SQLite)"
	@echo "  make ts       TypeScript 连接器"
	@echo "  make test_bridge  构建数据桥接测试 (test_bridge)"
	@echo "  make run-bridge   构建并运行数据桥接测试"
	@echo "  make bridge-so    独立共享库 libdlc_bridge.so (脱离 Bridge 框架)"
	@echo "  make clean    清理"
	@echo ""
	@echo "Detected dependencies:"
	@echo "  SQLite3:    $(if $(filter 1,$(HAS_SQLITE3)),✓,✗)"
	@echo "  Node.js:    $(if $(filter 1,$(HAS_NODE)),✓,✗)"
	@echo "  .NET SDK:   $(if $(filter 1,$(HAS_DOTNET)),✓,✗)"
	@echo "  QuickJS:    $(if $(filter 1,$(HAS_QUICKJS)),✓,✗)"