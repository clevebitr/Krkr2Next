#!/usr/bin/env bash
#
# build_engine_android.sh — 构建 KrKr2-Next-Compose 的 C++ 引擎共享库并投放到 Kotlin 壳的 jniLibs
#
# Usage:
#   ./scripts/build_engine_android.sh [debug|release] [--configure-only]
#
#   --configure-only  只跑 CMake configure 就退出。configure 会触发 vcpkg 安装
#                     全部 manifest 依赖（本项目 124 个库），是最慢的一步；
#                     拆出来便于调用方在依赖就位后、编译之前先落缓存。
#
# Output:
#   out/android/<type>/bridge/engine_api/libengine_api.so   （构建产物）
#   app/app/src/main/jniLibs/arm64-v8a/libengine_api.so     （投放位置，已被 .gitignore）
#
# 环境变量:
#   ANDROID_NDK_HOME    Android NDK 路径（或 ANDROID_HOME/ndk/<ver>，取最新）
#   VCPKG_ROOT          已有 vcpkg；未设置则自举到 .devtools/vcpkg
#   JOBS                并行任务数（默认 8）
#   ENABLE_RENDER_PROBE 渲染诊断探针开关，默认 OFF；变更会强制重新 configure
#   KRKR_LOG_LEVEL      日志级别；为空/auto 表示按构建类型自动
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="debug"
CONFIGURE_ONLY=false

while (( $# > 0 )); do
    case "$1" in
        --configure-only)
            # 只跑 CMake configure 就退出。configure 会触发 vcpkg 安装全部
            # manifest 依赖（本项目 124 个库），是整条链路里最慢的一步。
            # 拆出来单独跑，是为了让 CI 能在依赖编完后立刻落缓存，
            # 而不必等引擎编译结束（引擎若失败或 runner 超时，缓存就白编了）。
            CONFIGURE_ONLY=true
            ;;
        debug|release|Debug|Release)
            BUILD_TYPE="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
            ;;
        *)
            echo "错误：未知参数 '$1'（可用：debug|release --configure-only）" >&2
            exit 1
            ;;
    esac
    shift
done

BUILD_TYPE_LOWER="$BUILD_TYPE"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "错误：无效构建类型 '$BUILD_TYPE'，请用 'debug' 或 'release'。"
    exit 1
fi

BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"
CMAKE_CONFIG_PRESET="Android ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="Android ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/android/$BUILD_TYPE_LOWER"

# 当前只构建 arm64-v8a（与 PocketKrKr 一致；vcpkg/triplets/arm64-android.cmake 也只支持它）
ANDROID_ABI="arm64-v8a"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; CYAN='\033[0;36m'; NC='\033[0m'
log_step()  { echo ""; echo -e "${CYAN}========================================${NC}"; echo -e "${CYAN}  $1${NC}"; echo -e "${CYAN}========================================${NC}"; }
log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_command() {
    if ! command -v "$1" &>/dev/null; then
        log_error "'$1' 未安装或不在 PATH 中。"
        exit 1
    fi
}

# ============================================================
# 定位 Android NDK
# ============================================================
if [[ -n "${ANDROID_NDK_HOME:-}" && -d "$ANDROID_NDK_HOME" ]]; then
    NDK_ROOT="$ANDROID_NDK_HOME"
elif [[ -n "${ANDROID_NDK_ROOT:-}" && -d "$ANDROID_NDK_ROOT" ]]; then
    NDK_ROOT="$ANDROID_NDK_ROOT"
elif [[ -n "${ANDROID_HOME:-}" && -d "$ANDROID_HOME/ndk" ]]; then
    NDK_ROOT="$(ls -1d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -z "$NDK_ROOT" ]]; then
        log_error "\$ANDROID_HOME/ndk 下没有 NDK。请用 SDK Manager 安装。"
        exit 1
    fi
else
    log_error "未找到 Android NDK。"
    echo "  请设置 ANDROID_NDK_HOME（或 ANDROID_HOME 并提供 ndk/ 子目录）。"
    echo "  推荐版本：27.0.12077973（NDK 29 会破坏 libffi:arm64-android）"
    echo "  例：export ANDROID_NDK_HOME=\"\$ANDROID_HOME/ndk/27.0.12077973\""
    exit 1
fi
export ANDROID_NDK_HOME="$NDK_ROOT"

