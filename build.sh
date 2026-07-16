#!/usr/bin/env bash
# yay build script
# Requires: Android NDK r25c+ installed, NDK_PATH set or auto-detected below
# Usage: ./build.sh [release|debug] [NDK_PATH]
set -euo pipefail

# ─── Config ──────────────────────────────────────────────────────────────────
BUILD_TYPE="${1:-release}"
ANDROID_API=31          # Android 12 minimum (cmd game downscale API 31+)
ABI=arm64-v8a

# Auto-detect NDK if not provided
if [ -n "${2:-}" ]; then
    NDK="$2"
elif [ -n "${ANDROID_NDK_HOME:-}" ]; then
    NDK="$ANDROID_NDK_HOME"
elif [ -n "${NDK_PATH:-}" ]; then
    NDK="$NDK_PATH"
elif [ -d "$HOME/Android/Sdk/ndk" ]; then
    NDK=$(ls -d "$HOME/Android/Sdk/ndk"/*/ 2>/dev/null | sort -V | tail -1)
    NDK="${NDK%/}"
else
    echo "ERROR: NDK not found. Set ANDROID_NDK_HOME or pass path as second arg."
    exit 1
fi

echo "NDK : $NDK"
echo "API : $ANDROID_API"
echo "ABI : $ABI"
echo "TYPE: $BUILD_TYPE"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_${ABI}_${BUILD_TYPE}"
OUT_DIR="$SCRIPT_DIR/bin"

# nlohmann/json is fetched automatically by CMake (FetchContent, pinned to
# a tagged release — see CMakeLists.txt) during the configure step below.
# No manual download needed here anymore. yay_inspect additionally links
# zlib (libz.so), which ships as part of every Android system image and
# is provided by the NDK sysroot — no extra fetch/vendoring needed for it.

# ─── CMake configure ─────────────────────────────────────────────────────────
CMAKE_BUILD_TYPE="Release"
[ "$BUILD_TYPE" = "debug" ] && CMAKE_BUILD_TYPE="Debug"

cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$ANDROID_API" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SCRIPT_DIR" \
    "$SCRIPT_DIR"

# ─── Build ───────────────────────────────────────────────────────────────────
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" --parallel "$JOBS"

# ─── Install binaries into module bin/ ───────────────────────────────────────
mkdir -p "$OUT_DIR"
cmake --install "$BUILD_DIR"
cp "$BUILD_DIR/yay_apply" "$OUT_DIR/yay_apply"
cp "$BUILD_DIR/yay_watch" "$OUT_DIR/yay_watch"
cp "$BUILD_DIR/yay_inspect" "$OUT_DIR/yay_inspect"

echo ""
echo "Build complete:"
ls -lh "$OUT_DIR"/yay_apply "$OUT_DIR"/yay_watch "$OUT_DIR"/yay_inspect
file "$OUT_DIR"/yay_apply
