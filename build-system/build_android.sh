#!/usr/bin/env bash
# ============================================================================
# PyBridge NDK 交叉编译脚本
#
# 将 CPython + libpybridge 编译为 Android 可用的 .so
#
# 依赖:
#   - Android NDK r25+ (https://developer.android.com/ndk/downloads)
#   - 自动下载 CPython 源码
#
# 用法:
#   ./build_android.sh                           # 默认 arm64-v8a
#   ./build_android.sh --abi arm64-v8a           # 指定 ABI
#   ./build_android.sh --abi x86_64 --clean      # 重新编译
#   ./build_android.sh --all                     # 编译所有 ABI
#
# 输出:
#   output/arm64-v8a/libpython3.12.so
#   output/arm64-v8a/libpybridge.so
#   output/arm64-v8a/python312_stdlib.zip
# ============================================================================

set -euo pipefail

# ── 默认配置 ────────────────────────────────────────────────────────

PYTHON_VERSION="${PYTHON_VERSION:-3.12.3}"
PYTHON_MAJOR_MINOR="3.12"
ANDROID_API="${ANDROID_API:-24}"         # 最低 API level
ANDROID_NDK="${ANDROID_NDK:-}"           # NDK 路径
BUILD_DIR="$(pwd)/build"
OUTPUT_DIR="$(pwd)/output"
JOBS="$(nproc 2>/dev/null || echo 4)"
TARGET_ABI="${1:-arm64-v8a}"
CLEAN=false
BUILD_ALL=false

# ── ABI 配置表 ──────────────────────────────────────────────────────

declare -A ABI_TRIPLE=(
    ["arm64-v8a"]="aarch64-linux-android"
    ["armeabi-v7a"]="arm-linux-androideabi"
    ["x86_64"]="x86_64-linux-android"
    ["x86"]="i686-linux-android"
)

declare -A ABI_ARCH=(
    ["arm64-v8a"]="aarch64"
    ["armeabi-v7a"]="arm"
    ["x86_64"]="x86_64"
    ["x86"]="i686"
)

# ── 参数解析 ────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case $1 in
        --abi)      TARGET_ABI="$2"; shift 2 ;;
        --ndk)      ANDROID_NDK="$2"; shift 2 ;;
        --api)      ANDROID_API="$2"; shift 2 ;;
        --clean)    CLEAN=true; shift ;;
        --all)      BUILD_ALL=true; shift ;;
        --jobs)     JOBS="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--abi ABI] [--ndk PATH] [--api LEVEL] [--clean] [--all]"
            echo ""
            echo "ABIs: arm64-v8a, armeabi-v7a, x86_64, x86"
            echo "NDK:  Set --ndk or ANDROID_NDK environment variable"
            exit 0
            ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
done

# ── 验证环境 ────────────────────────────────────────────────────────

check_env() {
    # 自动检测 NDK
    if [[ -z "$ANDROID_NDK" ]]; then
        for path in "$ANDROID_HOME/ndk/"* "$HOME/Android/Sdk/ndk/"* "/opt/android-ndk" ; do
            if [[ -d "$path/toolchains/llvm" ]]; then
                ANDROID_NDK="$path"
                break
            fi
        done
    fi

    if [[ -z "$ANDROID_NDK" ]]; then
        echo "❌ Android NDK not found."
        echo "   Set ANDROID_NDK=/path/to/ndk or use --ndk"
        echo ""
        echo "   Download: https://developer.android.com/ndk/downloads"
        exit 1
    fi

    local toolchain="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
    if [[ ! -d "$toolchain" ]]; then
        toolchain="$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64"
    fi
    if [[ ! -d "$toolchain" ]]; then
        echo "❌ NDK toolchain not found at $ANDROID_NDK"
        exit 1
    fi

    TOOLCHAIN="$toolchain"
    echo "✅ NDK: $ANDROID_NDK"
    echo "   Toolchain: $TOOLCHAIN"
}

# ── 日志 ────────────────────────────────────────────────────────────

log()  { echo "🔷 $*"; }
ok()   { echo "✅ $*"; }
err()  { echo "❌ $*" >&2; }

# ── 步骤 1: 下载 CPython 源码 ───────────────────────────────────────

