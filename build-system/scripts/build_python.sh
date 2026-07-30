#!/usr/bin/env bash
# =============================================================================
# Bridge / PyBridge CPython 交叉编译脚本
#
# 将 CPython 编译为 Android 可用的共享库
#
# 用法:
#   ./build_python.sh --abi arm64-v8a --version 3.13
#   ./build_python.sh --abi x86_64 --version 3.13 --clean
#
# 依赖:
#   - Android NDK r25+
#   - 自动下载 CPython 源码
# =============================================================================

set -euo pipefail

# 默认配置
PYTHON_VERSION="${PYTHON_VERSION:-3.13.1}"
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
ANDROID_API="${ANDROID_API:-24}"
ANDROID_NDK="${ANDROID_NDK:-$ANDROID_NDK_HOME}"
BUILD_DIR="${BUILD_DIR:-$(pwd)/build/python}"
OUTPUT_DIR="${OUTPUT_DIR:-$(pwd)/output}"
JOBS="${JOBS:-$(nproc)}"
CLEAN="${CLEAN:-false}"

# ABI 到 target triple 的映射
declare -A ABI_TRIPLES=(
    ["arm64-v8a"]="aarch64-linux-android"
    ["armeabi-v7a"]="arm-linux-androideabi"
    ["x86_64"]="x86_64-linux-android"
    ["x86"]="i686-linux-android"
)

# ABI 到架构的映射
declare -A ABI_ARCHS=(
    ["arm64-v8a"]="aarch64"
    ["armeabi-v7a"]="arm"
    ["x86_64"]="x86_64"
    ["x86"]="i686"
)

# =============================================================================
# 参数解析
# =============================================================================

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --abi ABI          Target ABI (arm64-v8a, armeabi-v7a, x86_64, x86)"
    echo "  --version VER      Python version (e.g., 3.13.1)"
    echo "  --ndk PATH         Android NDK path"
    echo "  --api LEVEL        Android API level (default: 24)"
    echo "  --jobs N           Parallel build jobs"
    echo "  --clean            Clean build directory"
    echo "  --output DIR       Output directory"
    echo "  -h, --help         Show this help"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --abi) ANDROID_ABI="$2"; shift 2 ;;
        --version) PYTHON_VERSION="$2"; shift 2 ;;
        --ndk) ANDROID_NDK="$2"; shift 2 ;;
        --api) ANDROID_API="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --clean) CLEAN="true"; shift ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# 验证参数
if [[ -z "${ANDROID_NDK}" ]]; then
    echo "❌ Error: Android NDK not set. Use --ndk or set ANDROID_NDK"
    exit 1
fi

TRIPLE="${ABI_TRIPLES[$ANDROID_ABI]}"
ARCH="${ABI_ARCHS[$ANDROID_ABI]}"
PYTHON_MAJOR_MINOR="${PYTHON_VERSION%.*}"

echo "============================================"
echo "Bridge / PyBridge CPython Cross-Compilation"
echo "============================================"
echo "Python:  ${PYTHON_VERSION}"
echo "ABI:     ${ANDROID_ABI}"
echo "Triple:  ${TRIPLE}"
echo "NDK:     ${ANDROID_NDK}"
echo "API:     ${ANDROID_API}"
echo "Jobs:    ${JOBS}"
echo "============================================"

# =============================================================================
# 工具函数
# =============================================================================

log_info() {
    echo "ℹ️  $*"
}

log_step() {
    echo ""
    echo "🔷 $*"
}

log_success() {
    echo "✅ $*"
}

log_error() {
    echo "❌ $*" >&2
}

# =============================================================================
# 步骤 1: 下载 CPython 源码
# =============================================================================

download_python() {
    local src_dir="${BUILD_DIR}/cpython-${PYTHON_VERSION}"

    if [[ -d "${src_dir}" ]]; then
        log_info "Python source already exists: ${src_dir}"
        return
    fi

    log_step "Downloading CPython ${PYTHON_VERSION}..."

    local url="https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tar.xz"
    local tarball="${BUILD_DIR}/Python-${PYTHON_VERSION}.tar.xz"

    mkdir -p "${BUILD_DIR}"
    curl -L -o "${tarball}" "${url}"
    tar -xf "${tarball}" -C "${BUILD_DIR}"
    mv "${BUILD_DIR}/Python-${PYTHON_VERSION}" "${src_dir}"
    rm -f "${tarball}"

    log_success "Downloaded to ${src_dir}"
}

# =============================================================================
# 步骤 2: 配置交叉编译
# =============================================================================

