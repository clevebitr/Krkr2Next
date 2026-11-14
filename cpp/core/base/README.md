# core/base —— 三层，读目录就知道依赖方向

`base` 曾经是一个 60 文件的目录 + 一个 STATIC target。那让"base 依赖 7 个模块"
这句话把**低层**（消息、流、字符集）和**高层**（启动序列、TJS 类注册表）的依赖
记在了同一个名字上。现在按依赖层次切成三个目录，目录名 = target 名：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `core/` | `core_base_core` | 消息、流、字符集、存储门面、事件**接口** | `tjs2` `utils` |
| `runtime/` | `core_base_runtime` | 线程、定时器、时钟、调试输出、事件分发、文件选择框 | `core` `environ` `visual` `utils` |
| `script/` | `core_base_script` | TJS 脚本引擎、`System` 对象、KAGParser | 上面全部 + `sound` `plugin` `extension` |

原来的第四簇 `sysinit/`（启动序列、平台契约、配置、CPU 检测）已经**整体搬进
`cpp/core/environ/sysinit/`** —— 它读 `Application` / `SystemControl`、要
`GraphicsLoaderIntf` / `LayerIntf`，本来就是启动路径，不是"base 的低层设施"。
放在 base 里只会让"base 依赖 environ"记错账。

`core_base_module` 是一个 **INTERFACE 聚合层**，把三个簇 + `io` 串起来。
其它模块（`io` / `environ` / `visual` / `plugin` / `tools/xp3`）只认这个名字，
所以拆 target 时调用方零改动。

## 一条规矩：接口头在下层，实现在上层

**不是**每个 `.h` 都和它的 `.cpp` 在同一个目录。这是有意的：

```
core/EventIntf.h        ← 50 个文件包含它，只需要 tjs2 类型
runtime/EventIntf.cpp   ← 实现要把事件推给 TJS，得看见 script/ScriptMgnIntf.h
core/FileSelector.h     ← 纯声明（iTVPFileSelector）
runtime/FileSelector.cpp← 实现要开窗口，得看见 visual/WindowImpl.h
```

判断标准只有一条：**这个文件 include 谁**，不是它叫什么。

## 加一个新文件放哪

1. 只 include `tjs2` / `utils` 的头 → `core/`
2. 要 `MsgIntf` / `EventIntf` / `Application.h` / `WindowImpl.h` → `runtime/`
3. 要 TJS 对象、要注册 native class → `script/`
4. 属于启动流程、平台契约、配置、CPU 检测 → **`cpp/core/environ/sysinit/`**，不是这里

如果落到 2/3 说明它本来就不是"底层设施"，别往 `core/` 里塞。

## 已知的环（不在本次重构里解决）

- `script ↔ environ`：`ScriptMgnIntf.cpp` 要 `Application.h` / `SystemControl.h`；
  `environ/sysinit/SysInitIntf.cpp` 又要跑脚本、要 `KAGParser.h`。
- `script → visual/sound/plugin/extension`：`ScriptMgnIntf.cpp` 里 21 个
  `TVPCreateNativeClass_*` 工厂的内联注册表。拆法是 base 留注入口、`environ` 注册
  （见 `cpp/DECISIONS.md` #9）。

静态库允许环，所以这些不会挡住构建；但它们会挡住"只读一个目录就理解它"。
拆的顺序与判据见 `docs/STRUCTURE_DESIGN.md` §3.2。

## 公共头的现状（技术债）

`include` 语句现在写的还是裸文件名（`#include "MsgIntf.h"`），靠三条
`-I base/{core,runtime,script}` 解析 —— 也就是**整个 base 仍然是一张扁平的
公共头面**。门面头 + 显式路径（`#include "base/core/MsgIntf.h"`）是下一步的活，
不在本轮范围内。
