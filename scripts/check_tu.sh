#!/usr/bin/env bash
#
# check_tu.sh — 单文件 / 单模块的语法检查（不需要 CMake configure，也不需要 vcpkg install）
#
# 背景：完整构建要先跑 vcpkg 装 196 个包（几十分钟，还受 GitHub 网络影响），
# 中途任何一个包的下载失败都会让整个 CMake configure 中止 —— 拿不到任何关于
# "我改的代码编不编得过" 的信息。
#
# 这个脚本绕过 CMake 和 vcpkg install，直接用 NDK clang 对指定文件跑
# `-fsyntax-only`：只看语法与声明/定义是否自洽，不链接、不产生目标文件。
# 一个文件通常 0.1–2 秒，可以把"重构是否自洽"从几十分钟压到几秒。
#
# 它**不能**替代完整构建，但能拦住这类问题：
#   - using 声明与全局声明冲突（declaration vs using-declaration）
#   - 声明在 namespace 里、定义在 namespace 外
#   - 少了 include（显式 include 缺失、只靠传递依赖才能编过）
#   - 宏展开出的未限定名 / 显式特化写在错误作用域（NCB_* 宏族）
#   - 枚举值没导入、块作用域 extern 绑错命名空间
#
# Usage:
#   scripts/check_tu.sh core/base/CharacterSet.cpp
#   scripts/check_tu.sh core/base/*.cpp            # 一个模块
#   scripts/check_tu.sh --module base              # 同上，按模块名
#   scripts/check_tu.sh --list-modules
#   JOBS=8 scripts/check_tu.sh --module visual     # 并行（当前强制串行，参数保留）
#
# 环境变量：
#   ANDROID_NDK_HOME   必填，指向 Linux 版 NDK（r27 系列）
#   VCPKG_INSTALLED    可选，指向 vcpkg_installed/<triplet>/include
#                      未设置时按 out/android/debug/vcpkg_installed/arm64-android/include 推断
#   JOBS               并行度，默认 1
#
# 退出码：0 = 全部通过 / 1 = 有失败 / 2 = 环境不具备
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CPP="$ROOT/cpp"

if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    echo "SKIP  请先 export ANDROID_NDK_HOME=<linux 版 NDK>" >&2
    exit 2
fi

HOST_TAG=linux-x86_64
TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"
CXX="$TOOLCHAIN/bin/aarch64-linux-android24-clang++"
if [[ ! -x "$CXX" ]]; then
    echo "SKIP  找不到 $CXX" >&2
    exit 2
fi

VCPKG_INC="${VCPKG_INSTALLED:-$ROOT/out/android/debug/vcpkg_installed/arm64-android/include}"
if [[ ! -d "$VCPKG_INC" ]]; then
    echo "WARN  没有 $VCPKG_INC（第三方头缺失会导致大量误报）" >&2
fi

# 项目内 include 路径：与各模块 CMakeLists 里 PUBLIC include 的并集保持一致
INC=(
    -I"$CPP"
    -I"$CPP/common"
    -I"$CPP/tjs2"
    -I"$CPP/utils"            -I"$CPP/utils/win32"    -I"$CPP/utils/iconv"
    -I"$CPP/base/core"        -I"$CPP/base/runtime"
    -I"$CPP/base/script"
    -I"$CPP/io"
    -I"$CPP/plugin"
    -I"$CPP/environ"          -I"$CPP/environ/sysinit"
    -I"$CPP/environ/win32"    -I"$CPP/environ/android"
    -I"$CPP/visual"           -I"$CPP/visual/impl"
    -I"$CPP/sound"            -I"$CPP/sound/win32"
    -I"$CPP/movie"            -I"$CPP/movie/ffmpeg"
    -I"$CPP/compat"
    -I"$CPP/extension"
)
if [[ -d "$VCPKG_INC" ]]; then
    INC+=(
        -I"$VCPKG_INC"
        -I"$VCPKG_INC/glib-2.0"
        -I"$VCPKG_INC/freetype2"
    )
    # glib/gtk 的子目录布局不统一，全部加上
    while IFS= read -r d; do INC+=(-I"$d"); done < <(find "$VCPKG_INC" -maxdepth 3 -type d -name include 2>/dev/null)
fi

# 与 krkr2core/CMakeLists.txt 的 target_compile_definitions 保持一致
DEFS=(
    -DTJS_TEXT_OUT_CRLF
    -D__STDC_CONSTANT_MACROS
    -DUSE_UNICODE_FSTRING
)

CXXFLAGS=(
    --target=aarch64-linux-android24
    --sysroot="$TOOLCHAIN/sysroot"
    -std=c++17
    -fsyntax-only
    -fno-caret-diagnostics
    -Wno-everything
    "${DEFS[@]}"
    "${INC[@]}"
)

MODULES=(tjs2 utils base io compat environ extension plugin movie sound visual)
if [[ "${1:-}" == "--list-modules" ]]; then
    printf '%s\n' "${MODULES[@]}"
    exit 0
fi

# 收集目标文件
FILES=()
if [[ "${1:-}" == "--module" ]]; then
    m="${2:?--module 需要模块名}"
    if [[ ! -d "$CPP/$m" ]]; then
        echo "没有模块 $m" >&2
        exit 1
    fi
    while IFS= read -r f; do FILES+=("$f"); done < <(find "$CPP/$m" -name '*.cpp' | sort)
else
    for a in "$@"; do
        case "$a" in
            /*) FILES+=("$a") ;;
            *)  FILES+=("$CPP/$a") ;;
        esac
    done
fi

if (( ${#FILES[@]} == 0 )); then
    echo "没有要检查的文件" >&2
    exit 1
fi

check_one() {
    local f="$1" out rc
    out=$("$CXX" "${CXXFLAGS[@]}" "$f" 2>&1)
    rc=$?
    if (( rc == 0 )); then
        printf 'PASS  %s\n' "${f#"$ROOT/"}"
        return 0
    fi
    printf 'FAIL  %s\n' "${f#"$ROOT/"}"
    # 只打印真正有诊断意义的前若干行
    printf '%s\n' "$out" | grep -E 'error:' | head -8 | sed 's/^/      /'
    return 1
}

JOBS="${JOBS:-1}"   # 目前串行（1–2 秒/文件已够快）；并行留待需要时再加
pass=0 fail=0
failed_files=()

for f in "${FILES[@]}"; do
    if check_one "$f"; then pass=$((pass+1)); else fail=$((fail+1)); failed_files+=("$f"); fi
done

echo
echo "──────────────────────────────────────────"
echo "  通过 $pass / $((pass+fail))"
if (( fail > 0 )); then
    echo "  失败："
    for f in "${failed_files[@]}"; do echo "    ${f#"$ROOT/"}"; done
    exit 1
fi
