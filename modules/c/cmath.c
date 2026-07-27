/**
 * C 语言示例模块
 *
 * 编译: gcc -shared -fPIC -o libcmath.so cmath.c -lm
 * 测试: pb_load_module("cmath", "./libcmath.so")
 */
#include "pybridge/pb_module.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ── 函数定义 ────────────────────────────────────────────────────── */

/* 基本数学运算 */
static double c_add(double a, double b) { return a + b; }
static double c_sub(double a, double b) { return a - b; }
static double c_mul(double a, double b) { return a * b; }
static double c_div(double a, double b) { return b != 0 ? a / b : 0; }

/* 高级数学 */
static double c_sqrt(double x) { return sqrt(x); }
static double c_pow(double base, double exp) { return pow(base, exp); }
static double c_sin(double x) { return sin(x); }
static double c_cos(double x) { return cos(x); }
static double c_log(double x) { return log(x); }
static double c_abs(double x) { return fabs(x); }

/* 整数运算 */
static int c_factorial(int n) {
    if (n <= 1) return 1;
    int r = 1;
    for (int i = 2; i <= n; i++) r *= i;
    return r;
}

static int c_fibonacci(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) { int t = a + b; a = b; b = t; }
    return b;
}

static int c_gcd(int a, int b) {
    while (b) { int t = b; b = a % b; a = t; }
    return a;
}

/* 字符串 */
static const char* c_version(void) {
    return "cmath 1.0.0 (C)";
}

/* ── 模块注册 ────────────────────────────────────────────────────── */

PB_MODULE("cmath", "1.0.0", "c",
    PB_FUNC_ENTRY(c_add,       "dd>d"),
    PB_FUNC_ENTRY(c_sub,       "dd>d"),
    PB_FUNC_ENTRY(c_mul,       "dd>d"),
    PB_FUNC_ENTRY(c_div,       "dd>d"),
    PB_FUNC_ENTRY(c_sqrt,      "d>d"),
    PB_FUNC_ENTRY(c_pow,       "dd>d"),
    PB_FUNC_ENTRY(c_sin,       "d>d"),
    PB_FUNC_ENTRY(c_cos,       "d>d"),
    PB_FUNC_ENTRY(c_log,       "d>d"),
    PB_FUNC_ENTRY(c_abs,       "d>d"),
    PB_FUNC_ENTRY(c_factorial, "i>i"),
    PB_FUNC_ENTRY(c_fibonacci, "i>i"),
    PB_FUNC_ENTRY(c_gcd,       "ii>i"),
    PB_FUNC_ENTRY(c_version,   "v>s"),
)
