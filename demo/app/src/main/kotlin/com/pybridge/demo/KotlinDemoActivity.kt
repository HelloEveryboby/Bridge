package com.pybridge.demo

import android.os.Bundle
import android.util.Log
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import com.pybridge.core.Python
import com.pybridge.core.PyObject
import com.pybridge.types.toKt

/**
 * Kotlin 调用示例 — 原生 Kotlin DSL 风格
 *
 * 展示 Kotlin 特有的惯用写法：
 * - 运算符重载（[], +, -, *, /）
 * - 解构声明
 * - 内联泛型转换
 * - use 自动关闭
 * - 集合遍历
 */
class KotlinDemoActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "PyBridge-Kotlin"
    }

    private lateinit var resultText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_demo)

        resultText = findViewById(R.id.result_text)
        val inputField = findViewById<EditText>(R.id.input_field)

        // ─── 初始化 ───────────────────────────────────────────────
        Python.initialize(this)
        log("Python ${Python.getVersion()} initialized")

        // ─── 运行用户代码 ─────────────────────────────────────────
        findViewById<Button>(R.id.btn_run).setOnClickListener {
            val code = inputField.text.toString().trim()
            if (code.isNotEmpty()) {
                try {
                    val result = Python.exec(code)
                    log("Result: ${result ?: "OK"}")
                } catch (e: Exception) {
                    log("Error: ${e.message}")
                }
            }
        }

        // ─── 示例 1: Kotlin DSL 风格 ──────────────────────────────
        findViewById<Button>(R.id.btn_math).setOnClickListener {
            try {
                val math = Python.module("math")

                // 属性访问用 [] 运算符
                val pi = math["PI"].toDouble()
                log("π = $pi")

                // 方法调用
                val sqrt = math.callAttr("sqrt", 144.0).toDouble()
                log("sqrt(144) = $sqrt")

                // 链式调用 + 内联泛型转换
                val greeting = Python.module("demo_utils")
                    .callAttr("greet", "Kotlin")
                    .toStr()
                log(greeting)

                // 解构声明
                val utils = Python.module("demo_utils")
                val coords = utils.callAttr("get_coords") // 假设返回 tuple(1,2,3)
                val (x, y, z) = coords
                log("coords: x=${x.toInt()}, y=${y.toInt()}, z=${z.toInt()}")

                // 运算符重载
                val a = Python.wrap(10)
                val b = Python.wrap(3)
                log("10 + 3 = ${(a + b).toInt()}")
                log("10 * 3 = ${(a * b).toInt()}")
                log("10 % 3 = ${(a % b).toInt()}")

            } catch (e: Exception) {
                log("Error: ${e.message}")
            }
        }

        // ─── 示例 2: 集合操作 ─────────────────────────────────────
        findViewById<Button>(R.id.btn_numpy).setOnClickListener {
            try {
                val np = Python.module("numpy")

                // 创建数组
                val arr = np.callAttr("array", listOf(1, 2, 3, 4, 5))
                log("Array: $arr")

                // 统计
                val mean = np.callAttr("mean", arr).toDouble()
                val std = np.callAttr("std", arr).toDouble()
                log("Mean=$mean, Std=$std")

                // 矩阵运算
                val matrix = np.callAttr("array", listOf(
                    listOf(1, 2), listOf(3, 4)
                ))
                val det = np.callAttr("linalg.det", matrix).toDouble()
                log("det([[1,2],[3,4]]) = $det")

                // use 自动关闭
                Python.module("json").use { json ->
                    val data = json.callAttr("dumps", mapOf(
                        "mean" to mean,
                        "std" to std,
                        "det" to det
                    ), 2)
                    log("JSON:\n$data")
                }

            } catch (e: Exception) {
                log("NumPy Error: ${e.message}")
            }
        }

        // ─── 示例 3: Python 访问 Android ─────────────────────────
        findViewById<Button>(R.id.btn_json).setOnClickListener {
            try {
                // 在 Python 中调用 Java
                val code = """
                    from pybridge_java import java
                    Log = java.jclass('android.util.Log')
                    Log.i('PyBridge-Kotlin', 'Hello from Python!')
                    'Toast sent!'
                """.trimIndent()

                val result = Python.exec(code)
                log("Python→Java: $result")

                // 传递 Android Context 给 Python
                val demo = Python.module("demo_android")
                demo.callAttr("log_device_info", this)

            } catch (e: Exception) {
                log("Error: ${e.message}")
            }
        }
    }

    private fun log(msg: String) {
        Log.d(TAG, msg)
        runOnUiThread {
            val prev = resultText.text.toString()
            resultText.text = if (prev.isEmpty()) msg else "$prev\n$msg"
        }
    }
}