download_cpython() {
    local src="$BUILD_DIR/cpython-$PYTHON_VERSION"

    if [[ -d "$src/Include" ]]; then
        log "CPython source exists: $src"
        CPYTHON_SRC="$src"
        return
    fi

    log "Downloading CPython $PYTHON_VERSION ..."
    mkdir -p "$BUILD_DIR"

    local url="https://www.python.org/ftp/python/$PYTHON_VERSION/Python-$PYTHON_VERSION.tar.xz"
    local tarball="$BUILD_DIR/Python-$PYTHON_VERSION.tar.xz"

    curl -L --progress-bar -o "$tarball" "$url"
    tar xf "$tarball" -C "$BUILD_DIR"
    mv "$BUILD_DIR/Python-$PYTHON_VERSION" "$src"
    rm -f "$tarball"

    CPYTHON_SRC="$src"
    ok "Downloaded to $src"
}

# ── 步骤 2: 为单个 ABI 编译 CPython ────────────────────────────────

build_cpython_for_abi() {
    local abi="$1"
    local triple="${ABI_TRIPLE[$abi]}"
    local arch="${ABI_ARCH[$abi]}"
    local build="$BUILD_DIR/build-$abi"
    local install="$BUILD_DIR/install-$abi"

    log "Building CPython for $abi ($triple) ..."

    # 设置编译器
    local cc="$TOOLCHAIN/bin/${triple}${ANDROID_API}-clang"
    local cxx="$TOOLCHAIN/bin/${triple}${ANDROID_API}-clang++"

    if [[ ! -f "$cc" ]]; then
        # 某些 ABI 的编译器名不同
        cc="$TOOLCHAIN/bin/${arch}-linux-android${ANDROID_API}-clang"
        cxx="$TOOLCHAIN/bin/${arch}-linux-android${ANDROID_API}-clang++"
    fi

    if [[ ! -f "$cc" ]]; then
        err "Compiler not found for $abi"
        return 1
    fi

    export CC="$cc"
    export CXX="$cxx"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"

    if [[ "$CLEAN" == true ]]; then
        rm -rf "$build" "$install"
    fi

    mkdir -p "$build" "$install"

    # 配置（跳过交叉编译时无法运行的检测）
    cd "$CPYTHON_SRC"
    make distclean 2>/dev/null || true

    ./configure \
        --host="$triple" \
        --build="$(gcc -dumpmachine)" \
        --prefix="$install" \
        --enable-shared \
        --enable-optimizations \
        --with-lto \
        --without-ensurepip \
        --disable-test-modules \
        --disable-ipv6 \
        --with-build-python=$(which python3) \
        ac_cv_file__dev_ptmx=no \
        ac_cv_file__dev_ptc=no \
        ac_cv_buggy_getaddrinfo=no \
        ac_cv_have_long_long_format=yes \
        2>&1 | tail -5

    # 编译
    log "Compiling ($JOBS jobs) ..."
    make -j"$JOBS" \
        CROSS_COMPILE_TARGET=yes \
        HOSTPYTHON=$(which python3) \
        2>&1 | tail -3

    # 安装到本地目录
    make install \
        CROSS_COMPILE_TARGET=yes \
        DESTDIR="" \
        2>&1 | tail -3

    ok "CPython built for $abi"
}

# ── 步骤 3: 编译 libpybridge.so ────────────────────────────────────

build_pybridge_for_abi() {
    local abi="$1"
    local triple="${ABI_TRIPLE[$abi]}"
    local install="$BUILD_DIR/install-$abi"
    local output="$OUTPUT_DIR/$abi"

    log "Building libpybridge.so for $abi ..."

    mkdir -p "$output"

    local cc="$TOOLCHAIN/bin/${triple}${ANDROID_API}-clang"
    if [[ ! -f "$cc" ]]; then
        cc="$TOOLCHAIN/bin/${ABI_ARCH[$abi]}-linux-android${ANDROID_API}-clang"
    fi

    # 编译 libpybridge.so
    # 注意: 这里需要修改 pybridge_core.c 使其在 Android 上直接链接 libpython
    # 而不是用 dlopen（因为 Android 的 dlopen 路径不同）

    $cc -shared -fPIC -O2 \
        -I"$install/include/python${PYTHON_MAJOR_MINOR}" \
        -I"$(pwd)/jni/include" \
        -L"$install/lib" \
        -o "$output/libpybridge.so" \
        jni/src/pybridge_core.c \
        -lpython${PYTHON_MAJOR_MINOR} \
        -ldl -llog \
        -Wl,-rpath,'$ORIGIN' \
        2>&1

    ok "libpybridge.so built for $abi"
}

