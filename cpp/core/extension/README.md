# core/extension —— 有意不拆层

`extension` 只有两个文件：`Extension.h` + `Extension.cpp`（`tTVPExtension` 的
基类与 `TVPRegisterExtension` 一族），源文件里只有一个 `.cpp`，只 include
`tjsCommHead.h` / `Extension.h` / `tjs.h`。

**所以它不按"目录 = target"拆**：只有一个实现文件时，拆层不会让"谁依赖谁"
更清楚，只会多出两个空壳 target。这与 `cpp/core/README.md` 那条约定不冲突 ——
那条约定的目的是可读性，不是凑目录数。判据很简单：**一个集群里至少要有两个
可独立理解的实现文件**，否则不拆。

以下是已经做过的、和"拆层"无关的收口：

- namespace：`krkr::extension`（2/2 文件）。
- 依赖：只 `PUBLIC tjs2`，没有跨模块依赖，是真正的叶子
  （`STRUCTURE_DESIGN.md` §1.1 的依赖图里它只有一条边）。

将来如果 `extension` 长出第二个实现文件（比如 out-of-process 扩展、或者
某个游戏专属的扩展基类），再按 `sound` / `io` 的做法切 —— 那时的第一步仍然是
**先用 include 图判断层次**，而不是先建目录。
