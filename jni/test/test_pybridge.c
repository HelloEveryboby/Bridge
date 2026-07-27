/**
 * PyBridge 完整测试
 * 编译: make test
 */
#include "pybridge/pybridge_core.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int TR=0, TP=0, TF=0;
#define TEST(n) do { printf("\n── %s ──\n", n); TR++; } while(0)
#define ASSERT(c,m) do { if(!(c)){printf("  ❌ %s\n",m);TF++;return;}} while(0)
#define PASS() do { printf("  ✅ PASS\n"); TP++; } while(0)

void test_init(void) {
    TEST("初始化");
    ASSERT(!pb_is_init(), "未初始化");
    ASSERT(pb_init(NULL)==0, "init");
    ASSERT(pb_is_init(), "已初始化");
    printf("  %s\n", pb_version());
    PASS();
}

void test_types(void) {
    TEST("类型转换");
    PyObject* i = pb_from_long(42);
    ASSERT(pb_to_long(i)==42, "int"); pb_decref(i);
    PyObject* d = pb_from_double(3.14);
    ASSERT(fabs(pb_to_double(d)-3.14)<1e-10, "double"); pb_decref(d);
    PyObject* b = pb_from_bool(1);
    ASSERT(pb_to_bool(b)==1, "bool"); pb_decref(b);
    PyObject* s = pb_from_string("hello");
    ASSERT(strcmp(pb_to_string(s),"hello")==0, "string"); pb_decref(s);
    ASSERT(pb_is_none(pb_from_none()), "none");
    PASS();
}

void test_import(void) {
    TEST("模块导入");
    PyObject* m = pb_import("math");
    ASSERT(m, "math");
    PyObject* pi = pb_getattr(m, "pi");
    ASSERT(pi && fabs(pb_to_double(pi)-3.14159265)<1e-8, "pi");
    printf("  π = %.10f\n", pb_to_double(pi));
    pb_decref(pi);
    ASSERT(pb_import("no_such_mod_xyz")==NULL, "不存在的模块");
    pb_err_clear();
    pb_decref(m);
    PASS();
}

void test_call(void) {
    TEST("函数调用");
    PyObject* m = pb_import("math");

    PyObject* fn = pb_getattr(m, "sqrt");
    PyObject* a = pb_tuple_new(1);
    pb_tuple_set(a, 0, pb_from_double(144.0));
    PyObject* r = pb_call(fn, a, NULL);
    ASSERT(r && fabs(pb_to_double(r)-12.0)<1e-10, "sqrt(144)=12");
    printf("  sqrt(144) = %f\n", pb_to_double(r));
    pb_decref(r); pb_decref(fn);

    PyObject* fa = pb_tuple_new(1);
    pb_tuple_set(fa, 0, pb_from_long(10));
    PyObject* fr = pb_call_method(m, "factorial", fa);
    ASSERT(fr && pb_to_long(fr)==3628800, "10!");
    printf("  10! = %ld\n", pb_to_long(fr));
    pb_decref(fr);

    pb_decref(m);
    PASS();
}

void test_eval(void) {
    TEST("表达式求值");
    PyObject* r;

    r = pb_eval("2**10");
    ASSERT(r && pb_to_long(r)==1024, "2**10");
    printf("  2**10 = %ld\n", pb_to_long(r)); pb_decref(r);

    r = pb_eval("'hello' + ' ' + 'world'");
    ASSERT(r && strcmp(pb_to_string(r),"hello world")==0, "str concat");
    printf("  '%s'\n", pb_to_string(r)); pb_decref(r);

    r = pb_eval("sum([x**2 for x in range(6)])");
    ASSERT(r && pb_to_long(r)==55, "sum squares");
    printf("  sum squares 0..5 = %ld\n", pb_to_long(r)); pb_decref(r);

    PASS();
}

void test_exec(void) {
    TEST("语句执行");
    pb_exec("x = 100");
    PyObject* r = pb_eval("x");
    ASSERT(r && pb_to_long(r)==100, "x=100");
    printf("  x = %ld\n", pb_to_long(r)); pb_decref(r);

    pb_exec("def add(a,b): return a+b");
    r = pb_eval("add(3,4)");
    ASSERT(r && pb_to_long(r)==7, "add(3,4)=7");
    printf("  add(3,4) = %ld\n", pb_to_long(r)); pb_decref(r);

    pb_exec(
        "def fib(n):\n"
        "    a,b = 0,1\n"
        "    r = []\n"
        "    for _ in range(n):\n"
        "        r.append(a)\n"
        "        a,b = b,a+b\n"
        "    return r\n"
    );
    r = pb_eval("fib(10) == [0,1,1,2,3,5,8,13,21,34]");
    ASSERT(r && pb_to_bool(r), "fib(10)");
    printf("  fib(10) = [0,1,1,2,3,5,8,13,21,34] ✓\n");
    pb_decref(r);
    PASS();
}

void test_dict(void) {
    TEST("字典操作");
    pb_exec("d = {'name': 'PyBridge', 'ver': 1, 'ok': True}");
    PyObject* r = pb_eval("d['name']");
    ASSERT(r && strcmp(pb_to_string(r),"PyBridge")==0, "dict['name']");
    printf("  d['name'] = '%s'\n", pb_to_string(r)); pb_decref(r);

    r = pb_eval("len(d)");
    ASSERT(r && pb_to_long(r)==3, "len=3");
    printf("  len(d) = %ld\n", pb_to_long(r)); pb_decref(r);

    r = pb_eval("sorted(d.keys()) == ['name','ok','ver']");
    ASSERT(r && pb_to_bool(r), "keys sorted");
    printf("  keys = ['name','ok','ver'] ✓\n");
    pb_decref(r);
    PASS();
}