# ============================================================
# 定位 / 自举 vcpkg
# ============================================================
# vcpkg 钉在固定 commit，而不是跟随滚动 tip。理由：ABI 版本一旦漂移，vcpkg 二进制
# 缓存全部失效，所有依赖重编（~40 分钟）。钉住后 ABI 稳定，二进制缓存才真正复用。
VCPKG_PINNED_COMMIT="52d80838fb40c755b1615fbc9c7b994a33742a22"  # vcpkg master @2026-09-11

if [[ -d "$PROJECT_ROOT/.devtools/vcpkg/.git" ]]; then
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
    # 已存在也强制钉回目标 commit，防止漂移
    (cd "$VCPKG_ROOT" && git checkout --detach "$VCPKG_PINNED_COMMIT" 2>/dev/null) || true
elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
    : # 沿用环境里的 VCPKG_ROOT
else
    log_info "未找到 vcpkg，正在自举钉版 vcpkg 到 .devtools/vcpkg ..."
    mkdir -p "$PROJECT_ROOT/.devtools"
    git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
    (cd "$PROJECT_ROOT/.devtools/vcpkg" && git checkout --detach "$VCPKG_PINNED_COMMIT" && ./bootstrap-vcpkg.sh -disableMetrics)
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
fi
export VCPKG_ROOT

PARALLEL_JOBS="${JOBS:-8}"

# ============================================================
# 前置检查
# ============================================================
log_step "前置检查"
check_command cmake
check_command ninja

if [[ ! -d "$VCPKG_ROOT" ]]; then
    log_error "vcpkg 不存在：$VCPKG_ROOT"
    exit 1
fi

log_info "构建类型:      $BUILD_TYPE_CAP"
log_info "工程根:        $PROJECT_ROOT"
log_info "CMake preset:  $CMAKE_BUILD_PRESET"
log_info "Android NDK:   $NDK_ROOT"
log_info "并行任务数:    $PARALLEL_JOBS"

# ============================================================
# 构建 C++ 引擎
# ============================================================
log_step "构建 C++ 引擎 (libengine_api.so)"

# 探针 / 日志级别由环境传入，透传为 CMake 缓存变量；变更需重新 configure 才生效
RENDER_PROBE_OPT=""
if [[ -n "${ENABLE_RENDER_PROBE+x}" ]]; then
    RENDER_PROBE_OPT="-DENABLE_RENDER_PROBE=OFF"
    case "$ENABLE_RENDER_PROBE" in
        true|on|1) RENDER_PROBE_OPT="-DENABLE_RENDER_PROBE=ON" ;;
    esac
fi
LOG_LEVEL_OPT=""
if [[ -n "${KRKR_LOG_LEVEL:-}" && "${KRKR_LOG_LEVEL}" != "auto" ]]; then
    LOG_LEVEL_OPT="-DKRKR_LOG_LEVEL=$KRKR_LOG_LEVEL"
fi

# 工具链换过一次：旧的 Android preset 用 CMAKE_SYSTEM_NAME=Android + 裸
# clang/clang++，CMake 会把它解析成宿主的 /usr/bin/clang++（没有 NDK sysroot、
# 没有 __ANDROID__），构建目录里因此留下这两项缓存。现在改成 NDK
# android.toolchain.cmake 经 vcpkg chainload，而这套缓存仍在、且 CMake 不会因为
# 一个新增的 chainload 变量就报错，于是继续沿用旧编译器。检测到旧指纹就清掉
# 重建——这个目录里只有构建产物。
if [[ -f "$CMAKE_BUILD_DIR/CMakeCache.txt" ]] && \
   grep -qE '^(CMAKE_SYSTEM_NAME|CMAKE_ANDROID_NDK):' "$CMAKE_BUILD_DIR/CMakeCache.txt"; then
    log_warn "构建目录由旧工具链配置生成（宿主 clang / 无 NDK sysroot），清理后重新 configure"
    rm -rf "$CMAKE_BUILD_DIR"
fi

NEED_CFG=0
if [[ ! -f "$CMAKE_BUILD_DIR/build.ninja" ]]; then
    NEED_CFG=1
elif [[ -n "${RENDER_PROBE_OPT}${LOG_LEVEL_OPT}" ]]; then
    NEED_CFG=1
fi

if [[ "$NEED_CFG" == 1 ]]; then
    log_info "CMake configure... (probe='${ENABLE_RENDER_PROBE:-<default>}', log_level='${KRKR_LOG_LEVEL:-<auto>}')"
    log_info "  ↑ 这一步会触发 vcpkg 安装全部 manifest 依赖，首次执行很慢（分钟级）"
    cmake --preset "$CMAKE_CONFIG_PRESET" ${RENDER_PROBE_OPT} ${LOG_LEVEL_OPT}
