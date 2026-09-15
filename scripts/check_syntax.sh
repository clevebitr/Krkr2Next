#!/usr/bin/env bash
#
# check_syntax.sh — 本地语法检查（不需要 cmake / ninja / NDK / vcpkg）
#
# 背景：开发环境是 Termux（Android/aarch64），完整引擎构建必须走 CI。但**只依赖
# 少量头文件的源文件可以在这里先编译一遍**——Termux 自带 clang。这一步能把
# "拼错符号名 / 缺 include / 断言写错 / 结构体偏移算错" 这类问题在推送前拦下来，
# 而不是等几分钟的 CI 跑完才发现。
#
# 做法：为 Catch2 生成一个最小语法垫片（只提供宏，不提供测试运行器），
# 然后对目标文件跑 `clang++ -fsyntax-only`。它不链接、不执行，只验证
# 「能编译过」这一件事。Catch2 的真实语义仍由 CI 的 ctest 验证。
#
# Usage:
#   scripts/check_syntax.sh              # 检查默认集合
#   scripts/check_syntax.sh <file.cpp>   # 检查指定文件（可多个）
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 退出码约定（check_static.sh 依赖）：0=通过  1=失败  2=跳过（环境不具备）
CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "SKIP 本地语法检查：找不到 C++ 编译器 '$CXX'（设 CXX= 指定）" >&2
    exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/kirinext-syntax.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------------------
# Catch2 语法垫片
# ---------------------------------------------------------------------------
mkdir -p "$WORK/shim/catch2"
cat > "$WORK/shim/catch2/catch_all.hpp" <<'SHIM'
// 最小 Catch2 语法垫片：仅供本地 -fsyntax-only 使用，不提供测试运行器。
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdio>
#define CATCH_SHIM_CAT2(a, b) a##b
#define CATCH_SHIM_CAT(a, b) CATCH_SHIM_CAT2(a, b)
#define TEST_CASE(name, tags) \
    static void CATCH_SHIM_CAT(catch_shim_tc_, __LINE__)()
#define SECTION(...) if (true)
#define REQUIRE(...) assert(__VA_ARGS__)
#define CHECK(...) assert(__VA_ARGS__)
#define STATIC_REQUIRE(...) static_assert(__VA_ARGS__)
SHIM

# 头文件搜索路径：按被测代码可能用到的位置补齐。
INCLUDES=(
    "-I$WORK/shim"
    "-I$ROOT/bridge/engine_api/include"
    "-I$ROOT/cpp/core"
    "-I$ROOT/cpp/plugins"
    "-I$ROOT/cpp/core/visual"
)

# ---------------------------------------------------------------------------
# 目标文件
#
# 只放"头文件依赖少、能在无 vcpkg 环境下编译过"的文件。依赖 spdlog / ffmpeg /
# vcpkg 三方库的源文件不要加进来——那类只能靠 CI。
# ---------------------------------------------------------------------------
if (( $# > 0 )); then
    FILES=("$@")
else
    FILES=(
        "$ROOT/tests/unit-tests/engine_api/abi-layout.cpp"
    )
fi

fail=0
checked=0
for f in "${FILES[@]}"; do
    if [[ ! -f "$f" ]]; then
        echo "跳过（不存在）：$f"
        continue
    fi
    checked=$((checked + 1))
    rel="${f#"$ROOT"/}"
    if "$CXX" -std=c++17 -Wall -Wextra -Wno-unused-function -fsyntax-only \
            "${INCLUDES[@]}" "$f" 2>"$WORK/err.txt"; then
        echo "✓ $rel"
    else
        echo "✗ $rel"
        sed 's/^/    /' "$WORK/err.txt"
        fail=1
    fi
done

echo
if (( fail )); then
    echo "语法检查失败（$checked 个文件）。"
    exit 1
fi
echo "语法检查通过（$checked 个文件）。注意：这只证明能编译，不证明行为正确。"
