#!/usr/bin/env python3
"""校验 cpp_new 的层间依赖方向（见 docs/ARCHITECTURE.md 第 3 节）。

规则：
  core     不得引用 shim/ platform/ tjs/ engine/
  platform 不得引用 shim/ tjs/ engine/
  shim     不得引用 platform/ engine/（通过 core/contracts 取得能力）
  tjs      不得引用任何 cpp_new 内其他层
  engine   装配根：可引用 shim/ tjs/ core/ platform/，不得引用 host/
  host     测试台：不受限

用法：python cpp_new/tools/check_layer_deps.py [cpp_new 根目录]
退出码：0 通过，1 存在违规。
"""
import os
import re
import sys

FORBIDDEN = {
    'core': ('shim/', 'platform/', 'tjs/', 'engine/', 'host/'),
    'platform': ('shim/', 'tjs/', 'engine/', 'host/'),
    'shim': ('platform/', 'engine/', 'host/'),
    'tjs': ('shim/', 'platform/', 'core/', 'engine/', 'host/'),
    'engine': ('host/',),
    'host': (),
}

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def layer_of(path, root):
    relative = os.path.relpath(path, root)
    first = relative.split(os.sep)[0]
    return first if first in FORBIDDEN else None


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..')
    root = os.path.abspath(root)

    violations = []
    for directory, _, files in os.walk(root):
        for name in files:
            if not name.endswith(('.h', '.hpp', '.cpp', '.cc', '.c')):
                continue
            path = os.path.join(directory, name)
            layer = layer_of(path, root)
            if layer is None:
                continue
            with open(path, encoding='utf-8', errors='surrogateescape') as handle:
                for number, line in enumerate(handle, 1):
                    match = INCLUDE.match(line)
                    if not match:
                        continue
                    target = match.group(1)
                    for prefix in FORBIDDEN[layer]:
                        if target.startswith(prefix):
                            violations.append((path, number, target, layer, prefix))

    for path, number, target, layer, prefix in violations:
        print(f'{os.path.relpath(path, root)}:{number}: include "{target}" '
              f'违反分层（{layer} 不得引用 {prefix.rstrip("/")}）')
    print(f'检查完成：{len(violations)} 处违规')
    return 0 if not violations else 1


if __name__ == '__main__':
    sys.exit(main())
