#!/usr/bin/env bash
#
# verify_engine_so.sh — 对构建出的 libengine_api.so 做结构与符号断言
#
# 这些断言把「只会在真机上以黑屏/SatisfiedLinkError 暴露」的问题提前到构建期：
#   1. 全部 engine_* C ABI 符号齐全（Kotlin 壳与调试工具依赖）
#   2. NativeEngine 的三个 JNI 符号存在（包名/类名重绑后最容易静默出错）
#   3. 动态依赖里是平台原生 libEGL/libGLESv2，而不是被静态链进来的 ANGLE
#   4. 二进制里不含 ANGLE 的平台扩展常量（去 ANGLE 是否彻底）
#   5. Live2D 插件是否真被编进去（EXPECT_LIVE2D=1 时缺失即失败）
#
# Usage: verify_engine_so.sh <path/to/libengine_api.so> [ndk_root] [jni_libs_dir]
#
#   jni_libs_dir  可选。给了就额外检查"非平台的运行时依赖是否都已和引擎放在
#                 一起"（libomp.so 这类）。不传则跳过该项。
#
# 环境变量：
#   EXPECT_LIVE2D=1  断言二进制里必须含 Live2D 插件。CI 在成功还原 Cubism SDK 后
#                    设置它，用来堵住"以为有 Live2D、其实 CMake 静默跳过"的降级。
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
    engine_cancel_termination
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

