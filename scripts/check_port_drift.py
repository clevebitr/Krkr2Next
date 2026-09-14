#!/usr/bin/env python3
"""校验"与 AetherKiri 共享的移植文件"没有发生未经承认的漂移。

背景见 compat/upstream/aetherkiri_ports.json 的 _comment。简述：本仓库有大量
插件是从 AetherKiri 移植来的，必须能回答"这文件相对上游改了什么"。本脚本是
那份清单的执行者。

两类失败：

  1. 本地漂移（硬失败）
     清单记录的文件被改动，但 modifications 字段没更新。
     → 用 --update 重新计算哈希，并把 modifications 改成实际改动类型。

  2. 上游移动（提示，不失败）
     本地存在 upstream 检出时，发现上游该文件的内容已与清单记录的基线不同。
     → 说明上游改了它，需要人工判断要不要跟进。上游持续演进是常态，
       不应让 CI 因为上游提交而变红。

用法：
    python3 scripts/check_port_drift.py            # 校验
    python3 scripts/check_port_drift.py --update   # 重新计算哈希并写回
    python3 scripts/check_port_drift.py -v         # 显示每个文件的比对结果
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST = REPO_ROOT / "compat/upstream/aetherkiri_ports.json"


def sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def short(digest: str | None) -> str:
    return digest[:16] if digest else "—"


def upstream_root(manifest: dict) -> Path | None:
    """返回 upstream 检出根目录；不存在或 rev 不符则返回 None。"""
    rel = manifest["upstream"].get("local_checkout")
    if not rel:
        return None
    root = (REPO_ROOT / rel).resolve()
    if not (root / ".git").exists():
        return None
    want = manifest["upstream"]["rev"]
    try:
        got = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    if got != want:
        print(f"注意：upstream 检出在 {got[:7]}，清单记录的是 {want[:7]}。")
        print("      上游比对结果可能不准；要精确比对请先 checkout 到清单的 rev。")
        print()
        # 仍然继续——内容哈希比对本身不依赖 rev，只是基线可能对不上
    return root


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update", action="store_true",
                    help="重新计算并写回哈希（改动移植文件后必须跑一次）")
    ap.add_argument("-v", "--verbose", action="store_true", help="逐文件输出")
    args = ap.parse_args()

    if not MANIFEST.is_file():
        print(f"错误：找不到清单 {MANIFEST}", file=sys.stderr)
        return 1

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    up_root = upstream_root(manifest)

    entries = manifest.get("ported", [])
    if not entries:
        print("清单里没有任何 ported 条目——无需校验。")
        return 0

    local_drift: list[tuple[str, str, str]] = []   # (local, 记录值, 实际值)
    uninitialized: list[str] = []                  # 清单里有条目但没记录哈希
    missing: list[str] = []
    upstream_moved: list[tuple[str, str, str]] = []
    changed = False

    for e in entries:
        local_rel = e["local"]
        local_path = REPO_ROOT / local_rel
        actual_local = sha256(local_path)

        if actual_local is None:
            missing.append(local_rel)
            continue

        recorded_local = e.get("sha256_local")
        if recorded_local is None:
            # 没有基线哈希就无法判断漂移。静默通过等于这条保护不存在。
            uninitialized.append(local_rel)
            e["sha256_local"] = actual_local
            changed = True
        elif recorded_local != actual_local:
            if not args.update:
                local_drift.append((local_rel, short(recorded_local), short(actual_local)))
            e["sha256_local"] = actual_local
            changed = True
            if args.verbose:
                print(f"  本地已更新哈希：{local_rel}")

        # 上游比对
        if up_root is not None:
            up_rel = e["upstream"]
            actual_up = sha256(up_root / up_rel)
            if actual_up is None:
                if args.verbose:
                    print(f"  上游文件不存在，跳过：{up_rel}")
            else:
                recorded_up = e.get("sha256_upstream_at_rev")
                if recorded_up != actual_up:
                    if recorded_up is not None:
                        upstream_moved.append((up_rel, short(recorded_up), short(actual_up)))
                    e["sha256_upstream_at_rev"] = actual_up
                    changed = True

    # 上游移动只是提示，不构成失败
    if upstream_moved:
        print("上游已改动以下文件（人工判断是否跟进，不是错误）：")
        for rel, was, now in upstream_moved:
            print(f"    {rel}")
            print(f"        基线 {was} → 上游现在 {now}")
        print()

    if args.update:
        MANIFEST.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"清单已更新：{MANIFEST.relative_to(REPO_ROOT)}")
        if local_drift:
            print("以下文件的本地哈希已刷新——请确认 modifications 字段反映了实际改动类型：")
            for rel, _, _ in local_drift:
                print(f"    {rel}")
        return 0

    fail = False

    if uninitialized:
        fail = True
        print("清单条目缺少基线哈希，无法判断漂移：")
        for rel in uninitialized:
            print(f"    {rel}")
        print()
        print("    运行 python3 scripts/check_port_drift.py --update 建立基线。")
        print()

    if missing:
        fail = True
        print("清单记录了但这些文件不存在：")
        for rel in missing:
            print(f"    {rel}")
        print("    → 若确实删除了，请把它移到 not_ported 并说明原因。")
        print()

    if local_drift:
        fail = True
        print("本地已改动，但清单未反映（未经承认的漂移）：")
        for rel, was, now in local_drift:
            print(f"    {rel}  记录 {was} → 实际 {now}")
        print()
        print("    这是刻意设计的硬失败：改了移植文件就必须显式承认。")
        print("    请运行 python3 scripts/check_port_drift.py --update，")
        print("    并把该条目的 modifications 改为 none|api-shim|bridge-substituted|local-fix。")
        print()

    if fail:
        return 1

    print(f"移植清单校验通过（{len(entries)} 个文件，无未经承认的漂移）。")
    if up_root is None:
        print("（本地无 upstream 检出，仅校验了本地哈希；上游比对已跳过。）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
