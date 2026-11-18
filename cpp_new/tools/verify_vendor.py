#!/usr/bin/env python3
"""校验 vendor/krkrz 的完整性与补丁面（见 vendor/krkrz/VENDOR.md）。

检查三件事：
  1. 每个 vendored 文件的 sha256 与登记的"本地"值一致（有人改了上游文件会立刻发现）；
  2. 补丁 #1（`##(` → ` (`) 已应用：文件里不应再出现 `##(`；
  3. 补丁 #2（`screen_blend_func` 补回 `~`）已应用。

用法：python3 cpp_new/tools/verify_vendor.py [cpp_new 根目录]
退出码：0 通过，1 不一致。
"""
import hashlib
import os
import sys

# 本地（打完补丁后）的 sha256。更新上游文件时必须同步本表与 VENDOR.md。
FILES = {
    "visual/tvpgl.c": "eb340b75da8c43a7bad100938dcbc9fcab416fb58d7c247d706c13b701fe6074",
    "visual/tvpgl.h": "62a373612b56853ef0b8855521434c935c871a2c8eca15f8cde1b860bac02cdd",
    "visual/drawable.h": "a28b9b93d29c5bf8e3c3414d07c4e5e3a8e57e2c9187a2a14e03f1464d83ce22",
    "visual/gl/blend_function.cpp": "f6a85099329385529ac4aca986cebbbe2e7b93794560e08a64a4a2dc947651cc",
    "visual/gl/blend_functor_c.h": "d707fa39e00d6933f1409e382feca2e318dfcbe689e30dfafae13bac30a03f7a",
    "visual/gl/blend_util_func.h": "a0e3d5bc2172e9069c60efdc334aef0e1df0c8e1cfef3b1f48b3b918fc8ba9f3",
    "visual/gl/blend_variation.h": "af7252e3fbe5898997fe02c34612e05f5cb7b6180f84389e6c56e6f2d76a31b4",
    "visual/gl/tvpgl_mathutil.h": "f2dcdec474ec998c2a125b4d707d910592de12d82f5ea87a45896589f5fbb09f",
    "visual/IA32/tvpgl_ia32_intf.h": "03aadbbd029fbb1c5942d970c1ccff1c88992be2a693e3cd8954e44bdcecddc6",
    "visual/GraphicsLoaderIntf.h": "4c924e13d6b6932b15ef9189379a3533cc235755201d669b1e79b4ba7dcdcd73",
}

# 我们的适配层（不是上游代码），只要求存在。
PORTING = [
    "visual/porting/toolchain_compat.h",
    "visual/porting/pixel_routines.h",
    "visual/porting/debug_output.h",
    "visual/porting/debug_output.cpp",
    "msg/MsgIntf.h",
    "msg/MsgIntf.cpp",
    "msg/generated/messages.inc",
]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    vendor = os.path.join(os.path.abspath(root), "vendor", "krkrz")
    problems = []

    for relative, expected in sorted(FILES.items()):
        path = os.path.join(vendor, relative)
        if not os.path.isfile(path):
            problems.append(f"{relative}: 缺失")
            continue
        actual = hashlib.sha256(open(path, "rb").read()).hexdigest()
        if actual != expected:
            problems.append(f"{relative}: sha256 不符（登记 {expected[:12]}…，实际 {actual[:12]}…）")

    blend_function = os.path.join(vendor, "visual/gl/blend_function.cpp")
    if os.path.isfile(blend_function):
        if b"##(" in open(blend_function, "rb").read():
            problems.append("visual/gl/blend_function.cpp: 补丁 #1 未应用（仍存在 `##(`）")

    loader_header = os.path.join(vendor, "visual/GraphicsLoaderIntf.h")
    if os.path.isfile(loader_header):
        if b"class TJS::tTJSBinaryStream;" in open(loader_header, "rb").read():
            problems.append("visual/GraphicsLoaderIntf.h: 补丁 #3 未应用（嵌套名字前置声明）")

    blend_functor = os.path.join(vendor, "visual/gl/blend_functor_c.h")
    if os.path.isfile(blend_functor):
        text = open(blend_functor, "rb").read().decode("utf-8", "replace")
        start = text.find("struct screen_blend_func")
        if start < 0:
            problems.append("visual/gl/blend_functor_c.h: 找不到 screen_blend_func（上游结构变了？）")
        elif "return ~tmp;" not in text[start:start + 800]:
            problems.append("visual/gl/blend_functor_c.h: 补丁 #2 未应用（screen_blend_func 缺 `~`）")

    for relative in PORTING:
        if not os.path.isfile(os.path.join(vendor, relative)):
            problems.append(f"{relative}: 缺失（适配层文件）")

    for problem in problems:
        print(f"FAIL  {problem}")
    print(f"检查完成：{len(FILES)} 个上游文件、{len(PORTING)} 个适配文件，{len(problems)} 处不一致")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
