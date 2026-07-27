cmake_minimum_required(VERSION 3.18)
project(pybridge LANGUAGES C)

# Android NDK 配置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Python 配置
set(PYTHON_VERSION "3.12" CACHE STRING "Python version")
set(PYTHON_HOME "" CACHE PATH "Python installation root (cross-compiled for Android)")

# ── 实际存在的源文件 ────────────────────────────────────────────────
set(PYBRIDGE_SOURCES
    src/pybridge_core.c    # 核心桥接层 (dlopen libpython)
    src/pybridge_jni.c     # JNI 封装 (Java/Kotlin ↔ Python 类型转换)
    src/pb_loader.c        # 多语言模块动态加载器
)

# 头文件
set(PYBRIDGE_HEADERS
    include/pybridge/pybridge_core.h
    include/pybridge/pb_module.h
)

# 创建共享库
add_library(pybridge SHARED ${PYBRIDGE_SOURCES})

# ── 头文件搜索路径 ──────────────────────────────────────────────────
target_include_directories(pybridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Python 头文件和库 ───────────────────────────────────────────────
if(PYTHON_HOME)
    target_include_directories(pybridge PRIVATE
        ${PYTHON_HOME}/include/python${PYTHON_VERSION}
    )
    target_link_libraries(pybridge
        ${PYTHON_HOME}/lib/libpython${PYTHON_VERSION}.so
    )
else()
    # 尝试 find_package (桌面开发用)
    find_package(Python3 ${PYTHON_VERSION} QUIET COMPONENTS Development)
    if(Python3_FOUND)
        target_include_directories(pybridge PRIVATE ${Python3_INCLUDE_DIRS})
        target_link_libraries(pybridge Python3::Python)
    endif()
endif()

# Android 系统库
find_library(log-lib log)
if(log-lib)
    target_link_libraries(pybridge ${log-lib})
endif()

# dl 库 (桌面端需要)
find_library(dl-lib dl)
if(dl-lib)
    target_link_libraries(pybridge ${dl-lib})
endif()

# ── 编译选项 ────────────────────────────────────────────────────────
target_compile_definitions(pybridge PRIVATE
    PYBRIDGE_VERSION="1.0.0"
    PYBRIDGE_PYTHON_VERSION="${PYTHON_VERSION}"
)

target_compile_options(pybridge PRIVATE
    -Wall -Wextra
    -O2
    -fvisibility=hidden
)

# 允许 JNI 函数导出
set_target_properties(pybridge PROPERTIES
    C_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs"
)