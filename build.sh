#!/usr/bin/env bash
#
# build.sh — KiriNext 构建入口（仅 Android）
#
# Usage:
#   ./build.sh [debug|release] [--engine-only] [--apk-only]
#
# 说明：
#   KiriNext 只面向 Android。引擎共享库经 scripts/build_engine_android.sh 构建
#   并投放到 app/app/src/main/jniLibs/arm64-v8a/，随后由 Gradle 打包进 APK。
#
#   Gradle 不使用 externalNativeBuild —— 引擎在 Gradle 之外独立构建。原因是根
#   CMakeLists.txt 会设置 vcpkg 的 CMAKE_TOOLCHAIN_FILE，与 Gradle 传入的 NDK
#   toolchain file 冲突，CMake 会因 toolchain 重定义而行为异常。
#
# 环境变量：见 scripts/build_engine_android.sh 头部注释
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_TYPE="debug"
ENGINE_ONLY=false
APK_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            sed -n '3,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --engine-only) ENGINE_ONLY=true ;;
        --apk-only)    APK_ONLY=true ;;
        debug|release|Debug|Release)
            BUILD_TYPE="$(echo "$arg" | tr '[:upper:]' '[:lower:]')" ;;
        *)
            echo "错误：未知参数 '$arg'（用 --help 查看用法）" >&2
            exit 1
            ;;
    esac
done

if [[ "$APK_ONLY" == false ]]; then
    bash "$SCRIPT_DIR/scripts/build_engine_android.sh" "$BUILD_TYPE"
    [[ "$ENGINE_ONLY" == true ]] && exit 0
fi

# ============================================================
# Gradle 打包 APK
# ============================================================
APP_DIR="$SCRIPT_DIR/app"

if [[ ! -d "$APP_DIR" ]]; then
    echo "错误：Kotlin 壳目录不存在：$APP_DIR" >&2
    exit 1
fi

JNI_SO="$APP_DIR/app/src/main/jniLibs/arm64-v8a/libengine_api.so"
if [[ ! -f "$JNI_SO" ]]; then
    echo "错误：未找到引擎共享库：$JNI_SO" >&2
    echo "  先运行：./build.sh $BUILD_TYPE --engine-only" >&2
    exit 1
fi

if [[ "$BUILD_TYPE" == "release" ]]; then
    GRADLE_TASK="assembleRelease"
else
    GRADLE_TASK="assembleDebug"
fi

echo ""
echo "========================================"
echo "  构建 APK：$GRADLE_TASK"
echo "========================================"
(cd "$APP_DIR" && ./gradlew ":$GRADLE_TASK")

echo ""
echo "APK 产物："
find "$APP_DIR/app/build/outputs/apk" -name '*.apk' 2>/dev/null || true