void test_exceptions(void) {
    TEST("异常处理");
    PyObject* r = pb_eval("1/0");
    ASSERT(!r, "ZeroDivisionError");
    printf("  1/0 → error captured ✓\n");
    pb_err_clear();

    r = pb_eval("xyz_undefined");
    ASSERT(!r, "NameError");
    printf("  undefined → error captured ✓\n");
    pb_err_clear();

    r = pb_eval("def");
    ASSERT(!r, "SyntaxError");
    printf("  syntax → error captured ✓\n");
    pb_err_clear();
    PASS();
}

void test_json(void) {
    TEST("JSON 处理");
    pb_exec("import json");
    pb_exec("data = {'name':'PyBridge','ver':1,'tags':['python','java','kotlin']}");

    PyObject* r = pb_eval("json.dumps(data, indent=2)");
    ASSERT(r, "json.dumps");
    printf("  JSON:\n%s\n", pb_to_string(r));
    pb_decref(r);

    r = pb_eval("json.loads(json.dumps(data))['name']");
    ASSERT(r && strcmp(pb_to_string(r),"PyBridge")==0, "round-trip");
    printf("  round-trip name='%s' ✓\n", pb_to_string(r));
    pb_decref(r);
    PASS();
}

void test_math(void) {
    TEST("数值计算");
    pb_exec(
        "def dot(a,b): return sum(x*y for x,y in zip(a,b))\n"
        "def mean(a): return sum(a)/len(a)\n"
        "def std(a):\n"
        "    m=mean(a)\n"
        "    return (sum((x-m)**2 for x in a)/len(a))**0.5\n"
    );
    PyObject* r = pb_eval("dot([1,2,3],[4,5,6])");
    ASSERT(r && pb_to_long(r)==32, "dot=32");
    printf("  dot = %ld\n", pb_to_long(r)); pb_decref(r);

    r = pb_eval("mean([10,20,30,40,50])");
    ASSERT(r && fabs(pb_to_double(r)-30.0)<1e-10, "mean=30");
    printf("  mean = %.1f\n", pb_to_double(r)); pb_decref(r);

    r = pb_eval("round(std([2,4,4,4,5,5,7,9]), 2)");
    ASSERT(r, "std");
    printf("  std = %s\n", pb_to_string(r)); pb_decref(r);
    PASS();
}

void test_class(void) {
    TEST("类和对象");
    pb_exec(
        "class Calc:\n"
        "    def __init__(s,v=0): s.v=v\n"
        "    def add(s,x): s.v+=x; return s\n"
        "    def mul(s,x): s.v*=x; return s\n"
        "    def get(s): return s.v\n"
    );
    PyObject* r = pb_eval("Calc(5).add(3).mul(2).get()");
    ASSERT(r && pb_to_long(r)==16, "(5+3)*2=16");
    printf("  (5+3)*2 = %ld\n", pb_to_long(r)); pb_decref(r);
    PASS();
}

void test_list_ops(void) {
    TEST("列表操作");
    pb_exec("lst = [10, 20, 30, 40, 50]");
    PyObject* r = pb_eval("sum(lst)");
    ASSERT(r && pb_to_long(r)==150, "sum=150");
    printf("  sum = %ld\n", pb_to_long(r)); pb_decref(r);

    r = pb_eval("lst[2]");
    ASSERT(r && pb_to_long(r)==30, "lst[2]=30");
    printf("  lst[2] = %ld\n", pb_to_long(r)); pb_decref(r);

    r = pb_eval("[x*2 for x in lst if x > 20]");
    ASSERT(r, "filtered list");
    pb_decref(r);
    r = pb_eval("[x*2 for x in lst if x > 20] == [60, 80, 100]");
    ASSERT(r && pb_to_bool(r), "filtered");
    printf("  filtered = [60, 80, 100] ✓\n");
    pb_decref(r);
    PASS();
}

void test_string_ops(void) {
    TEST("字符串操作");
    pb_exec("s = 'Hello, PyBridge!'");
    PyObject* r = pb_eval("s.upper()");
    ASSERT(r && strcmp(pb_to_string(r),"HELLO, PYBRIDGE!")==0, "upper");
    printf("  upper = '%s'\n", pb_to_string(r)); pb_decref(r);

    r = pb_eval("s.replace('PyBridge', 'World')");
    ASSERT(r && strcmp(pb_to_string(r),"Hello, World!")==0, "replace");
    printf("  replace = '%s'\n", pb_to_string(r)); pb_decref(r);

    r = pb_eval("len(s)");
    ASSERT(r && pb_to_long(r)==16, "len=16");
    printf("  len = %ld\n", pb_to_long(r)); pb_decref(r);
    PASS();
}

int main(void) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   PyBridge 核心桥接层 — 完整测试     ║\n");
    printf("╚══════════════════════════════════════╝\n");

    test_init();
    test_types();
    test_import();
    test_call();
    test_eval();
    test_exec();
    test_dict();
    test_exceptions();
    test_json();
    test_math();
    test_class();
    test_list_ops();
    test_string_ops();

    pb_finalize();

    printf("\n╔══════════════════════════════════════╗\n");
    printf("║  %d/%d 通过", TP, TR);
    if (TF) printf(", %d 失败", TF);
    printf("                ║\n");
    printf("╚══════════════════════════════════════╝\n");
    return TF ? 1 : 0;
}
