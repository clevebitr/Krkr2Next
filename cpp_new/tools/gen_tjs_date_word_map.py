#!/usr/bin/env python3
"""生成 tjsDateWordMap.cc（TJS 日期词表的前缀树）。

为什么需要这个脚本：
  krkrz 仓库**不提交**这个文件，它由上游 tjs2/script/create_world_map.py 在构建时生成；
  而上游脚本产出的是新版 bison（3.6+）的 `parser::token_kind_type::DP_TZ` 写法，
  我们 vendored 的是 krkrz 提交的旧版 tjsdate.tab.hpp（裸 `DP_TZ` 枚举）。

  为了**不改权威实现**：产物落在 cpp_new/core/script/generated/，
  通过 #include 搜索路径覆盖（vendored 目录里没有这个文件，编译器会往后找），
  vendored 的 tjs2 源码保持与上游逐字节一致。

用法：
  python cpp_new/tools/gen_tjs_date_word_map.py \
      cpp_new/tools/dp_word_table.txt \
      cpp_new/core/script/generated/tjsDateWordMap.cc
"""
import sys

INDENT = '\t'

def parse_table(path):
    entries = []
    with open(path, encoding='utf-8') as handle:
        for line in handle:
            line = line.rstrip('\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            fields = line.split()
            if len(fields) < 3:
                continue
            word, value, token = fields[0], fields[1], fields[2]
            token = token.rsplit('::', 1)[-1]  # parser::token_kind_type::DP_TZ -> DP_TZ
            entries.append((word, int(value), token))
    return entries

class Emitter:
    def __init__(self, entries):
        self.entries = entries
        self.lines = []
        self.depth = 0

    def out(self, text=''):
        self.lines.append(INDENT * self.depth + text)

    def walk(self, items, depth):
        """items 是共享前 depth 个字符前缀的一组词。"""
        by_char = {}
        terminal = None
        for word, value, token in items:
            if len(word) == depth:
                terminal = (word, value, token)  # 这个前缀本身就是一个词
                continue
            by_char.setdefault(word[depth], []).append((word, value, token))

        # 先判"到这里已经是一个完整的词"，再继续看更长的词
        if terminal is not None:
            word, value, token = terminal
            self.out(f'if(!TJS_iswalpha(InputPointer[{len(word)}])) {{')
            self.depth += 1
            self.out(f'InputPointer += {len(word)}; yylex->val = {value}; return {token};')
            self.depth -= 1
            self.out('}')

        if not by_char:
            return

        self.out(f'switch(InputPointer[{depth}]) {{')
        self.depth += 1
        for char in sorted(by_char):
            self.out(f"case TJS_W('{char}'):")
            # 小写和它的大写如果是同一个字符（数字/符号），不能重复写 case
            if char.lower() != char.upper():
                self.out(f"case TJS_W('{char.upper()}'):")
            self.out('{')
            self.depth += 1
            self.walk(by_char[char], depth + 1)
            self.out('break;')
            self.depth -= 1
            self.out('}')
        self.depth -= 1
        self.out('}')

    def emit(self, out_path):
        self.out(f'// 由 cpp_new/tools/gen_tjs_date_word_map.py 自动生成，不要手改。')
        self.out(f'// 词表来源：tools/dp_word_table.txt（{len(self.entries)} 条）')
        self.out('')
        self.walk(self.entries, 0)
        with open(out_path, 'w', encoding='utf-8', newline='\n') as handle:
            handle.write('\n'.join(self.lines) + '\n')

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    entries = parse_table(sys.argv[1])
    Emitter(entries).emit(sys.argv[2])
    print(f'generated {sys.argv[2]}: {len(entries)} entries')
    return 0

if __name__ == '__main__':
    sys.exit(main())
