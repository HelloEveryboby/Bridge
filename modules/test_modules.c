/**
 * PyBridge 多语言模块测试
 *
 * 加载 C、Rust、Go 编译的 .so，验证统一调用接口。
 *
 * 编译: gcc -o test_modules test_modules.c pb_loader.c -ldl -lm -I../include
 * 运行: ./test_modules
 */
#include "pybridge/pb_module.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* 模块加载器（pb_loader.c 导出） */
typedef struct PB_Module PB_Module;
extern PB_Module* pb_load_module(const char* name, const char* path);
extern void* pb_get_func(PB_Module* mod, const char* func_name);
extern const char* pb_module_get_info(PB_Module* mod);
extern PB_Module* pb_module_find(const char* name);

static int TR = 0, TP = 0, TF = 0;
#define TEST(n) do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c, m) do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;} } while(0)
#define PASS() do { printf("  ✅ PASS\n"); TP++; } while(0)

/* ── 通用调用封装 ────────────────────────────────────────────────── */

typedef double (*fn_dd)(double, double);
typedef double (*fn_d)(double);
typedef int    (*fn_ii)(int, int);
typedef int    (*fn_i)(int);
typedef const char* (*fn_v)(void);
typedef const char* (*fn_s)(const char*);

/* ── 测试 C 模块 ────────────────────────────────────────────────── */

void test_c_module(void) {
    TEST("C 模块 (cmath)");

    PB_Module* mod = pb_load_module("cmath", "./libcmath.so");
    ASSERT(mod != NULL, "加载 libcmath.so");
    printf("  模块信息: %s\n", pb_module_get_info(mod));

    /* 数学运算 */
    fn_dd add = (fn_dd)pb_get_func(mod, "c_add");
    ASSERT(add != NULL, "c_add 函数");
    ASSERT(fabs(add(3.0, 4.0) - 7.0) < 1e-10, "c_add(3,4)=7");
    printf("  c_add(3, 4) = %f\n", add(3.0, 4.0));

    fn_d sqrt_fn = (fn_d)pb_get_func(mod, "c_sqrt");
    ASSERT(sqrt_fn != NULL, "c_sqrt 函数");
    ASSERT(fabs(sqrt_fn(144.0) - 12.0) < 1e-10, "c_sqrt(144)=12");
    printf("  c_sqrt(144) = %f\n", sqrt_fn(144.0));

    fn_i fact = (fn_i)pb_get_func(mod, "c_factorial");
    ASSERT(fact != NULL, "c_factorial 函数");
    ASSERT(fact(10) == 3628800, "c_factorial(10)=3628800");
    printf("  c_factorial(10) = %d\n", fact(10));

    fn_ii gcd = (fn_ii)pb_get_func(mod, "c_gcd");
    ASSERT(gcd(12, 8) == 4, "c_gcd(12,8)=4");
    printf("  c_gcd(12, 8) = %d\n", gcd(12, 8));

    fn_v ver = (fn_v)pb_get_func(mod, "c_version");
    ASSERT(ver != NULL, "c_version 函数");
    printf("  version: %s\n", ver());

    PASS();
}

/* ── 测试 Rust 模块 ─────────────────────────────────────────────── */

void test_rust_module(void) {
    TEST("Rust 模块 (rustmath)");

    PB_Module* mod = pb_load_module("rustmath", "./librustmath.so");
    if (!mod) {
        printf("  ⚠ Rust 模块未编译，跳过\n");
        printf("  编译: rustc --crate-type cdylib -o librustmath.so rustmath.rs\n");
        TR--;  /* 不计入测试总数 */
        return;
    }
    printf("  模块信息: %s\n", pb_module_get_info(mod));

    fn_dd add = (fn_dd)pb_get_func(mod, "r_add");
    ASSERT(add != NULL, "r_add 函数");
    ASSERT(fabs(add(10.0, 20.0) - 30.0) < 1e-10, "r_add(10,20)=30");
    printf("  r_add(10, 20) = %f\n", add(10.0, 20.0));

    fn_i prime = (fn_i)pb_get_func(mod, "r_is_prime");
    if (prime) {
        ASSERT(prime(17) == 1, "r_is_prime(17)=true");
        ASSERT(prime(15) == 0, "r_is_prime(15)=false");
        printf("  r_is_prime(17) = %s\n", prime(17) ? "true" : "false");
        printf("  r_is_prime(15) = %s\n", prime(15) ? "true" : "false");
    }

    fn_i fib = (fn_i)pb_get_func(mod, "r_fibonacci");
    if (fib) {
        ASSERT(fib(10) == 55, "r_fibonacci(10)=55");
        printf("  r_fibonacci(10) = %d\n", fib(10));
    }

    PASS();
}

