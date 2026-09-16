#!/usr/bin/env python3
"""校验 Kotlin 的 external 方法与 C++ 侧 JNI 符号一一对应。

JNI 符号名编码了 Java 包名与类名（`.` → `_`，`_` → `_1`）。改名不一致不会在
编译期报错，只在运行时抛 `UnsatisfiedLinkError`——这类问题最难在真机上定位。
本脚本把这条约束变成可在无 NDK 环境下运行的静态检查。

覆盖的“Kotlin 类 ↔ C++ JNI 实现”配对见 PAIRS。目前两对：

* `NativeEngine`（engine_api 的 C ABI 包装）
* `KR2Activity`（引擎侧硬编码的消息框回调落点，包名固定 org.tvp.kirikiri2）

用法:
    python3 scripts/check_jni_symbols.py

退出码 0 表示一致；1 表示有缺失/多余。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Kotlin 源 ↔ C++ JNI 实现。**包名与类名都不写死常量**——从 Kotlin 文件里的
# `package` 声明与 `object` 名字读。
#
# 为什么：写成常量的话，重命名 Kotlin 包/类却忘了改常量，脚本仍会用旧名字去
# C++ 里找符号并"通过"——检查和被检查的对象一起错了，等于没检查。
# 从源头读才能保证校验的是真实状态。
PAIRS = (
    (
        REPO_ROOT / "app/app/src/main/kotlin/org/dpdns/clevebitr/core/NativeEngine.kt",
        REPO_ROOT / "bridge/engine_api/src/engine_api_android_jni.cpp",
    ),
    (
        REPO_ROOT / "app/app/src/main/kotlin/org/tvp/kirikiri2/KR2Activity.kt",
        REPO_ROOT / "cpp/core/environ/android/AndroidUtils.cpp",
    ),
)


def kotlin_package(path: Path) -> str:
    """从 Kotlin 源里读 `package` 声明。"""
    m = re.search(r"^\s*package\s+([\w.]+)\s*$", path.read_text(encoding="utf-8"), re.M)
    if not m:
        raise SystemExit(f"错误：{path} 里找不到 package 声明")
    return m.group(1)


def kotlin_object(path: Path) -> str:
    """从 Kotlin 源里读 `object X` 的名字（JNI 符号里的类名）。"""
    m = re.search(r"^\s*object\s+(\w+)", path.read_text(encoding="utf-8"), re.M)
    if not m:
        raise SystemExit(f"错误：{path} 里找不到 object 声明")
    return m.group(1)


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


def check_pair(kotlin_source: Path, jni_source: Path) -> bool:
    for p in (kotlin_source, jni_source):
        if not p.is_file():
            print(f"错误：找不到 {p}", file=sys.stderr)
            return False

    package = kotlin_package(kotlin_source)
    klass = kotlin_object(kotlin_source)
    prefix = jni_prefix(package, klass)
    kt = kotlin_externals(kotlin_source)
    cpp = cpp_symbols(jni_source, prefix)

    print(f"── {package}.{klass}")
    print(f"   Kotlin external 方法: {len(kt)} 个")
    print(f"   C++ JNI 符号:         {len(cpp)} 个  (前缀 {prefix})")

    missing_in_cpp = sorted(kt - cpp)
    missing_in_kt = sorted(cpp - kt)

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
    if ok:
        print("   ✓ 一致。")
    return ok


def main() -> int:
    ok = True
    for kotlin_source, jni_source in PAIRS:
        if not check_pair(kotlin_source, jni_source):
            ok = False

    if not ok:
        print("\n提示：两边的包名/类名/方法名必须完全一致。")
        return 1

    print("\n✓ Kotlin 与 C++ 的 JNI 符号一致。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
