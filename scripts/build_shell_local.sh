#!/usr/bin/env bash
#
# build_shell_local.sh — 在 Termux 本地编译/类型检查 Kotlin/Compose 壳
#
# 背景：CI 是唯一"完整"构建（引擎 + APK）。但**壳本身的 Kotlin 编译**其实可以在
# Termux 本地跑通，从而不用为了一处 import 拼错等一次 CI。两个前提：
#
#   1. Gradle 版本必须是 8.x：AGP 8.13 依赖 Gradle 9.6 里被移除的内部 API，用系统
#      自带的 gradle（9.7.x）会在配置期就失败。本机已缓存 CI 同款的 8.14.5
#      （~/.gradle/wrapper/dists/gradle-8.14.5-bin/…）。
#   2. AGP 自带的 aapt2 是 linux-x86_64 二进制，在 Android/arm64 上起不来
#      （AarResourcesCompilerTransform: Daemon startup failed）。用 Termux 原生
#      aapt2 覆盖：-Pandroid.aapt2FromMavenOverride=$(command -v aapt2)。
#
# 依赖已在 ~/.gradle/caches 里（离线可编译）。
#
# Usage:
#   scripts/build_shell_local.sh              # 只编译 Kotlin（默认，最快）
#   scripts/build_shell_local.sh --assemble    # 出 debug APK（需要 jniLibs 里有
#                                             # libengine_api.so，否则 APK 不含引擎）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GRADLE_BIN=""
for d in "$HOME"/.gradle/wrapper/dists/gradle-8.14.5-bin/*/gradle-8.14.5/bin/gradle; do
    [ -x "$d" ] && GRADLE_BIN="$d" && break
done
if [ -z "$GRADLE_BIN" ]; then
    echo "找不到缓存的 Gradle 8.14.5，回退系统 gradle（大概率因版本过高而失败）" >&2
    GRADLE_BIN="$(command -v gradle || true)"
fi
[ -n "$GRADLE_BIN" ] || { echo "没有可用的 gradle" >&2; exit 2; }

AAPT2="$(command -v aapt2 || true)"
[ -n "$AAPT2" ] || { echo "找不到 termux 的 aapt2" >&2; exit 2; }

TASK=":app:compileDebugKotlin"
if [ "${1:-}" = "--assemble" ]; then
    TASK=":app:assembleDebug"
fi

cd "$ROOT/app"
exec "$GRADLE_BIN" --offline --no-daemon \
    "-Pandroid.aapt2FromMavenOverride=$AAPT2" \
    "$TASK"
