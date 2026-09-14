#!/usr/bin/env python3
"""校验代码里直接调用的 GL/EGL 函数在目标平台上确实存在。

为什么需要：本仓库的 cpp/ 里有相当一部分代码是桌面 GL / Kodi 渲染器 heritage
（ffmpeg 那套、AlphaMovie 等），容易混入 Android 上并不提供的符号（GLES1 固定
管线、桌面 GL 专有函数）。这类问题在链接期才暴露，表现为一条 `undefined symbol`
——而去掉 ANGLE 之后，原本由 ANGLE 的 libGLESv2 兜住的符号面不再有冗余覆盖，
这种错误更容易漏出来。

本脚本只检查**直接调用**（`glFoo(`）——经由 `GL::` 命名空间包装、用
eglGetProcAddress 动态加载的用法不需要链接期符号，不在检查范围。

库来源按顺序探测，取第一个存在的：
  Android(Termux): /system/lib64/libGLESv2.so, /vendor/lib64/...
  Linux(CI):       /usr/lib/x86_64-linux-gnu/libGLESv2.so (libgles2-mesa-dev)

找不到任何库时跳过（不算失败）——例如在没有 GL 库的容器里跑静态检查。

用法：python3 scripts/check_gl_symbols.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

GL_LIB_CANDIDATES = [
    "/system/lib64/libGLESv2.so",
    "/vendor/lib64/libGLESv2.so",
    "/usr/lib/x86_64-linux-gnu/libGLESv2.so",
    "/usr/lib/aarch64-linux-gnu/libGLESv2.so",
    "/usr/lib/libGLESv2.so",
]
EGL_LIB_CANDIDATES = [
    "/system/lib64/libEGL.so",
    "/vendor/lib64/libEGL.so",
    "/usr/lib/x86_64-linux-gnu/libEGL.so",
    "/usr/lib/aarch64-linux-gnu/libEGL.so",
    "/usr/lib/libEGL.so",
]

SOURCE_SUFFIXES = {".cpp", ".cc", ".h", ".hpp"}


def exported_symbols(lib: str) -> set[str]:
    """读取动态库导出符号（去掉版本后缀）。"""
    try:
        out = subprocess.run(
            ["nm", "-D", "--defined-only", lib],
            capture_output=True, text=True, check=True,
        ).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return set()
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            syms.add(parts[-1].split("@")[0])
    return syms


def strip_comments(src: str) -> str:
    """去掉注释，保留代码。

    必须做这一步：本仓库里有不少函数名出现在注释里（例如描述某段实现用到了
    glVertex4f），把它们当成真实调用会制造假阳性——而一个会误报的检查等于没有
    检查，人会开始忽略它。

    同时跟踪字符串/字符字面量，避免把 "http://..." 里的 // 当成行注释开头。
    宁可漏掉注释里的内容，也不能误删真实代码。
    """
    out: list[str] = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == "\\":          # 转义：连同下一个字符一起吞掉
                    if i + 1 < n:
                        out.append(src[i + 1])
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n:
            if src[i + 1] == "/":
                while i < n and src[i] != "\n":
                    i += 1
                continue
            if src[i + 1] == "*":
                i += 2
                while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                    i += 1
                i += 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def direct_calls(prefix: str) -> dict[str, set[str]]:
    """收集直接调用的函数（排除 GL::/egl:: 命名空间包装的用法）。"""
    # (?<![\w:>.]) 排除 GL::glFoo( 、obj.glFoo( 、->glFoo(
    pattern = re.compile(rf"(?<![\w:>.]){prefix}([A-Z]\w*)\s*\(")
    found: dict[str, set[str]] = {}
    for path in REPO_ROOT.joinpath("cpp").rglob("*"):
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        try:
            src = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for m in pattern.finditer(strip_comments(src)):
            found.setdefault(prefix + m.group(1), set()).add(
                str(path.relative_to(REPO_ROOT)))
    return found


def check(label: str, prefix: str, candidates: list[str]) -> bool:
    lib = next((c for c in candidates if Path(c).exists()), None)
    if lib is None:
        print(f"跳过 {label}：找不到可用的库（尝试过 {len(candidates)} 个路径）")
        return True

    calls = direct_calls(prefix)
    if not calls:
        print(f"{label}：没有直接调用，跳过")
        return True

    exported = exported_symbols(lib)
    if not exported:
        print(f"跳过 {label}：无法读取 {lib} 的导出表（缺 nm？）")
        return True

    missing = sorted(s for s in calls if s not in exported)
    if missing:
        print(f"✗ {label}：{len(missing)} 个符号不在 {lib} 中，链接期会 undefined：")
        for sym in missing:
            where = sorted(calls[sym])[:3]
            print(f"    {sym}")
            for w in where:
                print(f"        {w}")
        print("    → 若确实需要，改用 eglGetProcAddress 动态加载，或确认平台支持")
        return False

    print(f"✓ {label}：{len(calls)} 个直接调用全部在 {lib} 中可解析")
    return True


def main() -> int:
    ok = check("GL 符号", "gl", GL_LIB_CANDIDATES)
    ok &= check("EGL 符号", "egl", EGL_LIB_CANDIDATES)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