# ── 步骤 4: 打包产物 ───────────────────────────────────────────────

package_for_abi() {
    local abi="$1"
    local install="$BUILD_DIR/install-$abi"
    local output="$OUTPUT_DIR/$abi"

    log "Packaging for $abi ..."

    mkdir -p "$output/lib" "$output/assets"

    # 复制 libpython.so
    local libpy=$(find "$install/lib" -name "libpython${PYTHON_MAJOR_MINOR}*.so*" | head -1)
    if [[ -n "$libpy" ]]; then
        cp "$libpy" "$output/lib/"
        ok "libpython: $(basename $libpy)"
    else
        err "libpython not found!"
    fi

    # 打包精简版标准库
    local stdlib="$install/lib/python${PYTHON_MAJOR_MINOR}"
    if [[ -d "$stdlib" ]]; then
        cd "$stdlib"
        zip -qr "$output/assets/python${PYTHON_MAJOR_MINOR/./}_stdlib.zip" \
            -x "test/*" -x "tests/*" -x "idle*/**" \
            -x "tkinter/**" -x "turtle*" -x "lib2to3/**" \
            -x "ensurepip/**" -x "distutils/**" \
            -x "__pycache__/**" -x "*.pyc" -x "*.pyo" \
            -x "plat-*/**" \
            . 2>/dev/null
        ok "stdlib.zip: $(du -sh "$output/assets/"*.zip | cut -f1)"
    fi

    # 生成配置
    cat > "$output/abi.json" << EOF
{
    "abi": "$abi",
    "triple": "${ABI_TRIPLE[$abi]}",
    "python_version": "$PYTHON_VERSION",
    "api_level": $ANDROID_API,
    "files": {
        "libpython": "lib/$(basename ${libpy:-libpython.so})",
        "libpybridge": "lib/libpybridge.so",
        "stdlib": "assets/python${PYTHON_MAJOR_MINOR/./}_stdlib.zip"
    }
}
EOF

    # 列出产物
    echo ""
    echo "  📦 $abi 产物:"
    ls -lh "$output/lib/"*.so 2>/dev/null | awk '{print "     " $NF " (" $5 ")"}'
    ls -lh "$output/assets/"*.zip 2>/dev/null | awk '{print "     " $NF " (" $5 ")"}'
    echo ""
}

# ── 主流程 ──────────────────────────────────────────────────────────

main() {
    echo "╔══════════════════════════════════════════════════╗"
    echo "║  PyBridge NDK 交叉编译                          ║"
    echo "╠══════════════════════════════════════════════════╣"
    echo "║  Python: $PYTHON_VERSION"
    echo "║  ABI:    $TARGET_ABI"
    echo "║  API:    $ANDROID_API"
    echo "╚══════════════════════════════════════════════════╝"
    echo ""

    check_env
    download_cpython

    if [[ "$BUILD_ALL" == true ]]; then
        ABIS="arm64-v8a armeabi-v7a x86_64 x86"
    else
        ABIS="$TARGET_ABI"
    fi

    for abi in $ABIS; do
        echo ""
        echo "════════════════════════════════════════"
        echo "  Building for: $abi"
        echo "════════════════════════════════════════"

        build_cpython_for_abi "$abi"
        build_pybridge_for_abi "$abi"
        package_for_abi "$abi"
    done

    echo ""
    echo "╔══════════════════════════════════════════════════╗"
    echo "║  ✅ 编译完成                                    ║"
    echo "║                                                  ║"
    echo "║  输出目录: $OUTPUT_DIR/"
    for abi in $ABIS; do
        echo "║    $abi/"
        echo "║      lib/libpython${PYTHON_MAJOR_MINOR}.so"
        echo "║      lib/libpybridge.so"
        echo "║      assets/python${PYTHON_MAJOR_MINOR/./}_stdlib.zip"
    done
    echo "╚══════════════════════════════════════════════════╝"
}

main
