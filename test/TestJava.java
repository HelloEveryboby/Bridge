/**
 * PyBridge Java 端到端测试（桌面版，无 Android 依赖）
 *
 * 编译运行:
 *   cd pybridge
 *   javac -d build/classes -cp build/classes \
 *       runtime/src/main/java/com/pybridge/javaapi/PyBridge.java \
 *       build/classes/com/pybridge/core/Python.java \
 *       build/classes/com/pybridge/core/PyObject.java \
 *       test/TestJava.java
 *   java -Djava.library.path=jni -cp build/classes TestJava
 */
public class TestJava {

    static { System.loadLibrary("test_jni"); }

    // ── JNI native 方法 ────────────────────────────────────────────
    static native int    nativeInit(String home, String[] paths);
    static native void   nativeFinalize();
    static native long   nativeImportModule(String name);
    static native Object nativeCallFunction(long ptr, String func, Object[] args);
    static native Object nativeExec(String code);
    static native String nativeGetVersion();
    static native void   nativeDecRef(long ptr);

    // ── 高层 API ───────────────────────────────────────────────────
    static long importModule(String name) {
        long ptr = nativeImportModule(name);
        if (ptr == 0) throw new RuntimeException("Import failed: " + name);
        return ptr;
    }

    static Object call(long ptr, String func, Object... args) {
        return nativeCallFunction(ptr, func, args);
    }

    static Object eval(String code) {
        return nativeExec(code);
    }

    // ── 测试框架 ───────────────────────────────────────────────────
    static int TR = 0, TP = 0, TF = 0;

    static void TEST(String name) {
        System.out.println("\n── " + name + " ──");
        TR++;
    }

    static void ASSERT(boolean cond, String msg) {
        if (!cond) {
            System.out.println("  ❌ " + msg);
            TF++;
            throw new RuntimeException("FAIL: " + msg);
        }
    }

    static void PASS() {
        System.out.println("  ✅ PASS");
        TP++;
    }

    // ── 测试用例 ───────────────────────────────────────────────────

    public static void main(String[] args) {
        System.out.println("╔══════════════════════════════════════════╗");
        System.out.println("║   PyBridge Java → Python 端到端测试      ║");
        System.out.println("╚══════════════════════════════════════════╝");

        try {
            testInit();
            testMath();
            testString();
            testExec();
            testJson();
            testPassJavaObjects();
        } catch (Exception e) {
            System.out.println("  💥 异常: " + e.getMessage());
            TF++;
        }

        nativeFinalize();

        System.out.println("\n╔══════════════════════════════════════════╗");
        System.out.printf("║  %d/%d 通过", TP, TR);
        if (TF > 0) System.out.printf(", %d 失败", TF);
        System.out.println("                ║");
        System.out.println("╚══════════════════════════════════════════╝");

        System.exit(TF > 0 ? 1 : 0);
    }

    static void testInit() {
        TEST("初始化");
        int rc = nativeInit(null, null);
        ASSERT(rc == 0, "nativeInit 应返回 0");
        String ver = nativeGetVersion();
        ASSERT(ver != null && ver.contains("3."), "版本应包含 3.x");
        System.out.println("  Python " + ver);
        PASS();
    }

    static void testMath() {
        TEST("数学计算");

        // import math
        eval("import math");
        long math = importModule("math");

        // math.sqrt(144) = 12.0
        Object sqrtResult = call(math, "sqrt", 144.0);
        ASSERT(sqrtResult instanceof Number, "sqrt 结果应为 Number");
        double sqrt = ((Number) sqrtResult).doubleValue();
        ASSERT(Math.abs(sqrt - 12.0) < 1e-10, "sqrt(144) = 12");
        System.out.printf("  sqrt(144) = %f%n", sqrt);

        // math.pi (通过 eval 获取属性)
        Object piResult = eval("math.pi");
        ASSERT(piResult instanceof Number, "pi 应为 Number");
        double pi = ((Number) piResult).doubleValue();
        ASSERT(Math.abs(pi - 3.14159265) < 1e-6, "π ≈ 3.14159");
        System.out.printf("  π = %.10f%n", pi);

        // math.factorial(10) = 3628800
        Object factResult = call(math, "factorial", 10);
        ASSERT(factResult instanceof Number, "factorial 应为 Number");
        long fact = ((Number) factResult).longValue();
        ASSERT(fact == 3628800, "10! = 3628800");
        System.out.println("  10! = " + fact);

        nativeDecRef(math);
        PASS();
    }