# ── 4. 非平台的运行时依赖必须随 jniLibs 一起打包 ─────────────────────────────
# libengine_api.so 的 DT_NEEDED 里，平台不提供的那些（libomp.so、用了
# c++_shared 时的 libc++_shared.so）必须被拷进 jniLibs，否则真机 dlopen 直接失败：
#   dlopen failed: library "libomp.so" not found: needed by libengine_api.so
# 这个错误只在装机后暴露，且表现成"游戏启动即崩溃"，APK 本身照样打得出来——
# 所以必须在构建期拦住。检查的是「有没有和引擎放在同一目录」，那正是 APK 里
# lib/<abi>/ 的形态。
JNI_LIBS_DIR="${3:-}"
if [[ -n "$JNI_LIBS_DIR" ]]; then
    PLATFORM_LIBS="libc.so|libm.so|libdl.so|liblog.so|libandroid.so|libEGL.so"
    PLATFORM_LIBS+="|libGLESv1_CM.so|libGLESv2.so|libOpenSLES.so|libz.so|libatomic.so"
    PLATFORM_LIBS+="|libjnigraphics.so|libmediandk.so|libnativewindow.so|libsync.so"
    PLATFORM_LIBS+="|libvulkan.so|libOpenMAXAL.so|libcamera2ndk.so"
    unbundled=()
    for lib in $NEEDED; do
        if grep -qE "^(${PLATFORM_LIBS})$" <<<"$lib"; then
            continue
        fi
        if [[ ! -f "$JNI_LIBS_DIR/$lib" ]]; then
            unbundled+=("$lib")
        fi
    done
    if (( ${#unbundled[@]} > 0 )); then
        echo "✗ 以下依赖既非平台提供、也没有和引擎放在一起（真机会 dlopen 失败）："
        printf '    %s\n' "${unbundled[@]}"
        echo "  jniLibs 目录：$JNI_LIBS_DIR"
        echo "  提示：scripts/build_engine_android.sh 的 copy_ndk_runtime_deps 负责把"
        echo "        它们从 NDK 拷进来；APK job 也必须上传/下载整个 jniLibs 目录，"
        echo "        只传 libengine_api.so 会让这些库停在构建机上。"
        fail=1
    else
        echo "✓ 非平台运行时依赖都已随引擎放在一起"
    fi
else
    echo "· 跳过运行时依赖打包检查（未提供 jniLibs 目录）"
fi

# ── 5. 二进制里不应残留 ANGLE 平台常量 ───────────────────────────────────────
if strings -a "$SO" | grep -q "EGL_PLATFORM_ANGLE_TYPE_ANGLE"; then
    echo "✗ 二进制中仍含 ANGLE 平台常量（EGL_PLATFORM_ANGLE_TYPE_ANGLE）"
    fail=1
else
    echo "✓ 二进制中无 ANGLE 平台常量"
fi

# ── 6. Live2D 是否真被编进去 ─────────────────────────────────────────────────
# "krkrlive2d.dll" 这个字面量只在 cpp/plugins/krkrlive2d.cpp 的 NCB_MODULE_NAME 里
# 出现一次，用它当"Live2D 插件参与了编译"的判据足够唯一。
#
# ⚠️ 判据的坑（2026-09-16 被它白烧一轮 CI）：
#   * 不能用 `strings -a`——`NCB_MODULE_NAME` 是 `TJS_W("krkrlive2d.dll")`，而
#     `TJS_W(X)` = `u##X`、`tjs_char` = `char16_t`（cpp/core/tjs2/tjsTypes.h:44-45，
#     **不是** wchar_t），所以它在二进制里是 **UTF-16LE**、字符间夹 NUL。
#     `strings` 默认按 ASCII 连续可打印字节切分，**永远匹配不到**。
#   * 也不能 `tr -d '\0' < "$SO" | grep -q`——SO 有 100+ MB，`grep -q` 一命中就退出，
#     `tr` 随即吃到 SIGPIPE 报 "write error: Broken pipe"，而本脚本是
#     `set -euo pipefail`，于是判据整体变成非确定性失败（实测同一文件可能 0 也可能 1）。
# 所以下面走**无管道**的路子：把 UTF-16LE 的模式写进临时文件，用 `grep -aF -f` 直接比对。
# 模式由字符串本身生成，改了 NCB_MODULE_NAME 也不会失配。
LIVE2D_MODULE="krkrlive2d.dll"
L2D_PAT="$(mktemp)"
# printf 的 \0 在 printf 内建里不会吃掉后续数字（"%b" 才会），故可安全逐段拼接。
printf 'k\0r\0k\0r\0l\0i\0v\0e\0' > "$L2D_PAT"
printf '2\0d\0.\0d\0l\0l\0' >> "$L2D_PAT"
PAT_OK=1
if [[ "$(wc -c < "$L2D_PAT")" -ne $(( ${#LIVE2D_MODULE} * 2 )) ]]; then
    # 空模式文件会让 grep 匹配一切，宁可直接判失败
    echo "✗ 内部错误：Live2D 判据的模式文件生成异常（$(wc -c < "$L2D_PAT") 字节，应为 $(( ${#LIVE2D_MODULE} * 2 ))）" >&2
    PAT_OK=0
    fail=1
fi
HAVE_L2D=0
if (( PAT_OK )) && LC_ALL=C grep -aqF -f "$L2D_PAT" "$SO"; then
    HAVE_L2D=1
fi
rm -f "$L2D_PAT"

if (( HAVE_L2D )); then
    echo "✓ 二进制含 Live2D 插件（krkrlive2d）"
elif (( PAT_OK )) && [[ "${EXPECT_LIVE2D:-0}" == "1" ]]; then
    echo "✗ 本轮还原了 Cubism SDK，但 libengine_api.so 里没有 Live2D 插件"
    echo "  多半是 CMake 没找到 SDK：核对 cpp/plugins/cubism/Framework/CubismFramework.hpp"
    echo "  与 cpp/plugins/cubism/Core/lib/android/arm64-v8a/libLive2DCubismCore.a 是否在位。"
    fail=1
elif (( PAT_OK )); then
    echo "· 二进制不含 Live2D 插件（本机无 Cubism SDK，属预期降级）"
fi

# ── 6b. 内嵌着色器是否真被编进去 ───────────────────────────────────────────
# Live2D 一创建立绘模型就走 CubismShader_OpenGLES2::GenerateShaders()，而它靠
# Option::LoadFileFunction 向宿主索取样板着色器；本插件用内嵌表提供。
# 少了这张表 = 真机加载 .l2d 时 SIGSEGV（2026-09-16 真机实测栈顶就是
# LoadShaderProgramFromFile ← GenerateShaders）。这里按**同名标记**断言表已在二进制里。
SHADER_MARK="VertShaderSrc.vert"
if (( ! HAVE_L2D )); then
    : # 没有 Live2D 就不谈着色器
elif LC_ALL=C grep -aqF "$SHADER_MARK" "$SO"; then
    echo "✓ 二进制含内嵌 GLES2 着色器（$SHADER_MARK）"
else
    echo "✗ libengine_api.so 里没有内嵌着色器（$SHADER_MARK）"
    echo "  引擎会在真机加载 .l2d 时崩在 CubismShader_OpenGLES2::GenerateShaders()。"
    echo "  核对 CMake 是否执行了 scripts/gen_embedded_shaders.py 并编入 embedded_shaders.cpp，"
    echo "  以及 cpp/plugins/cubism/Framework/Rendering/OpenGL/Shaders/StandardES/ 是否在位。"
    fail=1
fi

echo
if (( fail )); then
    echo "校验失败。"
    exit 1
fi
echo "libengine_api.so 校验通过。"
