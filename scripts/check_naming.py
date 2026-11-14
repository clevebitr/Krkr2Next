#!/usr/bin/env python3
"""check_naming.py — 命名契约的存量 ratchet（不需要编译器 / NDK / vcpkg）

背景见 docs/NAMING.md。契约是"N3 去 TVP 前缀 / N4 去 t·iTVP / N2 文件名 PascalCase /
§3 过渡 using 只能减少"。本脚本不判断"新名字好不好"，只做三件可机械判断的事：

  1. legacy_symbols  某模块里还出现多少个唯一的遗留符号（`TVP*` / `tTVP*` / `iTVP*`）
  2. legacy_files    某模块里有多少个文件名不合 N2（含下划线后缀、小写开头）
  3. bridges         `.h` 里还有多少条 `using krkr::…;` 过渡桥接

三个指标都只能**降**。基线在 scripts/naming/baseline.tsv，只有 `--update-baseline`
能改，且改之前会打印每个模块的升降，防止手滑把基线调高。

为什么用"唯一符号数"而不是"出现次数"：出现次数里 `TVPThrowExceptionMessage` 一个
符号就能占几百次，改名的工作量取决于**符号种类**，不是引用次数。

用法：
    python3 scripts/check_naming.py                     # 全模块，只看是否越线
    python3 scripts/check_naming.py --module io -v      # 列出 io 还剩哪些符号
    python3 scripts/check_naming.py --update-baseline   # 降低基线（只允许降）
    python3 scripts/check_naming.py --all-modules       # 连 visual/movie 一起统计

退出码：0=通过  1=有模块比基线差  2=环境/参数问题
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CORE = REPO_ROOT / "cpp" / "core"

# 已按层重组的模块（见 cpp/core/README.md）。visual/movie 只在 --all-modules 里出现：
# 它们的结构未动，先把"调用点会被改"和"结构会被改"分开。
REFACTORED_MODULES = [
    "base",
    "io",
    "sound",
    "environ",
    "plugin",
    "utils",
    "compat",
    "extension",
]
# tjs2 是第三方，永远不统计（会淹没信号）。
EXTRA_MODULES = ["visual", "movie"]

SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".inc"}

# 与契约 §4 对齐的文件级例外：这些文件里的 TVP* 名字不是"待改建"。
EXCEPTION_FILES = {
    # 生成文件：343 个消息符号的产地
    "cpp/core/base/core/MsgIntfInc.h",
    "cpp/core/base/core/MsgImpl.h",
    # MIDL / SDK / 第三方 C
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
    # NCB 宏层（与 plugin/README.md 的宏改造一起动）
    "cpp/core/plugin/ncb/ncbind.hpp",
    "cpp/core/plugin/ncb/ncbind.cpp",
    "cpp/core/plugin/ncb/ncb_invoke.hpp",
    "cpp/core/plugin/ncb/ncb_foreach.inc",
    # TAR 格式结构体（与 POSIX tar.h 同名，改名要同步所有 include，低优先）
    "cpp/core/io/archive/tar.h",
}

# N2：UpperCamelCase，不带下划线后缀。`7zArchive.cpp` 这类数字开头也算不合规。
FILE_NAME_RE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(h|cpp|hpp|c|inc|mm|in)$")

# N3/N4：遗留符号。要求 TVP 后面紧跟大写字母 —— `TVP_MSG_DEFINE` 这类宏不在此列。
LEGACY_SYMBOL_RE = re.compile(r"\b(?:i|t)?TVP[A-Z][A-Za-z0-9_]*\b")

BRIDGE_RE = re.compile(r"^using\s+krkr::[\w:]+;", re.MULTILINE)
MSG_DEFINE_RE = re.compile(r"TVP_MSG_DEFINE\(\s*(\w+)")


def load_message_symbols() -> set[str]:
    """从生成文件里解析出消息符号名（这些永不改名，见契约 §4）。"""
    path = REPO_ROOT / "cpp/core/base/core/MsgIntfInc.h"
    if not path.is_file():
        return set()
    return set(MSG_DEFINE_RE.findall(path.read_text(encoding="utf-8", errors="replace")))


def module_files(module: str) -> list[Path]:
    root = CORE / module
    if not root.is_dir():
        return []
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES)


def scan(module: str, messages: set[str], verbose: bool) -> dict:
    files = module_files(module)
    symbols: dict[str, int] = {}
    bad_files: list[str] = []
    bridges = 0

    for path in files:
        rel = str(path.relative_to(REPO_ROOT)).replace("\\", "/")
        text = path.read_text(encoding="utf-8", errors="replace")

        bridges += len(BRIDGE_RE.findall(text))

        if not FILE_NAME_RE.match(path.name):
            bad_files.append(rel)

        if rel in EXCEPTION_FILES:
            continue
        for sym in LEGACY_SYMBOL_RE.findall(text):
            if sym in messages:
                continue
            symbols[sym] = symbols.get(sym, 0) + 1

    # `TVP2`（版权注释里的引擎名）不是符号
    symbols.pop("TVP2", None)

    if verbose:
        top = sorted(symbols.items(), key=lambda kv: (-kv[1], kv[0]))
        print(f"  ── {module}: {len(symbols)} 个遗留符号")
        for sym, n in top[:40]:
            print(f"     {n:4d}×  {sym}")
        if len(top) > 40:
            print(f"     … 还有 {len(top) - 40} 个")
        for rel in bad_files:
            print(f"     [文件] {rel}")

    return {
        "module": module,
        "legacy_symbols": len(symbols),
        "legacy_files": len(bad_files),
        "bridges": bridges,
        "symbols": symbols,
        "bad_files": bad_files,
    }


def load_baseline() -> dict[str, dict[str, int]]:
    path = REPO_ROOT / "scripts/naming/baseline.tsv"
    if not path.is_file():
        return {}
    out: dict[str, dict[str, int]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        module, sym, fil, br = parts[:4]
        out[module] = {
            "legacy_symbols": int(sym),
            "legacy_files": int(fil),
            "bridges": int(br),
        }
    return out


def write_baseline(results: list[dict]) -> None:
    path = REPO_ROOT / "scripts/naming/baseline.tsv"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# 命名契约存量基线（docs/NAMING.md）。三个数字都只能降。",
        "# module\tlegacy_symbols\tlegacy_files\tbridges",
    ]
    for r in results:
        lines.append(
            f"{r['module']}\t{r['legacy_symbols']}\t{r['legacy_files']}\t{r['bridges']}"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"已写入 {path.relative_to(REPO_ROOT)}（{len(results)} 个模块）")


def main() -> int:
    ap = argparse.ArgumentParser(description="命名契约存量 ratchet")
    ap.add_argument("--module", action="append", help="只看某模块（可重复）")
    ap.add_argument("--all-modules", action="store_true", help="连 visual/movie 一起统计")
    ap.add_argument("-v", "--verbose", action="store_true", help="列出遗留符号与不合规文件")
    ap.add_argument("--update-baseline", action="store_true", help="把当前值写成新基线")
    args = ap.parse_args()

    messages = load_message_symbols()
    modules = REFACTORED_MODULES + (EXTRA_MODULES if args.all_modules else [])
    if args.module:
        unknown = [m for m in args.module if m not in modules]
        if unknown:
            print(f"未知模块: {unknown}（可选: {modules}）", file=sys.stderr)
            return 2
        modules = args.module

    print(f"消息符号例外: {len(messages)} 个（来自 MsgIntfInc.h，永不改名）\n")

    results = [scan(m, messages, args.verbose) for m in modules]
    baseline = load_baseline()

    if args.update_baseline:
        for r in results:
            base = baseline.get(r["module"])
            if base:
                for key in ("legacy_symbols", "legacy_files", "bridges"):
                    if r[key] > base[key]:
                        print(
                            f"拒绝更新：{r['module']}.{key} 从 {base[key]} 升到 {r[key]}。"
                            f"基线只能降（新增遗留名请先改代码，不要调基线）。",
                            file=sys.stderr,
                        )
                        return 1
        write_baseline(results)
        return 0

    header = f"{'模块':<12}{'遗留符号':>9}{'基线':>7}{'文件':>6}{'基线':>6}{'桥接':>7}{'基线':>6}  状态"
    print(header)
    print("-" * (len(header) + 8))

    failed = []
    for r in results:
        base = baseline.get(r["module"])
        cells = [f"{r['module']:<12}"]
        ok = True
        for key, width in (("legacy_symbols", 9), ("legacy_files", 6), ("bridges", 7)):
            cur = r[key]
            b = base[key] if base else None
            cells.append(f"{cur:>{width}}")
            cells.append(f"{b:>{width - 1}}" if b is not None else f"{'-':>{width - 1}}")
            if b is not None and cur > b:
                ok = False
        state = "✓" if ok else "✗ 比基线差"
        if not ok:
            failed.append(r["module"])
        print("".join(cells) + f"  {state}")

    total = sum(r["legacy_symbols"] for r in results)
    print(f"\n合计遗留符号: {total}")

    if failed:
        print(f"\n✗ 这些模块的遗留名比基线多了: {failed}", file=sys.stderr)
        print("  新代码请遵守 docs/NAMING.md；不要用 --update-baseline 掩盖。", file=sys.stderr)
        return 1

    print("✓ 没有模块比基线差。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
