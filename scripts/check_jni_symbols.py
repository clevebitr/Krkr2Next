#!/usr/bin/env python3
"""校验 Kotlin 的 external 方法与 C++ 侧 JNI 符号一一对应。

JNI 符号名编码了 Java 包名与类名（`.` → `_`，`_` → `_1`）。改名不一致不会在
编译期报错，只在运行时抛 `UnsatisfiedLinkError`——这类问题最难在真机上定位。
本脚本把这条约束变成可在无 NDK 环境下运行的静态检查。

用法:
    python3 scripts/check_jni_symbols.py

退出码 0 表示一致；1 表示有缺失/多余。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Kotlin 侧：包名、类名、源文件
KOTLIN_PACKAGE = "dev.kirinext.core"
KOTLIN_OBJECT = "NativeEngine"
KOTLIN_SOURCE = REPO_ROOT / "app/app/src/main/kotlin/dev/kirinext/core/NativeEngine.kt"

# C++ 侧：JNI 实现
JNI_SOURCE = REPO_ROOT / "bridge/engine_api/src/engine_api_android_jni.cpp"


def jni_prefix(package: str, klass: str) -> str:
    """Java 包名 + 类名 → JNI 符号前缀（`.` → `_`，`_` → `_1`）。"""
    mangled = f"{package}.{klass}".replace("_", "_1").replace(".", "_")
    return f"Java_{mangled}_"


def kotlin_externals(path: Path) -> set[str]:
    """从 Kotlin 源里抓取所有 external fun 的方法名。"""
    text = path.read_text(encoding="utf-8")
    # 匹配 `external fun name(` 以及多行的 `external fun name(`
    return set(re.findall(r"\bexternal\s+fun\s+(\w+)\s*\(", text))


def cpp_symbols(path: Path, prefix: str) -> set[str]:
    """从 C++ 源里抓取以给定前缀开头的 JNI 符号，返回去掉前缀的方法名。"""
    text = path.read_text(encoding="utf-8")
    found = re.findall(rf"\b{re.escape(prefix)}(\w+)\s*\(", text)
    return set(found)


def main() -> int:
    for p in (KOTLIN_SOURCE, JNI_SOURCE):
        if not p.is_file():
            print(f"错误：找不到 {p}", file=sys.stderr)
            return 1

    prefix = jni_prefix(KOTLIN_PACKAGE, KOTLIN_OBJECT)
    kt = kotlin_externals(KOTLIN_SOURCE)
    cpp = cpp_symbols(JNI_SOURCE, prefix)

    # 注意：Kotlin 侧有 engine* 包装 + 三个 native* 入口；C++ 侧同名。
    missing_in_cpp = sorted(kt - cpp)
    missing_in_kt = sorted(cpp - kt)

    print(f"Kotlin external 方法: {len(kt)} 个")
    print(f"C++ JNI 符号:         {len(cpp)} 个  (前缀 {prefix})")

    ok = True
    if missing_in_cpp:
        ok = False
        print("\n✗ Kotlin 声明了 external，但 C++ 没有对应符号：")
        for name in missing_in_cpp:
            print(f"    {name}  →  期望 {prefix}{name}")
    if missing_in_kt:
        ok = False
        print("\n✗ C++ 有 JNI 符号，但 Kotlin 没有对应 external 声明：")
        for name in missing_in_kt:
            print(f"    {prefix}{name}")

    if not ok:
        print("\n提示：两边的包名/类名/方法名必须完全一致。")
        return 1

    print("\n✓ Kotlin 与 C++ 的 JNI 符号一致。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