    static void testString() {
        TEST("字符串操作");

        // eval 字符串操作
        Object upper = eval("'hello'.upper()");
        ASSERT(upper instanceof String, "upper 应为 String");
        ASSERT("HELLO".equals(upper), "upper = HELLO");
        System.out.println("  'hello'.upper() = " + upper);

        // 字符串拼接
        Object concat = eval("'Hello' + ', ' + 'World!'");
        ASSERT("Hello, World!".equals(concat), "拼接");
        System.out.println("  拼接 = " + concat);

        // 字符串长度
        Object len = eval("len('PyBridge')");
        ASSERT(len instanceof Number, "len 应为 Number");
        ASSERT(((Number) len).intValue() == 8, "len = 8");
        System.out.println("  len('PyBridge') = " + len);

        PASS();
    }

    static void testExec() {
        TEST("代码执行");

        // 定义变量
        eval("x = 100");
        Object x = eval("x");
        ASSERT(x instanceof Number && ((Number) x).intValue() == 100, "x = 100");
        System.out.println("  x = " + x);

        // 定义函数
        eval("def add(a, b): return a + b");
        Object result = eval("add(3, 4)");
        ASSERT(result instanceof Number && ((Number) result).intValue() == 7, "add(3,4) = 7");
        System.out.println("  add(3,4) = " + result);

        // Fibonacci
        eval(
            "def fib(n):\n" +
            "    a, b = 0, 1\n" +
            "    r = []\n" +
            "    for _ in range(n):\n" +
            "        r.append(a)\n" +
            "        a, b = b, a+b\n" +
            "    return r\n"
        );
        Object fibCheck = eval("fib(10) == [0,1,1,2,3,5,8,13,21,34]");
        ASSERT(fibCheck instanceof Boolean && (Boolean) fibCheck, "fib(10)");
        System.out.println("  fib(10) = [0,1,1,2,3,5,8,13,21,34] ✓");

        PASS();
    }

    static void testJson() {
        TEST("JSON 处理");

        eval("import json");
        eval("data = {'name': 'PyBridge', 'ver': 1, 'langs': ['java', 'kotlin', 'python']}");

        Object jsonStr = eval("json.dumps(data, indent=2)");
        ASSERT(jsonStr instanceof String, "json.dumps 应为 String");
        String json = (String) jsonStr;
        ASSERT(json.contains("PyBridge"), "JSON 应包含 PyBridge");
        System.out.println("  JSON:\n" + json);

        Object roundTrip = eval("json.loads(json.dumps(data))['name']");
        ASSERT("PyBridge".equals(roundTrip), "round-trip");
        System.out.println("  round-trip name = " + roundTrip);

        PASS();
    }

    static void testPassJavaObjects() {
        TEST("Java 对象传递给 Python");

        // 传递 Java int
        Object sumResult = eval("sum([1,2,3,4,5])");
        ASSERT(sumResult instanceof Number && ((Number) sumResult).intValue() == 15, "sum = 15");
        System.out.println("  sum([1..5]) = " + sumResult);

        // 传递 Java double 数组
        Object meanResult = eval("sum([10.0, 20.0, 30.0]) / 3");
        ASSERT(meanResult instanceof Number, "mean 应为 Number");
        double mean = ((Number) meanResult).doubleValue();
        ASSERT(Math.abs(mean - 20.0) < 1e-10, "mean = 20.0");
        System.out.printf("  mean = %.1f%n", mean);

        // 传递 Java 字符串
        Object upperResult = eval("'java_string'.upper()");
        ASSERT("JAVA_STRING".equals(upperResult), "upper");
        System.out.println("  upper = " + upperResult);

        PASS();
    }
}
