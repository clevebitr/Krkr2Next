#!/usr/bin/env bash
# 构建并运行 cpp_new 的全部测试台与脚本断言（CMake + CTest）。
#
#   bash cpp_new/tools/run_probes.sh [debug|release]
#
# 需要 cmake 与 ninja 在 PATH 上（Android SDK 的 cmake 亦可）。
# 断言脚本在 host/fixtures/demo，通过 CTest 注册，末尾抛 "<name>-ok" 表示通过。
set -euo pipefail
cd "$(dirname "$0")/.."

PRESET="desktop-${1:-debug}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake 不在 PATH 上；请先把它加入 PATH（例如 /f/runtime/cmake/*/bin）" >&2
  exit 2
fi

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
ctest --preset "$PRESET"

# 真游戏回归尚未通过（当前缺口见 todo.md §11），因此不进 CTest 的失败集。
REAL_ROOT="../build/real_root.txt"
REAL_SCRIPT="../build/py_startup.utf8.tjs"
if [ -f "$REAL_ROOT" ] && [ -f "$REAL_SCRIPT" ]; then
  echo
  if "../build/cmake/${1:-debug}/bin/tjs_probe.exe" "$REAL_SCRIPT" "$REAL_ROOT" \
      >"../build/real_probe.log" 2>&1; then
    echo "ok   real startup"
  else
    echo "gap  real startup:"
    grep -E '^exec|\[error\]' ../build/real_probe.log | tail -2 | sed 's/^/     /'
  fi
fi
