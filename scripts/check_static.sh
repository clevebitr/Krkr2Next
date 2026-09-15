#!/usr/bin/env bash
#
# check_static.sh — 全部本地静态检查的统一入口
#
# 这些检查都不需要 NDK / vcpkg / cmake，因此在 Termux 和 CI 上都能跑。
# 把它们集中成一个入口，是为了新增检查时只改一处，而不是去每个 workflow 里
# 同步罗列——漏同步的后果是某条检查在 CI 上悄悄不跑。
#
# CI 用法（android_build.yml 与 engine_verify.yml 都调用它）：
#   bash scripts/check_static.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

fail=0
skipped=0
skipped_names=()

# 退出码约定：0=通过  1=失败  2=跳过（环境不具备）
# 把"跳过"单独统计并显式报告——否则在缺 GL 库/编译器的环境里会打印"全部通过"，
# 而实际上有检查根本没跑。假信心比不检查更糟。
run() {
    local name="$1"; shift
    echo "──────────────────────────────────────────"
    echo "  $name"
    echo "──────────────────────────────────────────"
    local rc=0
    "$@" || rc=$?
    case "$rc" in
        0) echo ;;
        2) echo "  ↑ 跳过：$name"
           skipped=$((skipped + 1)); skipped_names+=("$name"); echo ;;
        *) echo "  ↑ 失败：$name"
           fail=1; echo ;;
    esac
    return 0
}

# 1. Kotlin external 方法 ↔ C++ JNI 符号一一对应。
#    JNI 符号名编码包名与类名，不一致只在运行时抛 UnsatisfiedLinkError。
run "JNI 符号一致性" python3 "$SCRIPT_DIR/check_jni_symbols.py"

# 2. 移植文件无未经承认的漂移（见 compat/upstream/aetherkiri_ports.json）。
run "移植溯源清单" python3 "$SCRIPT_DIR/check_port_drift.py"

# 3. 独立源文件的本地语法检查（Catch2 语法垫片 + clang -fsyntax-only）。
#    无编译器时脚本自行跳过（退出码 2）。
run "本地语法检查" bash "$SCRIPT_DIR/check_syntax.sh"

# 注意：check_gl_symbols.py **故意不在这里**。
#
# 它需要平台 GL 库才能跑，而"哪个平台的库"决定了结论：在 Android 构建 job 里
# runner 只有 Mesa 的 Linux 库，拿它去校验会给出误导性信号（验的是 Linux 目标，
# 不是 Android 目标）。因此它按平台单独调用：
#   - CI：engine_verify.yml（Linux 宿主目标）在装完依赖后显式调用
#   - 本机 Termux：直接跑，那时校验的是真正的 Android 库
# 放在这个无依赖的聚合入口里只会永远跳过、制造噪声。

echo "##########################################"
if (( fail )); then
    echo "# 静态检查有失败项。"
    if (( skipped )); then
        echo "# 另有 $((skipped)) 项被跳过：${skipped_names[*]}"
    fi
    exit 1
fi

if (( skipped )); then
    # 关键：不能在有跳过项时还打印"全部通过"。跳过往往意味着环境缺东西
    # （CI 里没装 GL 库、容器里没编译器），而这恰恰是最容易被忽略的情形——
    # 报"全部通过"会让人以为覆盖是全的。
    echo "# 静态检查通过，但有 $((skipped)) 项被跳过："
    for n in "${skipped_names[@]}"; do
        echo "#   - $n"
    done
    echo "# 跳过项不代表通过：请确认本环境是否本该具备相应依赖。"
    exit 0
fi

echo "# 全部静态检查通过（无跳过项）。"
