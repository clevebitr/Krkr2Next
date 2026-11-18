#!/usr/bin/env python3
"""从上游 `msg/text/Messages.xlsx` 生成旧渲染层（legacy visual）的消息表。

背景（为什么需要这个工具）：
  krkrz 的 `msg/MsgIntfInc.h` 在仓库里全部是 `TVP_MSG_DECL_NULL(...)`，真正的文案在
  Excel 里，由 `msg/text/gentext.pl` 生成 Win32 字符串资源，运行时靠 `LoadString` 读入。
  Android 没有 Win32 资源，所以这里把 Excel 里的文案**直接生成为 C++ 宽字符串**，
  与 `tools/gen_tjs_message_map.py` 对 TJS 消息做的事是同一个思路。

只读第 2、3 个工作表（`コア共通`、`コアWin32`）：第 1 个是 TJS2 的消息，已由
`tools/gen_tjs_message_map.py` 处理，重复生成会打架。

用法：
  python3 cpp_new/tools/gen_legacy_messages.py --xlsx ~/AppProjects/krkrz/msg/text/Messages.xlsx
  # 可选 --language zh|en|ja（默认 zh，缺失时回退 en → ja → 标识符名）
输出：
  cpp_new/vendor/krkrz/msg/generated/messages.inc
"""
import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"

# 只取这些工作表（名字在不同语言版本里会变，所以按顺序取第 2、3 个）
SHEET_INDICES = (1, 2)  # 0-based：跳过 TJS2

COL_ID, COL_JP, COL_EN, COL_OPT, COL_ZH = 0, 1, 2, 3, 4


def sheet_paths(archive):
    """按 workbook 里的顺序返回工作表路径（与 gentext.pl 的 Worksheets(i) 一致）。"""
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    rels = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
    targets = {
        rel.get("Id"): rel.get("Target")
        for rel in rels
    }
    paths = []
    for sheet in workbook.find(f"{NS}sheets"):
        rid = sheet.get("{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id")
        target = targets[rid].lstrip("/")
        paths.append(target if target.startswith("xl/") else "xl/" + target)
    return paths


def shared_strings(archive):
    """共享字符串表。忽略 <rPh>（日语假名注音），它不是正文。"""
    if "xl/sharedStrings.xml" not in archive.namelist():
        return []
    root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    values = []
    for item in root.findall(f"{NS}si"):
        text = []
        for node in item:
            if node.tag == f"{NS}t":
                text.append(node.text or "")
            elif node.tag == f"{NS}r":
                for part in node.findall(f"{NS}t"):
                    text.append(part.text or "")
        values.append("".join(text))
    return values


def column_index(reference):
    """`C12` → 2"""
    letters = re.match(r"([A-Z]+)", reference).group(1)
    index = 0
    for letter in letters:
        index = index * 26 + (ord(letter) - ord("A") + 1)
    return index - 1


def read_sheet(archive, path, shared):
    rows = []
    root = ET.fromstring(archive.read(path))
    for row in root.iter(f"{NS}row"):
        values = {}
        for cell in row.findall(f"{NS}c"):
            kind = cell.get("t")
            if kind == "s":
                node = cell.find(f"{NS}v")
                value = shared[int(node.text)] if node is not None else ""
            elif kind == "inlineStr":
                node = cell.find(f"{NS}is/{NS}t")
                value = node.text if node is not None else ""
            else:
                node = cell.find(f"{NS}v")
                value = node.text if node is not None else ""
            values[column_index(cell.get("r"))] = (value or "").strip()
        identifier = values.get(COL_ID, "")
        # 表头等非标识符行（例如「定義」）直接跳过，否则会生成非法的 C++ 标识符。
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", identifier):
            rows.append((values.get(COL_ID, ""), values.get(COL_JP, ""), values.get(COL_EN, ""),
                         values.get(COL_OPT, ""), values.get(COL_ZH, "")))
    return rows


def choose_text(pair, language):
    """按偏好挑一种语言；都空时回退到标识符名（不生成空消息）。"""
    identifier, jp, en, opt, zh = pair
    order = {"zh": (zh, en, jp), "en": (en, zh, jp), "ja": (jp, en, zh)}[language]
    for candidate in order:
        if candidate:
            return candidate
    return identifier


def escape_wide(text):
    """转成能在任何编译器/编码下都稳定的宽字符串字面量：非 ASCII 一律 \\uXXXX。"""
    out = []
    for char in text:
        code = ord(char)
        if char == "\\":
            out.append("\\\\")
        elif char == '"':
            out.append('\\"')
        elif 0x20 <= code < 0x7F:
            out.append(char)
        elif code <= 0xFFFF:
            out.append(f"\\u{code:04X}")
        else:
            out.append(f"\\U{code:08X}")
    return "".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xlsx", default=os.path.expanduser("~/AppProjects/krkrz/msg/text/Messages.xlsx"))
    parser.add_argument("--out", default=None)
    parser.add_argument("--language", choices=("zh", "en", "ja"), default="zh")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output = args.out or os.path.join(root, "vendor", "krkrz", "msg", "generated", "messages.inc")
    if not os.path.isfile(args.xlsx):
        print(f"找不到 Messages.xlsx：{args.xlsx}（用 --xlsx 指定）", file=sys.stderr)
        return 2

    with zipfile.ZipFile(args.xlsx) as archive:
        shared = shared_strings(archive)
        paths = sheet_paths(archive)
        entries = []
        for index in SHEET_INDICES:
            rows = read_sheet(archive, paths[index], shared)
            entries.extend(rows)
            print(f"  {paths[index]}: {len(rows)} 条")

    lines = [
        "// 由 cpp_new/tools/gen_legacy_messages.py 从上游 msg/text/Messages.xlsx 生成，不要手改。",
        f"#define LEGACY_MESSAGE_COUNT {len(seen) if False else len(entries)}",
        f"// 语言偏好：{args.language}（缺失回退 en → ja → 标识符名）。",
        f"// 条目数：{len(entries)}（第 2、3 个工作表；第 1 个是 TJS2 消息，由 gen_tjs_message_map.py 负责）。",
        "",
    ]
    seen = set()
    for entry in entries:
        identifier = entry[0]
        if identifier in seen:
            print(f"跳过重复标识符：{identifier}", file=sys.stderr)
            continue
        seen.add(identifier)
        lines.append(f'LEGACY_MESSAGE({identifier}, L"{escape_wide(choose_text(entry, args.language))}")')

    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")

    print(f"写入 {output}：{len(seen)} 条消息（语言 {args.language}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
