# Bridge Android 快速上手

让 Bridge（PyBridge 子模块）像 Chaquopy 一样在 Android 里跑 Python。

> Bridge 2.0：包名 `com.bridge.py.*`，Gradle 插件 `com.bridge.python`，默认 Python 3.13。
> 框架架构见 [BRIDGE.md](BRIDGE.md)。

---

## 总览

Bridge 的 Android 构建分两步：

```
┌─────────────────────────────────────────────────────────────────┐
│  步骤 1: NDK 交叉编译 (一次性)                                   │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │ CPython 源码  │ →  │ NDK clang    │ →  │ libpython3.13.so │  │
│  │ bridge_core   │ →  │ 交叉编译     │ →  │ libpybridge.so   │  │
│  │ pybridge_*    │    │              │    │ stdlib.zip       │  │
│  └──────────────┘    └──────────────┘    └──────────────────┘  │
│                                                       │         │
│  步骤 2: Gradle 打包 (每次构建)                         ▼         │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │ Python 源码   │ →  │ Gradle       │ →  │ APK 内嵌 Python  │  │
│  │ pip 包       │ →  │ Plugin       │ →  │ Kotlin/Java 调用 │  │
│  └──────────────┘    └──────────────┘    └──────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 前提条件

| 工具 | 版本 | 说明 |
|------|------|------|
| Android Studio | Hedgehog+ | 或单独安装 Android SDK |
| Android NDK | r25+ | [下载](https://developer.android.com/ndk/downloads) |
| Python | 3.10+ | 用于运行编译脚本 |
| JDK | 17+ | Android 构建需要 |

---

## 步骤 1: 编译原生库 (CPython + libpybridge)

### 1.1 设置 NDK 路径

```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909125
# 或通过命令行指定: --ndk /path/to/ndk
```

### 1.2 运行交叉编译

```bash
cd bridge/build-system

# 编译 arm64-v8a (最常见)
./build_android.sh --abi arm64-v8a

# 或者编译所有架构
./build_android.sh --all

# 编译 x86_64 (模拟器用)
./build_android.sh --abi x86_64
```

### 1.3 验证产物

编译完成后，输出目录结构：

```
build-system/output/
├── arm64-v8a/
│   ├── lib/libpython3.13.so    # CPython 运行时 (~20MB)
│   ├── lib/libpybridge.so      # Bridge 桥接层 (~100KB)
│   ├── assets/python313_stdlib.zip  # 精简标准库 (~2MB)
│   └── abi.json
├── x86_64/
│   └── ... (同上)
└── ...
```

---

## 步骤 2: 配置 Android 项目

### 2.1 项目结构

```
your-android-project/
├── build.gradle                         # 项目级
├── app/
│   ├── build.gradle                     # 应用级
│   └── src/
│       └── main/
│           ├── python/                  # Python 源码
│           │   ├── my_module.py
│           │   └── utils/
│           ├── java/com/example/...     # Java/Kotlin 源码
│           └── AndroidManifest.xml
├── build-system/
│   └── output/                          # 步骤 1 的产物复制到这里
│       └── arm64-v8a/...
├── runtime/                             # Bridge 运行时库 (com.bridge.py.*)
├── gradle-plugin/                       # Bridge Gradle 插件 (com.bridge.python)
└── settings.gradle
```

### 2.2 settings.gradle

```groovy
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "MyPythonApp"
include ':app'

// 包含 Bridge 本地模块 (开发时用)
includeBuild('../bridge') {
    dependencySubstitution {
        substitute module('com.bridge:runtime') using project(':runtime')
        substitute module('com.bridge:gradle-plugin') using project(':gradle-plugin')
    }
}
```

### 2.3 项目级 build.gradle

```groovy
plugins {
    id 'com.android.application' version '8.5.0' apply false
    id 'org.jetbrains.kotlin.android' version '2.0.21' apply false
}
```

### 2.4 应用级 app/build.gradle

```groovy
plugins {
    id 'com.android.application'
    id 'org.jetbrains.kotlin.android'
    id 'com.bridge.python'    // Bridge / PyBridge 插件
}

android {
    namespace 'com.example.mypythonapp'
    compileSdk 35

    defaultConfig {
        applicationId "com.example.mypythonapp"
        minSdk 24
        targetSdk 35
        versionCode 1
        versionName "1.0"

        ndk {
            abiFilters 'arm64-v8a', 'x86_64'
        }
    }
}

dependencies {
    implementation 'com.bridge:runtime:2.0.0'
    implementation 'androidx.appcompat:appcompat:1.6.1'
}

python {
    version "3.13"
    buildPython System.getenv("PYTHON3") ?: "python3"
    srcDirs = ["src/main/python"]
    abiFilters "arm64-v8a", "x86_64"

    pip {
        // 安装你需要的 Python 包
        // install "numpy==1.26.0"
        // install "requests"
    }
}
```

### 2.5 复制编译产物

将步骤 1 的产物复制到项目能访问的位置：

```bash
# 方式 1: 复制到 build-system/output/
cp -r bridge/build-system/output/ your-project/build-system/output/