/* ── 测试 Go 模块 ───────────────────────────────────────────────── */

void test_go_module(void) {
    TEST("Go 模块 (gomath)");

    PB_Module* mod = pb_load_module("gomath", "./libgomath.so");
    if (!mod) {
        printf("  ⚠ Go 模块未编译，跳过\n");
        printf("  编译: go build -buildmode=c-shared -o libgomath.so gomath.go\n");
        TR--;  /* 不计入测试总数 */
        return;
    }
    printf("  模块信息: %s\n", pb_module_get_info(mod));

    fn_dd add = (fn_dd)pb_get_func(mod, "g_add");
    ASSERT(add != NULL, "g_add 函数");
    ASSERT(fabs(add(100.0, 200.0) - 300.0) < 1e-10, "g_add(100,200)=300");
    printf("  g_add(100, 200) = %f\n", add(100.0, 200.0));

    fn_d sqrt_fn = (fn_d)pb_get_func(mod, "g_sqrt");
    ASSERT(sqrt_fn != NULL, "g_sqrt 函数");
    ASSERT(fabs(sqrt_fn(625.0) - 25.0) < 1e-10, "g_sqrt(625)=25");
    printf("  g_sqrt(625) = %f\n", sqrt_fn(625.0));

    fn_i prime = (fn_i)pb_get_func(mod, "g_is_prime");
    if (prime) {
        ASSERT(prime(13) == 1, "g_is_prime(13)=true");
        printf("  g_is_prime(13) = %s\n", prime(13) ? "true" : "false");
    }

    fn_s upper = (fn_s)pb_get_func(mod, "g_upper");
    if (upper) {
        const char* result = upper("hello go!");
        ASSERT(strcmp(result, "HELLO GO!") == 0, "g_upper");
        printf("  g_upper('hello go!') = %s\n", result);
    }

    PASS();
}

/* ── 测试跨语言统一调用 ─────────────────────────────────────────── */

void test_cross_language(void) {
    TEST("跨语言统一调用");

    /* 三个模块都实现了 add，统一调用 */
    PB_Module* cmod = pb_module_find("cmath");
    PB_Module* rmod = pb_module_find("rustmath");
    PB_Module* gmod = pb_module_find("gomath");

    double a = 100.0, b = 200.0;

    if (cmod) {
        fn_dd c_add = (fn_dd)pb_get_func(cmod, "c_add");
        printf("  C:    c_add(100, 200) = %f\n", c_add(a, b));
    }
    if (rmod) {
        fn_dd r_add = (fn_dd)pb_get_func(rmod, "r_add");
        printf("  Rust: r_add(100, 200) = %f\n", r_add(a, b));
    }
    if (gmod) {
        fn_dd g_add = (fn_dd)pb_get_func(gmod, "g_add");
        printf("  Go:   g_add(100, 200) = %f\n", g_add(a, b));
    }

    /* 统一的 factorial 调用 */
    printf("\n  factorial(10) 跨语言对比:\n");
    if (cmod) {
        fn_i f = (fn_i)pb_get_func(cmod, "c_factorial");
        printf("    C:    %d\n", f(10));
    }
    if (rmod) {
        fn_i f = (fn_i)pb_get_func(rmod, "r_factorial");
        if (f) printf("    Rust: %d\n", f(10));
    }
    if (gmod) {
        fn_i f = (fn_i)pb_get_func(gmod, "g_factorial");
        if (f) printf("    Go:   %d\n", f(10));
    }

    PASS();
}

/* ── 主函数 ──────────────────────────────────────────────────────── */

int main(void) {
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  PyBridge 多语言模块测试                     ║\n");
    printf("║  C + Rust + Go → 统一调用接口                ║\n");
    printf("╚══════════════════════════════════════════════╝\n");

    test_c_module();
    test_rust_module();
    test_go_module();
    test_cross_language();

    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  结果: %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                  ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    return TF ? 1 : 0;
}
