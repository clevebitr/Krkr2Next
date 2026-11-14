#!/usr/bin/env python3
"""rename_symbols.py — 契约驱动的机械改名（docs/NAMING.md 的执行工具）

两种模式：

  1) 执行模式：按 map 文件改名（默认 dry-run）
       python3 scripts/rename_symbols.py --map scripts/rename/maps/extension.tsv
       python3 scripts/rename_symbols.py --map scripts/rename/maps/extension.tsv --apply

  2) 起草模式：从模块里扫出遗留符号，机械生成一份**待人工审阅**的 map
       python3 scripts/rename_symbols.py --draft io --out scripts/rename/maps/io.tsv

map 文件格式（TSV，`#` 开头是注释）：

    # module: extension           ← 必须：决定全限定前缀 krkr::extension
    # qualify: krkr::extension    ← 可选，默认 krkr::<module>
    # owner: cpp/core/extension   ← 可选，默认 cpp/core/<module>
    TVPAddClassHandler	AddClassHandler	fn
    tTVPAtInstallClass	AtInstallClass	type

第三列 kind ∈ fn|type|macro|var：`macro` 不做 namespace 限定（宏没有 namespace）。

为什么外部调用点替换成**全限定名**（`krkr::extension::AddClassHandler`）而不是短名：
全限定在 C++ 里总是合法（哪怕调用点已经在那个 namespace 内），所以工具不需要
理解调用点所在的作用域 —— 这是"没有编译器"时唯一可靠的策略。代价是插件侧会出现
一些显式前缀，这正是契约 §3 想要的效果（旧名字彻底消失）。

安全检查（apply 之前全部会打印）：
  · map 内新名重复
  · 新名在仓库里**已经存在**（潜在冲突，需要人判断）
  · 旧名在目标根里零命中（map 过期）
  · 注释/字符串里的命中数（默认改注释、跳过字符串字面量）
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ROOTS = ["cpp", "bridge", "tools"]
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".inc", ".mm"}

# 与 check_naming.py 保持一致的例外（起草模式用）
DRAFT_EXCEPTION_FILES = {
    "cpp/core/base/core/MsgIntfInc.h",
    "cpp/core/base/core/MsgImpl.h",
    "cpp/core/sound/impl/kmp_pi.h",
    "cpp/core/sound/impl/oldwaveunpacker.h",
    "cpp/core/sound/impl/tvpsnd.h",
    "cpp/core/sound/impl/tvpsnd.cpp",
    "cpp/core/utils/c_api/md5.h",
    "cpp/core/utils/c_api/md5.c",
    "cpp/core/utils/c_api/iconv/iconv.h",
    "cpp/core/utils/c_api/iconv/utf8.h",
    "cpp/core/utils/c_api/encoding/gbk2unicode.c",
    "cpp/core/utils/c_api/encoding/jis2unicode.c",
    "cpp/core/plugin/ncb/ncbind.hpp",
    "cpp/core/plugin/ncb/ncbind.cpp",
    "cpp/core/plugin/ncb/ncb_invoke.hpp",
    "cpp/core/plugin/ncb/ncb_foreach.inc",
    "cpp/core/io/archive/tar.h",
    "cpp/core/tjs2",
}
LEGACY_SYMBOL_RE = re.compile(r"\b(?:i|t)?TVP[A-Z][A-Za-z0-9_]*\b")
MSG_DEFINE_RE = re.compile(r"TVP_MSG_DEFINE\(\s*(\w+)")


# --------------------------------------------------------------------------
# C++ 扫描：区分代码 / 注释 / 字符串，保证只改该改的地方
# --------------------------------------------------------------------------
def split_regions(text: str) -> list[tuple[str, str]]:
    """把源码切成 [(kind, chunk)]，kind ∈ code|line_comment|block_comment|string|char。

    不需要完整 C++ 词法，只要不错判 `"..."` 里的引号与 `//` 里的 `//`。
    """
    out: list[tuple[str, str]] = []
    i, n = 0, len(text)
    buf: list[str] = []

    def flush(kind: str = "code") -> None:
        if buf:
            out.append((kind, "".join(buf)))
            buf.clear()

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            flush()
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(("line_comment", text[i:j]))
            i = j
        elif c == "/" and nxt == "*":
            flush()
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(("block_comment", text[i:j]))
            i = j
        elif c in "\"'":
            flush()
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
            out.append(("string" if quote == '"' else "char", text[i:j]))
            i = j
        else:
            buf.append(c)
            i += 1
    flush()
    return out


def iter_sources(roots: list[str]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        base = REPO_ROOT / root
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if p.is_file() and p.suffix in SOURCE_SUFFIXES:
                files.append(p)
    return sorted(files)


# --------------------------------------------------------------------------
# map 解析
# --------------------------------------------------------------------------
class MapEntry:
    __slots__ = ("old", "new", "kind")

    def __init__(self, old: str, new: str, kind: str):
        self.old, self.new, self.kind = old, new, kind


def parse_map(path: Path) -> tuple[str, str, str, list[MapEntry]]:
    module = qualify = owner = ""
    entries: list[MapEntry] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            m = re.match(r"#\s*(module|qualify|owner)\s*:\s*(\S+)", stripped)
            if m:
                key, val = m.group(1), m.group(2)
                if key == "module":
                    module = val
                elif key == "qualify":
                    qualify = val
                else:
                    owner = val
            continue
        parts = stripped.split("\t")
        if len(parts) < 3:
            print(f"{path}:{lineno}: 需要 3 列（old/new/kind），跳过", file=sys.stderr)
            continue
        old, new, kind = parts[0], parts[1], parts[2]
        if kind not in ("fn", "type", "macro", "var"):
            print(f"{path}:{lineno}: 未知 kind={kind}，跳过", file=sys.stderr)
            continue
        entries.append(MapEntry(old, new, kind))

    if not module:
        sys.exit(f"{path}: 缺少 `# module: <name>` 头")
    return module, qualify or f"krkr::{module}", owner or f"cpp/core/{module}", entries


# --------------------------------------------------------------------------
# 执行模式
# --------------------------------------------------------------------------
def run_map(map_path: Path, roots: list[str], apply: bool, in_strings: bool,
            no_comments: bool, drop_bridges: bool) -> int:
    module, qualify, owner, entries = parse_map(map_path)
    owner_path = (REPO_ROOT / owner).resolve()

    print(f"模块 {module}  owner={owner}  限定前缀={qualify}  改名 {len(entries)} 个符号\n")

    # 1) map 内部重复
    seen: dict[str, str] = {}
    dup = False
    for e in entries:
        if e.new in seen:
            print(f"✗ 新名重复: {e.new}（{seen[e.new]} 与 {e.old}）", file=sys.stderr)
            dup = True
        seen[e.new] = e.old
    if dup:
        return 1

    files = iter_sources(roots)
    texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in files}

    # 2) 旧名命中统计（owner / 外部）与新名冲突检测
    old_re = {
        e.old: re.compile(r"(?:%s::)?\b%s\b" % (re.escape(qualify), re.escape(e.old)))
        for e in entries
    }
    new_re = {e.new: re.compile(r"\b%s\b" % re.escape(e.new)) for e in entries}

    hits_owner: dict[str, int] = {e.old: 0 for e in entries}
    hits_ext: dict[str, int] = {e.old: 0 for e in entries}
    # (entry.old, file) -> (old_hits, new_hits)
    per_file: dict[tuple[str, Path], tuple[int, int]] = {}

    for p, text in texts.items():
        in_owner = owner_path in p.resolve().parents or p.resolve() == owner_path
        for e in entries:
            old_n = len(old_re[e.old].findall(text))
            new_n = len(new_re[e.new].findall(text)) if e.new != e.old else 0
            per_file[(e.old, p)] = (old_n, new_n)
            if old_n:
                if in_owner:
                    hits_owner[e.old] += old_n
                else:
                    hits_ext[e.old] += old_n

    # 新名冲突的判据：仓库里**已经有声明**（class/struct/enum/typedef/#define/函数定义）。
    # 只看"出现过这个词"会把 `class XP3Archive` 与注释里的 Archive 混为一谈，
    # 噪声大到没法用（第一版就是这么错的）。
    def declared_re(name: str) -> re.Pattern:
        n = re.escape(name)
        return re.compile(
            r"(?:\b(?:class|struct|enum(?:\s+class)?|typedef|#\s*define)\b[^;\n]*?\b%s\b)"
            r"|(?:^[ \t]*(?:[A-Za-z_][\w:<>,*&]*[\s*&])%s\s*\()" % (n, n),
            re.MULTILINE,
        )

    declared = {e.new: [t for t in texts.values() if declared_re(e.new).search(t)] for e in entries}

    conflicts: dict[str, list[str]] = {}
    done: set[str] = set()
    stale: list[str] = []

    for e in entries:
        total = hits_owner[e.old] + hits_ext[e.old]
        if total == 0:
            # 旧名全仓消失：要么上一轮已经改过（新名在），要么 map 过期。
            if any(new_re[e.new].search(t) for t in texts.values()):
                done.add(e.old)
            else:
                stale.append(e.old)
            continue
        # 旧名还在，但某些文件里已经出现了新名、且该文件里没有旧名 →
        # 那个新名可能是别的符号（真冲突），也可能是手工先改过一半（需人看）。
        existing_decls = declared.get(e.new) or []
        if existing_decls:
            conflicts[e.new] = existing_decls

    print(f"{'旧名':<52}{'新名':<40}{'owner':>7}{'外部':>7}  状态")
    print("-" * 118)
    for e in entries:
        status = []
        if e.old in done:
            status.append("✓ 已完成（旧名已消失）")
        if e.old in stale:
            status.append("✗ 零命中且新名不存在（map 过期）")
        if e.new in conflicts:
            status.append("⚠ 新名已存在 → 全仓（含 owner）用全限定名，建议改 map 换个名字")
        print(
            f"{e.old:<52}{e.new:<40}{hits_owner[e.old]:>7}{hits_ext[e.old]:>7}  "
            + " ".join(status)
        )

    if conflicts:
        print("\n⚠ 新名在仓库里已有同名声明（apply 前请人工确认/改名）：")
        for new in sorted(conflicts):
            where = sorted(
                str(p.relative_to(REPO_ROOT)).replace("\\", "/")
                for p, t in texts.items() if declared_re(new).search(t)
            )
            print(f"   {new}: " + ", ".join(where[:3]) + (" …" if len(where) > 3 else ""))

    if stale:
        print(
            f"\n✗ 有 {len(stale)} 个旧名既不在仓库里、新名也不存在，map 可能过期。",
            file=sys.stderr,
        )
        return 1

    # 3) 逐文件替换
    changed: dict[Path, int] = {}
    comment_hits = 0
    string_hits = 0
    bridge_removed = 0

    bridge_line_re = re.compile(r"^\s*using\s+%s::(\w+);\s*$" % re.escape(qualify))
    renamed_names = {e.new for e in entries} | {e.old for e in entries}

    for p, text in texts.items():
        in_owner = owner_path in p.resolve().parents or p.resolve() == owner_path

        # 先删过渡 using，再替换符号：反过来的话 `using krkr::m::Old;` 会先变成
        # `using New;`（非法 C++），删桥接的正则就再也匹配不上了。
        if drop_bridges and in_owner:
            # 只删**本轮真的改名**的那些桥接：`using krkr::plugin::TVP_malloc;` 这种
            # 对应符号仍在用旧名（契约 §4 的 ABI 例外），删掉会让插件侧编译失败。
            kept = []
            for line in text.splitlines(keepends=True):
                m = bridge_line_re.match(line)
                if m and m.group(1) in renamed_names:
                    bridge_removed += 1
                    continue
                kept.append(line)
            text = "".join(kept)

        regions = split_regions(text)
        out: list[str] = []
        file_hits = 0
        for kind, chunk in regions:
            if kind in ("string", "char") and not in_strings:
                for e in entries:
                    string_hits += len(old_re[e.old].findall(chunk))
                out.append(chunk)
                continue
            if kind in ("line_comment", "block_comment") and no_comments:
                out.append(chunk)
                continue
            new_chunk = chunk
            for e in entries:
                # macro 没有 namespace；冲突条目（新名与已有声明同名，例如与某个类的
                # 成员函数同名）在 owner 内也必须全限定，否则会被成员函数遮蔽 ——
                # 这类遮蔽在 C++ 里是静默改语义，比编译错误更危险。
                if e.kind == "macro":
                    repl = e.new
                elif in_owner and e.new not in conflicts:
                    repl = e.new
                else:
                    repl = f"{qualify}::{e.new}"
                new_chunk, n = old_re[e.old].subn(repl, new_chunk)
                if n:
                    file_hits += n
                    if kind in ("line_comment", "block_comment"):
                        comment_hits += n
            out.append(new_chunk)
        new_text = "".join(out)

        if new_text != text:
            changed[p] = file_hits
            texts[p] = new_text

    total = sum(changed.values())
    print(f"\n命中: {total} 处，涉及 {len(changed)} 个文件"
          f"（其中注释 {comment_hits} 处、跳过字符串 {string_hits} 处）")
    for p, n in sorted(changed.items(), key=lambda kv: -kv[1])[:20]:
        print(f"   {n:4d}  {p.relative_to(REPO_ROOT)}")
    if bridge_removed:
        print(f"   删除 owner 内过渡 using: {bridge_removed} 行")

    if not apply:
        print("\n(dry-run) 没有写入任何文件。确认无误后加 --apply。")
        return 0

    for p, _ in changed.items():
        p.write_text(texts[p], encoding="utf-8", newline="")
    print(f"\n✓ 已写入 {len(changed)} 个文件。")
    print("下一步：python3 scripts/check_naming.py --module %s -v" % module)
    return 0


# --------------------------------------------------------------------------
# 起草模式
# --------------------------------------------------------------------------
def derive(old: str) -> tuple[str, str]:
    """机械推导新名。只做前缀剥离与大小写规范化，不做语义改写。"""
    if old.startswith("TVP_"):
        return "KRKR_" + old[len("TVP_"):], "macro"
    body = old
    kind = "fn"
    if body.startswith("tTVP"):
        body, kind = body[4:], "type"
    elif body.startswith("iTVP"):
        body, kind = "I" + body[4:], "type"
    elif body.startswith("TVP"):
        body = body[3:]
    # `tTVP`/`iTVP` 之后可能出现小写开头（tTVPalSound…）
    if body and body[0].islower() and not body.startswith("I") is False:
        pass
    if body and body[0].islower():
        body = body[0].upper() + body[1:]
    if not body:
        body = old
    return body, kind


def run_draft(module: str, out_path: Path, roots: list[str]) -> int:
    root = REPO_ROOT / "cpp" / "core" / module
    if not root.is_dir():
        return 2

    exceptions = {str((REPO_ROOT / p).resolve()) for p in DRAFT_EXCEPTION_FILES}
    exceptions_prefix = [str((REPO_ROOT / p).resolve()) for p in DRAFT_EXCEPTION_FILES
                         if not p.endswith((".h", ".hpp", ".c", ".inc", ".cpp"))]

    msg_path = REPO_ROOT / "cpp/core/base/core/MsgIntfInc.h"
    messages = set(MSG_DEFINE_RE.findall(msg_path.read_text(encoding="utf-8", errors="replace"))) \
        if msg_path.is_file() else set()

    # 1) 只收"在本模块里有定义"的符号：定义行判定放宽到 class/struct/typedef/宏/函数
    candidates: dict[str, str] = {}
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix not in SOURCE_SUFFIXES:
            continue
        if str(p.resolve()) in exceptions or any(str(p.resolve()).startswith(x) for x in exceptions_prefix):
            continue
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            stripped = line.strip()
            if stripped.startswith(("//", "*", "/*")):
                continue
            if not stripped:
                continue
            m = re.match(r"(?:class|struct|enum(?:\s+class)?)\s+((?:i|t)?TVP[A-Za-z0-9_]*)", stripped)
            if m:
                candidates.setdefault(m.group(1), "type")
                continue
            m = re.match(r"typedef\b.*?\b((?:i|t)?TVP[A-Za-z0-9_]*)\s*;", stripped)
            if m:
                candidates.setdefault(m.group(1), "type")
                continue
            m = re.match(r"#\s*define\s+((?:i|t)?TVP[A-Za-z0-9_]*)", stripped)
            if m:
                candidates.setdefault(m.group(1), "macro")
                continue
            # 命名空间作用域变量（契约 N7）：`extern TYPE TVPFoo;` /
            # `TYPE TVPFoo = ...;` —— 行首没有 class/struct/typedef/define。
            m = re.match(
                r"(?:extern\s+)?(?:[A-Za-z_][\w:<>,\*&]*[\s\*&]+)+((?:i|t)?TVP[A-Za-z0-9_]*)\s*(?:=|;|\[)",
                stripped,
            )
            if m:
                candidates.setdefault(m.group(1), "var")
                continue

            # 函数定义：行首必须是"返回类型 + 名字("，不能是"名字(" ——
            # 后者是**调用**（缩进被 strip 掉了，光看行首分不出定义与调用）。
            m = re.match(
                r"(?:[A-Za-z_][\w:<>,*&]*[\s*&]+)+((?:i|t)?TVP[A-Za-z0-9_]*)\s*\(", stripped
            )
            if m:
                candidates.setdefault(m.group(1), "fn")

    candidates.pop("TVP2", None)
    for msg in messages:
        candidates.pop(msg, None)

    # 2) 全仓的**声明**（用于冲突检测）。判据与 run_map 一致：
    #    class/struct/enum/typedef/#define 或函数定义行首的名字。
    existing: dict[str, list[str]] = {}
    decl_re = re.compile(
        r"(?:\b(?:class|struct|enum(?:\s+class)?|typedef|#\s*define)\b[^;\n]*?\b([A-Z]\w*)\b)"
        r"|(?:^[ \t]*(?:[A-Za-z_][\w:<>,*&]*[\s*&])([A-Z]\w*)\s*\()",
        re.MULTILINE,
    )
    for p in iter_sources(DEFAULT_ROOTS):
        text = p.read_text(encoding="utf-8", errors="replace")
        for groups in decl_re.findall(text):
            for ident in groups:
                if ident:
                    existing.setdefault(ident, []).append(str(p.relative_to(REPO_ROOT)))

    rows = []
    for old, kind_hint in sorted(candidates.items()):
        new, kind = derive(old)
        kind = kind_hint if kind != "macro" else kind
        status = ""
        if new == old:
            status = "无变化"
        elif new in existing:
            where = sorted(set(existing[new]))[:3]
            status = "冲突:" + ",".join(where)
        rows.append((old, new, kind, status))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# module: {module}",
        f"# qualify: krkr::{module}",
        f"# owner: cpp/core/{module}",
        "#",
        "# 由 rename_symbols.py --draft 生成：只做前缀剥离，未做语义审阅。",
        "# 第 4 列是提示（冲突/无变化），apply 时忽略；人工确认后请删掉或改掉。",
    ]
    for old, new, kind, status in rows:
        lines.append(f"{old}\t{new}\t{kind}\t{status}")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    conflicts = [r for r in rows if r[3].startswith("冲突")]
    print(f"起草 {module}: {len(rows)} 个符号 → {out_path.relative_to(REPO_ROOT)}")
    print(f"  其中新名冲突 {len(conflicts)} 个（需要人工改名）：")
    for old, new, _kind, status in conflicts[:25]:
        print(f"    {old:<52}{new:<32}{status}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="契约驱动的机械改名")
    ap.add_argument("--map", type=Path, help="map 文件（执行模式）")
    ap.add_argument("--draft", metavar="MODULE", help="起草某模块的 map")
    ap.add_argument("--out", type=Path, help="--draft 的输出路径")
    ap.add_argument("--apply", action="store_true", help="真的写文件（默认 dry-run）")
    ap.add_argument("--roots", nargs="+", default=DEFAULT_ROOTS, help=f"扫描根，默认 {DEFAULT_ROOTS}")
    ap.add_argument("--strings", action="store_true", help="连字符串字面量一起改（默认跳过）")
    ap.add_argument("--no-comments", action="store_true", help="不改注释（默认改）")
    ap.add_argument("--keep-bridges", action="store_true", help="保留 owner 里的过渡 using（默认删）")
    args = ap.parse_args()

    if args.draft:
        out = args.out or (REPO_ROOT / "scripts/rename/maps" / f"{args.draft}.tsv")
        return run_draft(args.draft, out, args.roots)

    if not args.map:
        ap.error("需要 --map 或 --draft")
    if not args.map.is_file():
        return 2
    return run_map(
        args.map,
        args.roots,
        args.apply,
        args.strings,
        args.no_comments,
        drop_bridges=not args.keep_bridges,
    )


if __name__ == "__main__":
    sys.exit(main())