# 方式 2: 复制到 app/prebuilt/ (Gradle 插件会自动检测)
mkdir -p your-project/app/prebuilt
cp -r bridge/build-system/output/* your-project/app/prebuilt/
```

---

## 步骤 3: 编写代码

### 3.1 Python 代码 (src/main/python/my_module.py)

```python
"""我的 Python 模块 — 在 Android 上运行"""

import json
import math


def greet(name: str) -> str:
    """打招呼"""
    return f"Hello {name} from Python!"


def compute_stats(data: list) -> dict:
    """计算统计数据"""
    n = len(data)
    mean = sum(data) / n
    variance = sum((x - mean) ** 2 for x in data) / n
    return {
        "count": n,
        "mean": mean,
        "std": math.sqrt(variance),
        "min": min(data),
        "max": max(data)
    }


def fibonacci(n: int) -> list:
    """斐波那契数列"""
    a, b = 0, 1
    result = []
    for _ in range(n):
        result.append(a)
        a, b = b, a + b
    return result


def process_json(json_str: str) -> str:
    """处理 JSON 数据"""
    data = json.loads(json_str)
    data["processed"] = True
    data["python_version"] = "3.13"
    return json.dumps(data, indent=2)
```

### 3.2 Kotlin 调用代码

```kotlin
package com.example.mypythonapp

import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import com.bridge.py.core.Python
import com.bridge.py.core.PyObject

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 初始化 Python (只需一次)
        Python.initialize(this)

        try {
            // 1. 导入自定义模块
            val myModule = Python.module("my_module")

            // 2. 调用函数
            val greeting = myModule.callAttr("greet", "Android")
            Log.d("PyBridge", greeting.toStr())

            // 3. 计算统计
            val stats = myModule.callAttr("compute_stats",
                listOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 10))
            val mean = stats["mean"].toDouble()
            Log.d("PyBridge", "Mean: $mean")

            // 4. 斐波那契
            val fib = myModule.callAttr("fibonacci", 10)
            Log.d("PyBridge", "Fibonacci: ${fib.toList()}")

            // 5. JSON 处理
            val jsonResult = myModule.callAttr("process_json",
                """{"name": "test", "value": 42}""")
            Log.d("PyBridge", "JSON: ${jsonResult.toStr()}")

            // 6. 标准库: math
            val math = Python.module("math")
            val pi = math["PI"].toDouble()
            val sqrt2 = math.callAttr("sqrt", 2.0).toDouble()
            Log.d("PyBridge", "π = $pi, √2 = $sqrt2")

            // 7. 执行任意 Python 代码
            val result = Python.exec("[x**2 for x in range(10)]")
            Log.d("PyBridge", "Squares: ${result.toList()}")

        } catch (e: Exception) {
            Log.e("PyBridge", "Error: ${e.message}", e)
        }
    }

    override fun onDestroy() {
        Python.shutdown()
        super.onDestroy()
    }
}
```

### 3.3 Java 调用代码

```java
package com.example.mypythonapp;

import com.bridge.py.javaapi.PyBridge;
import com.bridge.py.javaapi.PyObject;

public class PythonHelper {

    public static String callPython(String code) {
        PyBridge.initialize(context);

        // 执行 Python 代码
        PyObject result = PyBridge.exec(code);
        return result.toStr();
    }

    public static double compute(String module, String func, double... args) {
        PyObject mod = PyBridge.module(module);
        return mod.callAttr(func, args).toDouble();
    }
}
```

---

## 步骤 4: 构建和运行

```bash
# 构建 APK
./gradlew assembleDebug

# 安装到设备
./gradlew installDebug

# 或者直接在 Android Studio 中点击 Run ▶️
```

---

## 与 Chaquopy 的对比

| 特性 | Bridge / PyBridge | Chaquopy |
|------|----------|----------|
| **集成方式** | `build_android.sh` + Gradle Plugin | 纯 Gradle Plugin |
| **Python 版本** | 3.13 (可配，回退 3.12/3.11) | 3.8-3.14 |
| **pip 包** | 支持 (需交叉编译) | 开箱即用 (预编译) |
| **多语言** | 9 种语言互操作 | 仅 Python ↔ Java |
| **跨平台** | Android + 桌面 | Android only |
| **许可证** | Apache 2.0 | MIT |
| **APK 体积** | ~25MB (含 CPython) | ~25MB (含 CPython) |

**Bridge 的优势**: 多语言互操作、跨平台、更灵活。
**Bridge 需要额外做的事情**: 手动运行一次 NDK 交叉编译（或使用预编译 .so）。

---

## 常见问题

### Q: 编译 libpython 失败?

确保 NDK 版本 >= r25，且网络能访问 python.org 下载源码。

```bash
# 手动下载 CPython
curl -O https://www.python.org/ftp/python/3.13.1/Python-3.13.1.tar.xz
tar xf Python-3.13.1.tar.xz -C bridge/build-system/build/
```

### Q: APK 体积太大?

- 使用 `--all` 编译时只保留需要的 ABI（通常只需 arm64-v8a）
- 精简 stdlib.zip：`build_android.sh` 已自动排除 test/tkinter/idle
- 使用 `--enable-optimizations --with-lto` 减小 .so 体积

### Q: 找不到 pip 包?

Android 的 pip 包需要交叉编译（纯 Python 包可以直接用，C 扩展包需要 NDK 编译）。纯 Python 包（如 requests）可以直接 `pip install --target`。

### Q: 运行时 crash?

检查 logcat:
```bash
adb logcat | grep -E "pybridge|python|JNI"
```

常见原因:
- 缺少 libpython.so → 确认 `build_android.sh` 输出已复制到正确位置
- ABI 不匹配 → 确认设备的 ABI 包含在编译列表中
- Python 版本不匹配 → 确认 `python { version "3.13" }` 与编译版本一致

---

## 下一步

- [完整技术文档](COMPLETE_DOCUMENTATION.md)
- [架构设计](DESIGN.md)
- [DLC 多语言桥接](../dlc/README.md)
- [GitHub 仓库](https://github.com/caosbad/Bridge)