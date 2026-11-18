#!/usr/bin/env python3
"""check_qualification.py — 查出"在自己 namespace 里还写全限定名"的非法代码

背景：本仓库的机械改名（已随老树冻结移除）曾采用"owner 目录里用短名、别处用全限定名"，
因为全限定在 C++ 里**大多数**位置合法。但有一个位置例外，而且非常容易撞上：

    namespace krkr::utils {
    void krkr::utils::AddLog(...) { }   // ← 错：namespace 内的明确定义不能再限定
    }

GCC/Clang 都会报 "explicit qualification in declaration of ..."（或
"extra qualification"）。也就是说：**只要某个模块的实现文件在别的模块目录里**
（典型例子：utils 的 AddLog 定义在 base/runtime/DebugIntf.cpp，而那个文件也在
`namespace krkr::utils` 里），机械改名就会产出编译不过的代码。

所以这个脚本是 rename 之后的**必跑检查**（不需要编译器）：

    python3 scripts/check_qualification.py            # 全仓
    python3 scripts/check_qualification.py --fix cpp/core/base/runtime/DebugIntf.cpp

判据：在 `namespace krkr::<mod>`（含嵌套写法）作用域内，出现
`krkr::<mod>::` 形式的限定名即视为违规。注释与字符串里的不算。

退出码：0 = 干净 / 1 = 有违规。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".inc", ".mm"}
DEFAULT_ROOTS = ["cpp", "bridge", "tools"]

IDENT_RE = re.compile(r"krkr\s*::\s*(\w+)\s*::")

# 例外：这些位置的限定名**故意**保留，脚本不再报。
#   ncbind.hpp —— 模板显式特化/实例化写在宏体里，展开点在各插件的全局作用域，
#   未限定的 declarator-id 违反 [temp.expl.spec]，必须写全限定（DECISIONS.md #2）。
EXCEPTION_FILES = {
    "cpp/core/plugin/ncb/ncbind.hpp",
}


def blank_non_code(text: str) -> str:
    """把注释/字符串/字符字面量替换成等长空白，保持行号与列号不变。"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            quote = c
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                if text[j] == "\n" and quote == "'":
                    break
                j += 1
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


NAMESPACE_RE = re.compile(r"\bnamespace\s+([\w:\s]+?)\s*\{")


def scan(path: Path) -> list[tuple[int, str, str]]:
    """返回 [(行号, 违规文本, 所在 namespace)]。"""
    text = path.read_text(encoding="utf-8", errors="replace")
    code = blank_non_code(text)
    lines = code.splitlines()
    raw_lines = text.splitlines()

    violations = []
    spans: list[int] = []
    stack: list[str] = []          # 每层作用域对应的 namespace 路径（非 namespace 层是 ""）
    pending: list[str] = []        # 上一行/本行检测到的 namespace 头，等待 '{'

    # 逐字符走，遇到 '{' / '}' 维护作用域栈；同时在同一遍里记录 namespace 头。
    i, n = 0, len(code)
    line_no = 1
    while i < n:
        c = code[i]
        if c == "\n":
            line_no += 1
            i += 1
            continue
        if c == "{":
            # 看这个 '{' 之前同一行是否是 namespace 声明
            bol = code.rfind("\n", 0, i) + 1
            head = code[bol:i]
            m = re.search(r"\bnamespace\s+([\w:]+)\s*$", head)
            if m:
                path_parts = [p for p in m.group(1).split("::") if p]
                parent = stack[-1] if stack else ""
                full = "::".join(([parent] if parent else []) + path_parts)
                stack.append(full)
            else:
                stack.append("")
            i += 1
            continue
        if c == "}":
            if stack:
                stack.pop()
            i += 1
            continue
        m = IDENT_RE.match(code, i)
        if m:
            mod = m.group(1)
            cur = stack[-1] if stack else ""
            if cur == f"krkr::{mod}":
                violations.append((line_no, raw_lines[line_no - 1].strip(), cur))
                spans.append(i)
            i = m.end()
            continue
        i += 1
    _ = (pending, lines)
    return violations, spans


def strip_spans(text: str, spans: list[int]) -> str:
    """把 span 处的 `krkr::<mod>::` 前缀删掉（从后往前删，避免位置漂移）。"""
    for start in sorted(spans, reverse=True):
        m = IDENT_RE.match(text, start)
        if m:
            text = text[:start] + text[m.end():]
    return text


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--fix"]
    fix = "--fix" in sys.argv[1:]
    if args:
        paths = [Path(a) if Path(a).is_absolute() else REPO_ROOT / a for a in args]
    else:
        paths = []
        for root in DEFAULT_ROOTS:
            base = REPO_ROOT / root
            if not base.is_dir():
                continue
            paths += [p for p in base.rglob("*")
                      if p.is_file() and p.suffix in SOURCE_SUFFIXES]

    total = 0
    files = 0
    fixed = 0
    for p in sorted(paths):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if rel in EXCEPTION_FILES:
            continue
        v, spans = scan(p)
        if not v:
            continue
        if fix:
            text = p.read_text(encoding="utf-8")
            p.write_text(strip_spans(text, spans), encoding="utf-8", newline="")
            fixed += len(spans)
            print(f"{rel}: 去掉 {len(spans)} 处冗余限定")
            continue
        files += 1
        total += len(v)
        print(f"{p.relative_to(REPO_ROOT)}: {len(v)} 处")
        for line_no, line, ns in v[:8]:
            print(f"    {line_no}: [{ns}] {line[:100]}")
        if len(v) > 8:
            print(f"    … 还有 {len(v) - 8} 处")

    print()
    if fix:
        print(f"✓ 已去掉 {fixed} 处冗余限定。跑一遍不带 --fix 的检查确认。")
        return 0
    if total:
        print(f"✗ {total} 处“在自己 namespace 内还写全限定名”（{files} 个文件），"
              f"这些位置 GCC/Clang 会报 explicit qualification。")
        return 1
    print(f"✓ 没有“namespace 内重复限定”的问题（检查 {len(paths)} 个文件）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