else
    log_info "构建目录已配置，跳过 configure。"
fi

# --configure-only：依赖装完就退出，让调用方（CI）先落缓存再继续编译。
# 依赖是整条链路里最慢的一步，尽早固化到缓存，避免引擎编译失败/超时时白编。
if [[ "$CONFIGURE_ONLY" == true ]]; then
    log_step "仅配置完成（vcpkg 依赖已就位）"
    log_info "构建目录：$CMAKE_BUILD_DIR"
    log_info "下一步编译：$CMAKE_BUILD_PRESET"
    exit 0
fi

log_info "编译中（$PARALLEL_JOBS 并行）..."
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

ENGINE_LIB="$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.so"
if [[ ! -f "$ENGINE_LIB" ]]; then
    log_error "未找到引擎共享库：$ENGINE_LIB"
    exit 1
fi
log_info "引擎共享库已构建：$ENGINE_LIB"

# ============================================================
# 投放到 Kotlin 壳的 jniLibs
# ============================================================
log_step "投放 libengine_api.so 到 jniLibs"

JNI_LIBS_DIR="$PROJECT_ROOT/app/app/src/main/jniLibs/$ANDROID_ABI"
mkdir -p "$JNI_LIBS_DIR"
cp -f "$ENGINE_LIB" "$JNI_LIBS_DIR/libengine_api.so"
log_info "已投放 -> $JNI_LIBS_DIR/libengine_api.so"

# 解析 DT_NEEDED，把 NDK 运行时库（典型是 libomp.so / libc++_shared.so）一并拷进
# jniLibs。漏拷的话 Android 启动时 dlopen 会报
#   "library ... not found needed by libengine_api.so"
# 引擎加载失败 → 界面一直转圈（真机日志里表现为 nativeloader 报错）。
copy_ndk_runtime_deps() {
    local so="$1"
    local abi_dir="$2"

    local readelf_tool=""
    for t in llvm-readelf readelf; do
        if command -v "$t" &>/dev/null; then readelf_tool="$t"; break; fi
    done
    if [[ -z "$readelf_tool" ]]; then
        log_warn "未找到 readelf/llvm-readelf，跳过 NDK 运行时依赖拷贝。"
        return 0
    fi

    local copied=0
    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue
        case "$dep" in
            libomp.so|libc++_shared.so|libgomp.so|libatomic.so)
                [[ -f "$abi_dir/$dep" ]] && continue
                local src
                # NDK 的安装路径用的是三元组名（aarch64-linux-android）和 clang 自带的
                # 库目录（lib/linux/aarch64），**都不含 "arm64-v8a"**——按
                # `*/${ANDROID_ABI}/*` 去找会永远找不到，libc++_shared.so 就是这么被
                # 漏掉的。这里覆盖 arm64 的两种实际布局。
                src="$(find "$NDK_ROOT" -name "$dep" \
                         \( -path "*/aarch64-linux-android/*" -o -path "*/lib/linux/aarch64/*" \) \
                         2>/dev/null | head -n1 || true)"
                if [[ -n "$src" ]]; then
                    cp -f "$src" "$abi_dir/$dep"
                    log_info "已拷贝 NDK 运行时 -> $abi_dir/$dep (来自 $src)"
                    copied=1
                else
                    # 硬失败：这个库是 libengine_api.so 的 DT_NEEDED，缺了它真机
                    # dlopen 直接失败，表现成"游戏启动即崩溃"，而 APK 照样能打出来。
                    # 与其把问题发到用户手上，不如在构建期就停。
                    log_error "NDK 下未找到运行时库 '$dep'（libengine_api.so 依赖它，缺了真机无法加载）"
                    exit 1
                fi
                ;;
        esac
    done < <("$readelf_tool" -d "$so" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')

    [[ "$copied" == 0 ]] && log_info "无需拷贝额外的 NDK 运行时依赖。"
    return 0
}

copy_ndk_runtime_deps "$ENGINE_LIB" "$JNI_LIBS_DIR"

log_step "引擎构建完成"
log_info "产物：$JNI_LIBS_DIR/libengine_api.so"
echo ""
log_info "接下来构建 APK："
echo "  cd app && ./gradlew :app:assembleDebug"
