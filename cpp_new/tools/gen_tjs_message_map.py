#!/usr/bin/env python3
"""生成 TJS2 内置消息表的垫层副本。

背景：`cpp_new/tjs/tjsErrorInc.h` 由上游 gentext 生成，默认消息全部为 NULL，
因此运行时抛出的异常消息恒为空串（技术债 #40）。上游提供
`TJSAssignMessage(name, text)` 用于在运行期填充消息表，本脚本把消息文本从上游
参照实现里取出来，生成垫层可编译的字面量表，`tjs/` 保持逐字节不变。

用法：
    python cpp_new/tools/gen_tjs_message_map.py [上游 tjsError.h] [本树 tjsErrorInc.h] [输出]

默认参数即本仓库的路径；输出文件已提交，仅在需要重新生成时手工运行。
"""
import os
import re
import sys

DEFAULT_SOURCE = os.path.join('cpp', 'core', 'tjs2', 'tjsError.h')
DEFAULT_FILTER = os.path.join('cpp_new', 'tjs', 'tjsErrorInc.h')
DEFAULT_OUTPUT = os.path.join('cpp_new', 'shim', 'tjs_host', 'generated', 'tjsMessageMap.inc')

DECL = 'TJS_MSG_DECL('


def parse_declarations(text):
    """返回 [(name, text)]；text 已按 C++ 相邻字面量规则拼接。"""
    entries = []
    index = 0
    while True:
        start = text.find(DECL, index)
        if start < 0:
            break
        line_start = text.rfind('\n', 0, start) + 1
        if text[line_start:start].lstrip().startswith('#'):
            index = start + len(DECL)
            continue

        cursor = start + len(DECL)
        comma = text.index(',', cursor)
        name = text[cursor:comma].strip()

        depth = 1
        position = comma + 1
        chunks = []
        while depth:
            char = text[position]
            if char == '"':
                position += 1
                literal = []
                while True:
                    current = text[position]
                    if current == '\\':
                        literal.append(text[position:position + 2])
                        position += 2
                        continue
                    if current == '"':
                        position += 1
                        break
                    literal.append(current)
                    position += 1
                chunks.append(''.join(literal))
                continue
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
                if depth == 0:
                    break
            position += 1
        entries.append((name, ''.join(chunks)))
        index = position + 1
    return entries


def known_names(path):
    with open(path, encoding='utf-8') as handle:
        text = handle.read()
    return set(re.findall(r'TJS_MSG_DECL_NULL\(\s*(\w+)\s*\)', text))


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SOURCE
    filter_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_FILTER
    output = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUTPUT

    with open(source, encoding='utf-8') as handle:
        entries = parse_declarations(handle.read())
    available = known_names(filter_path)

    kept = [(name, text) for name, text in entries if name in available]
    skipped = [name for name, _ in entries if name not in available]

    lines = [
        '// 由 tools/gen_tjs_message_map.py 生成，不要手工修改。',
        '// 名称为 TJS2 内置消息键；文本取自上游参照实现。',
    ]
    for name, text in kept:
        lines.append('{ TJS_W("%s"), TJS_W("%s") },' % (name, text))

    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('\n'.join(lines) + '\n')

    print('generated %s: %d entries' % (output, len(kept)))
    if skipped:
        print('skipped (absent from vendored tjsErrorInc.h): %s' % ', '.join(skipped))


if __name__ == '__main__':
    main()
