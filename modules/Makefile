# PyBridge 多语言模块 Makefile
#
# 用法:
#   make          — 编译所有可用模块 + 测试
#   make c        — 只编译 C 模块
#   make rust     — 只编译 Rust 模块
#   make go       — 只编译 Go 模块
#   make test     — 编译并运行测试
#   make clean    — 清理

CC = gcc
CFLAGS = -Wall -O2 -fPIC
INCLUDES = -I../jni/include

# ── 默认目标 ────────────────────────────────────────────────────────

all: c test_modules
	@echo ""
	@echo "✅ 所有模块编译完成"

# ── C 模块 ──────────────────────────────────────────────────────────

c: libcmath.so

libcmath.so: c/cmath.c ../jni/include/pybridge/pb_module.h
	$(CC) -shared $(CFLAGS) -I../jni/include -o $@ c/cmath.c -lm
	@echo "✅ C 模块: libcmath.so"

# ── Rust 模块 ───────────────────────────────────────────────────────

rust: librustmath.so

librustmath.so: rust/rustmath.rs
	@if command -v rustc >/dev/null 2>&1; then \
		rustc --crate-type cdylib -o $@ $<; \
		echo "✅ Rust 模块: librustmath.so"; \
	else \
		echo "⚠ rustc 未安装，跳过 Rust 模块"; \
	fi

# ── Go 模块 ─────────────────────────────────────────────────────────

go: libgomath.so

libgomath.so: go/gomath.go
	@if command -v go >/dev/null 2>&1; then \
		cd go && go build -buildmode=c-shared -o ../libgomath.so gomath.go; \
		echo "✅ Go 模块: libgomath.so"; \
	else \
		echo "⚠ go 未安装，跳过 Go 模块"; \
	fi

# ── 测试程序 ────────────────────────────────────────────────────────

test_modules: test_modules.c ../jni/src/pb_loader.c libcmath.so
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_modules.c ../jni/src/pb_loader.c -ldl -lm
	@echo "✅ 测试程序: test_modules"

test: test_modules
	@echo ""
	@echo "══════════════════════════════════════════════"
	@echo "  运行多语言模块测试"
	@echo "══════════════════════════════════════════════"
	@echo ""
	LD_LIBRARY_PATH=. ./test_modules

# ── 清理 ────────────────────────────────────────────────────────────

clean:
	rm -f libcmath.so librustmath.so libgomath.so test_modules
	rm -f go/libgomath.so go/*.h
