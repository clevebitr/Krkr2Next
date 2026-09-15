#!/usr/bin/env bash
#
# verify_engine_so.sh — 对构建出的 libengine_api.so 做结构与符号断言
#
# 这些断言把「只会在真机上以黑屏/SatisfiedLinkError 暴露」的问题提前到构建期：
#   1. 24 个 engine_* C ABI 符号齐全（Kotlin 壳与调试工具依赖）
#   2. NativeEngine 的三个 JNI 符号存在（包名/类名重绑后最容易静默出错）
#   3. 动态依赖里是平台原生 libEGL/libGLESv2，而不是被静态链进来的 ANGLE
#   4. 二进制里不含 ANGLE 的平台扩展常量（去 ANGLE 是否彻底）
#
# Usage: verify_engine_so.sh <path/to/libengine_api.so> [ndk_root]
#
set -euo pipefail

SO="${1:?usage: verify_engine_so.sh <libengine_api.so> [ndk_root]}"
NDK_ROOT="${2:-${ANDROID_NDK_HOME:-}}"

if [[ ! -f "$SO" ]]; then
    echo "错误：找不到 $SO" >&2
    exit 1
fi

# 找 llvm 工具（优先 NDK 自带，其次 PATH）
find_tool() {
    local name="$1"
    if [[ -n "$NDK_ROOT" ]]; then
        local p
        p="$(find "$NDK_ROOT/toolchains/llvm/prebuilt" -maxdepth 3 -name "$name" -type f 2>/dev/null | head -n1)"
        [[ -n "$p" ]] && { echo "$p"; return; }
    fi
    command -v "$name" || command -v "${name/llvm-/}" || true
}

NM="$(find_tool llvm-nm)"; NM="${NM:-$(find_tool nm)}"
READELF="$(find_tool llvm-readelf)"; READELF="${READELF:-$(find_tool readelf)}"

if [[ -z "$NM" || -z "$READELF" ]]; then
    echo "错误：找不到 llvm-nm / llvm-readelf（请传 ndk_root 或安装 binutils）" >&2
    exit 1
fi

fail=0

# ── 1. C ABI 符号 ────────────────────────────────────────────────────────────
ENGINE_SYMBOLS=(
    engine_create
    engine_destroy
    engine_drain_startup_logs
    engine_get_frame_desc
    engine_get_frame_rendered_flag
    engine_get_host_native_view
    engine_get_host_native_window
    engine_get_last_error
    engine_get_memory_stats
    engine_get_renderer_info
    engine_get_runtime_api_version
    engine_get_startup_state
    engine_open_game
    engine_open_game_async
    engine_pause
    engine_read_frame_rgba
    engine_resume
    engine_send_input
    engine_set_log_file_path
    engine_set_option
    engine_set_render_target_iosurface
    engine_set_render_target_surface
    engine_set_surface_size
    engine_tick
)

DEFINED="$("$NM" -D --defined-only "$SO" 2>/dev/null || "$NM" --dynamic --defined-only "$SO")"

missing_engine=()
for sym in "${ENGINE_SYMBOLS[@]}"; do
    grep -qE "[[:space:]]T[[:space:]]+${sym}\$" <<<"$DEFINED" || missing_engine+=("$sym")
done
if (( ${#missing_engine[@]} )); then
    echo "✗ 缺失 C ABI 符号：${missing_engine[*]}"
    fail=1
else
    echo "✓ C ABI：全部 ${#ENGINE_SYMBOLS[@]} 个 engine_* 符号存在"
fi

# ── 2. Kotlin 壳的 JNI 符号 ──────────────────────────────────────────────────
JNI_SYMBOLS=(
    Java_org_dpdns_clevebitr_core_NativeEngine_nativeSetSurface
    Java_org_dpdns_clevebitr_core_NativeEngine_nativeDetachSurface
    Java_org_dpdns_clevebitr_core_NativeEngine_nativeSetApplicationContext
)
missing_jni=()
for sym in "${JNI_SYMBOLS[@]}"; do
    grep -qE "[[:space:]]T[[:space:]]+${sym}\$" <<<"$DEFINED" || missing_jni+=("$sym")
done
if (( ${#missing_jni[@]} )); then
    echo "✗ 缺失 JNI 符号：${missing_jni[*]}"
    echo "  提示：JNI 符号名编码包名与类名（. → _，_ → _1）。"
    echo "        若改过 NativeEngine 的包/类名，需同步改 engine_api_android_jni.cpp。"
    fail=1
else
    echo "✓ JNI：NativeEngine 的三个入口符号存在"
fi

# ── 3. 动态依赖必须是平台原生 EGL/GLES ───────────────────────────────────────
NEEDED="$("$READELF" -d "$SO" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
for lib in libEGL.so libGLESv2.so; do
    if grep -qE "^${lib}\$" <<<"$NEEDED"; then
        echo "✓ 动态依赖包含 $lib（平台原生）"
    else
        echo "✗ 动态依赖缺少 $lib —— 实际依赖："
        sed 's/^/    /' <<<"$NEEDED"
        fail=1
    fi
done

# ── 4. 二进制里不应残留 ANGLE 平台常量 ───────────────────────────────────────
if strings -a "$SO" | grep -q "EGL_PLATFORM_ANGLE_TYPE_ANGLE"; then
    echo "✗ 二进制中仍含 ANGLE 平台常量（EGL_PLATFORM_ANGLE_TYPE_ANGLE）"
    fail=1
else
    echo "✓ 二进制中无 ANGLE 平台常量"
fi

echo
if (( fail )); then
    echo "校验失败。"
    exit 1
fi
echo "libengine_api.so 校验通过。"
