package com.pybridge.demo;

import android.os.Bundle;
import android.util.Log;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;

import com.pybridge.javaapi.PyBridge;
import com.pybridge.core.PyObject;

/**
 * Java 调用示例 — 纯 Java 风格 API
 *
 * 展示如何从 Java 代码中使用 PyBridge。
 */
public class JavaDemoActivity extends AppCompatActivity {

    private static final String TAG = "PyBridge-Java";

    private TextView resultText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_demo);

        resultText = findViewById(R.id.result_text);
        EditText inputField = findViewById(R.id.input_field);

        // ─── 初始化 ───────────────────────────────────────────────
        PyBridge.initialize(this);
        log("Python " + PyBridge.getVersion() + " initialized");

        // ─── 运行用户代码 ─────────────────────────────────────────
        findViewById(R.id.btn_run).setOnClickListener(v -> {
            String code = inputField.getText().toString().trim();
            if (!code.isEmpty()) {
                try {
                    PyObject result = PyBridge.exec(code);
                    log("Result: " + (result != null ? result.toString() : "OK"));
                } catch (Exception e) {
                    log("Error: " + e.getMessage());
                }
            }
        });

        // ─── 示例 1: 数学计算 ─────────────────────────────────────
        findViewById(R.id.btn_math).setOnClickListener(v -> {
            try {
                // 方式一：直接调用
                PyObject math = PyBridge.module("math");
                double sqrt = math.callAttr("sqrt", 144.0).toDouble();
                log("sqrt(144) = " + sqrt);

                double pi = math.get("PI").toDouble();
                log("π = " + pi);

                // 方式二：快捷调用
                double log10 = PyBridge.call("math.log", 1000.0, 10.0).toDouble();
                log("log₁₀(1000) = " + log10);

                // 方式三：链式调用
                String result = PyBridge.module("demo_utils")
                    .callAttr("greet", "Java")
                    .toStr();
                log(result);

                // 自定义模块
                PyObject fib = PyBridge.module("demo_utils").callAttr("fibonacci", 15);
                log("Fibonacci(15) = " + fib.toString());

            } catch (Exception e) {
                log("Error: " + e.getMessage());
            }
        });

        // ─── 示例 2: NumPy ────────────────────────────────────────
        findViewById(R.id.btn_numpy).setOnClickListener(v -> {
            try {
                PyObject np = PyBridge.module("numpy");

                PyObject arr = np.callAttr("array", new double[]{1, 2, 3, 4, 5});
                log("Array: " + arr);

                double mean = np.callAttr("mean", arr).toDouble();
                log("Mean: " + mean);

                double std = np.callAttr("std", arr).toDouble();
                log("Std: " + std);

            } catch (Exception e) {
                log("NumPy Error: " + e.getMessage());
            }
        });

        // ─── 示例 3: JSON 处理 ────────────────────────────────────
        findViewById(R.id.btn_json).setOnClickListener(v -> {
            try {
                // Python dict → JSON string
                PyObject json = PyBridge.module("json");

                String code = "{'name': 'PyBridge', 'version': '1.0', 'features': ['java', 'kotlin']}";
                PyObject dict = PyBridge.exec(code);
                String jsonStr = json.callAttr("dumps", dict, 2).toStr();
                log("JSON:\n" + jsonStr);

                // JSON string → Python dict
                PyObject parsed = json.callAttr("loads", jsonStr);
                log("Parsed: " + parsed.toMap());

            } catch (Exception e) {
                log("JSON Error: " + e.getMessage());
            }
        });
    }

    private void log(String msg) {
        Log.d(TAG, msg);
        runOnUiThread(() -> {
            String prev = resultText.getText().toString();
            resultText.setText(prev.isEmpty() ? msg : prev + "\n" + msg);
        });
    }
}