configure_python() {
    local src_dir="${BUILD_DIR}/cpython-${PYTHON_VERSION}"
    local build_dir="${BUILD_DIR}/build-${ANDROID_ABI}"
    local toolchain="${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64"

    if [[ "${CLEAN}" == "true" ]]; then
        rm -rf "${build_dir}"
    fi

    mkdir -p "${build_dir}"

    log_step "Configuring CPython for ${ANDROID_ABI}..."

    cd "${src_dir}"

    # 清理之前的构建
    make distclean 2>/dev/null || true

    # 设置交叉编译工具链
    export CC="${toolchain}/bin/${TRIPLE}${ANDROID_API}-clang"
    export CXX="${toolchain}/bin/${TRIPLE}${ANDROID_API}-clang++"
    export AR="${toolchain}/bin/llvm-ar"
    export RANLIB="${toolchain}/bin/llvm-ranlib"
    export STRIP="${toolchain}/bin/llvm-strip"
    export READELF="${toolchain}/bin/llvm-readelf"

    # 验证工具链
    if [[ ! -f "${CC}" ]]; then
        log_error "Compiler not found: ${CC}"
        exit 1
    fi

    # Python 交叉编译配置
    # ac_cv_* 变量用于跳过运行时检测（交叉编译时无法运行目标程序）
    local config_opts=(
        --host="${TRIPLE}"
        --build="$(gcc -dumpmachine)"
        --prefix="${build_dir}/install"
        --enable-shared
        --enable-optimizations
        --with-lto
        --without-ensurepip
        --disable-test-modules
        --disable-ipv6
        ac_cv_file__dev_ptmx=no
        ac_cv_file__dev_ptc=no
        ac_cv_buggy_getaddrinfo=no
        ac_cv_have_long_long_format=yes
    )

    # 执行配置
    ./configure "${config_opts[@]}"

    log_success "Configuration complete"
}

# =============================================================================
# 步骤 3: 编译
# =============================================================================

build_python() {
    local src_dir="${BUILD_DIR}/cpython-${PYTHON_VERSION}"
    local build_dir="${BUILD_DIR}/build-${ANDROID_ABI}"

    log_step "Building CPython (${JOBS} jobs)..."

    cd "${src_dir}"

    # 编译
    make -j"${JOBS}" \
        CROSS_COMPILE_TARGET=yes \
        HOSTPYTHON="${BUILD_DIR}/host-python" 2>&1 | tail -20

    # 安装到本地目录
    make install \
        CROSS_COMPILE_TARGET=yes \
        DESTDIR="${build_dir}/install" 2>&1 | tail -10

    log_success "Build complete"
}

# =============================================================================
# 步骤 4: 打包
# =============================================================================

package_python() {
    local build_dir="${BUILD_DIR}/build-${ANDROID_ABI}"
    local output_dir="${OUTPUT_DIR}/${ANDROID_ABI}"

    log_step "Packaging Python for ${ANDROID_ABI}..."

    mkdir -p "${output_dir}/lib"
    mkdir -p "${output_dir}/assets"

    # 复制 libpython.so
    local libpython=$(find "${build_dir}/install" -name "libpython${PYTHON_MAJOR_MINOR}.so*" | head -1)
    if [[ -n "${libpython}" ]]; then
        cp "${libpython}" "${output_dir}/lib/"
        log_info "Copied libpython: ${libpython}"
    fi

    # 复制 Python 标准库（精简版）
    local stdlib_dir="${build_dir}/install/lib/python${PYTHON_MAJOR_MINOR}"
    if [[ -d "${stdlib_dir}" ]]; then
        # 创建精简版 zip
        cd "${stdlib_dir}"
        zip -r "${output_dir}/assets/python${PYTHON_MAJOR_MINOR//./}_stdlib.zip" \
            -x "test/*" \
            -x "tests/*" \
            -x "idle*/**" \
            -x "tkinter/**" \
            -x "turtle*" \
            -x "lib2to3/**" \
            -x "ensurepip/**" \
            -x "distutils/**" \
            -x "__pycache__/**" \
            -x "*.pyc" \
            -x "*.pyo" \
            -x "plat-*/**" \
            . 2>&1 | tail -5
        log_info "Created stdlib zip"
    fi

    # 复制头文件（用于编译扩展模块）
    local include_dir="${build_dir}/install/include"
    if [[ -d "${include_dir}" ]]; then
        cp -r "${include_dir}" "${output_dir}/include"
    fi

    # 生成配置文件
    cat > "${output_dir}/python_config.json" << EOF
{
    "version": "${PYTHON_VERSION}",
    "abi": "${ANDROID_ABI}",
    "triple": "${TRIPLE}",
    "api_level": ${ANDROID_API},
    "shared_lib": "lib/libpython${PYTHON_MAJOR_MINOR}.so",
    "stdlib": "assets/python${PYTHON_MAJOR_MINOR//./}_stdlib.zip",
    "built_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

    log_success "Packaged to ${output_dir}"
    log_info "Files:"
    ls -lh "${output_dir}/lib/"
    ls -lh "${output_dir}/assets/"
}

# =============================================================================
# 主流程
# =============================================================================

main() {
    download_python
    configure_python
    build_python
    package_python

    echo ""
    echo "============================================"
    log_success "Python ${PYTHON_VERSION} built for ${ANDROID_ABI}"
    echo "Output: ${OUTPUT_DIR}/${ANDROID_ABI}"
    echo "============================================"
}

main
