cmake_minimum_required(VERSION 3.18)
project(pybridge LANGUAGES C)

# Android NDK 配置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Python 配置
set(PYTHON_VERSION "3.11" CACHE STRING "Python version")
set(PYTHON_HOME "" CACHE PATH "Python installation root")

# 源文件
set(PYBRIDGE_SOURCES
    src/pybridge_jni.c
    src/pybridge_python.c
    src/pybridge_types.c
    src/pybridge_gil.c
    src/pybridge_exception.c
)

# 头文件
set(PYBRIDGE_HEADERS
    include/pybridge/pybridge.h
    include/pybridge/pybridge_types.h
    include/pybridge/pybridge_gil.h
)

# 创建共享库
add_library(pybridge SHARED ${PYBRIDGE_SOURCES})

# Python 头文件和库
if(PYTHON_HOME)
    target_include_directories(pybridge PRIVATE
        ${PYTHON_HOME}/include/python${PYTHON_VERSION}
    )
    target_link_libraries(pybridge
        ${PYTHON_HOME}/lib/libpython${PYTHON_VERSION}.so
    )
else()
    # 使用 find_package 查找 Python
    find_package(Python3 ${PYTHON_VERSION} REQUIRED COMPONENTS Development)
    target_include_directories(pybridge PRIVATE ${Python3_INCLUDE_DIRS})
    target_link_libraries(pybridge Python3::Python)
endif()

# Android 系统库
find_library(log-lib log)
target_link_libraries(pybridge ${log-lib})

# JNI 头文件
target_include_directories(pybridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 编译选项
target_compile_definitions(pybridge PRIVATE
    PYBRIDGE_VERSION="1.0.0"
    PYBRIDGE_PYTHON_VERSION="${PYTHON_VERSION}"
)

target_compile_options(pybridge PRIVATE
    -Wall -Wextra -Werror
    -O2
    -fvisibility=hidden
)

# 导出 JNI 函数
set_target_properties(pybridge PROPERTIES
    C_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)
