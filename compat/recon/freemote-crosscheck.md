# FreeMote 对照（**只作格式参考，不引入其代码**）

> 目的：E-mote（M2 PSB）的**容器/字段语义**有一个成熟的开源对照物
> （[UlyssesWu/FreeMote](https://github.com/UlyssesWu/FreeMote)），可以用来
> ① 把"魔法数字"变成有出处的常量 ② 用真实数据检验我们的位表/默认值
> ③ 生成离线对照图。本文记录**取用了什么、放哪、怎么验证**。
>
> 本文配合 `render-issues.md`（open issues）与根目录 `HANDOFF-EMOTE.md` 阅读。

---

## 0. 许可证边界（先看这条）

| 项 | 事实 |
|---|---|
| FreeMote 许可证 | **CC BY-NC-SA 4.0**（署名 — 非商业性使用 — 相同方式共享） |
| 能不能 vendor 进引擎 | **不能**（NC + SA 都与本仓库不兼容） |
| 能不能复制其代码/注释 | **不能** |
| 我们取用什么 | **格式事实**：位号、键名、默认值、枚举取值（事实不受版权保护），由我们自己书写 |
| 参考副本 | `temp/freemote/`（`temp/` 已被 `.gitignore` 覆盖，**不要提交**） |
| 版本锚点 | `9773f50`（2026-09-19，master） |
| 落地形态 | `cpp/plugins/psbfile/EmoteMaskBits.h`（常量 + 开关 + 统计结构体） |

引用其结论时的写法：**只写结论 + 注明出处**（`FreeMote/Motion/MmoEnums.cs:45-147`），
不要摘抄实现代码。审查这份改动时的检查项：`git diff` 里是否出现 FreeMote 的代码。

---

## 1. 两张位表（与我们的口径逐位对账）

### 1.1 `content["mask"]` —— 帧内容位域

语义：位=1 ⇒ 本帧**真正携带**该键；位=0 ⇒ 该键等同于**不存在**（不是"等于 0"）。
出处：① AetherKiri `PlayerInternal.h:1921-2035`（本项目既有口径）；
② FreeMote `FreeMote/Motion/MmoEnums.cs`（`MmoFrameMask`）。

| 位 | 值 | 键 | 本项目状态 |
|---|---|---|---|
| 0 | 0x1 | ox/oy | 已实现（krkr 里该位语义 = "是否使用 ox/oy"） |
| 1 | 0x2 | coord[0..2] | 已实现（注意 `coord[2]` 是 **Z**，不是缩放） |
| 2/3 | 0x4/0x8 | fx/fy | 已实现 |
| 4 | 0x10 | angle | 已实现 |
| 5/6 | 0x20/0x40 | zx/zy | 已实现 |
| 7/8 | 0x80/0x100 | sx/sy | 已实现 |
| 9 | **0x200** | **color** | **未实现**（本轮只统计） |
| 10 | 0x400 | opa（旧数据 `op`） | 已实现 |
| 11..15 | 0x800..0x8000 | ccc/acc/zcc/scc/occ | 曲线解析已实现，但**未按位门控**（本轮只统计） |
| 16 | 0x10000 | ？（两份来源都未说明，FreeMote 亦标 `???`） | 只统计 |
| 17 | 0x20000 | bm | 已实现；**默认值存疑**（参考取 0，FreeMote 默认表写 16） |
| 19 | 0x80000 | motion（子运动引用） | 未实现（子运动走 `src="motion/…"` 那条路） |
| 20 | 0x100000 | prt（粒子） | 未实现 |
| 25 | 0x2000000 | mesh（变形数据） | 未实现 |

### 1.2 `layer["inheritMask"]` —— 变换继承门控（位=1 表示继承父级该项）

出处：FreeMote `FreeMote.PsBuild/MmoBuilder.cs`（把 inheritMask 展开成具名
`inheritXxx`）。**交叉验证**：本项目沿用的默认值 `0x1FC` = bits 2..8 = 全部仿射项，
与该表完全吻合。

| 位 | 值 | 含义 | | 位 | 值 | 含义 |
|---|---|---|---|---|---|---|
| 2 | 0x4 | flipX | | 9 | 0x200 | colorWeight |
| 3 | 0x8 | flipY | | 10 | 0x400 | opacity |
| 4 | 0x10 | angle | | 22 | 0x400000 | parent |
| 5 | 0x20 | zoomX | | 25 | 0x2000000 | shape |
| 6 | 0x40 | zoomY | | | | |
| 7 | 0x80 | slantX | | | | |
| 8 | 0x100 | slantY | | | | |

### 1.3 `stencilType` 是**枚举**，不是位标志

FreeMote 的 PSB→MMO 转换明确写着 **5(psb) → 1(mmo)**（`MmoBuilder.cs`），
即 PSB 侧比 MMO 侧多一个常量偏移。所以 `type==12 && (stencilType & 4)` 这个判据
很可能**恒真**、没有区分度；"是否带作者声明的蒙版层列表"
（`layer["stencilCompositeMaskLayerList"]` → `PSBMotionNode::stencilMaskLabels`）
更有区分度。本轮**只统计两者的数量对比**，不改判据。

---

## 2. 本轮落地的代码（只统计，不改渲染行为）

| 文件 | 改动 |
|---|---|
| `cpp/plugins/psbfile/EmoteMaskBits.h` | **新增**。ContentMask / InheritMask / Stencil / Defaults 常量 + 三个 A/B 开关（默认全关）+ `MaskStats`（含 `SeenBits()` 等格式化） |
| `cpp/plugins/psbfile/PSBMedia.cpp` | `ApplyFrameContentMask()` 改用常量（行为逐字节不变）；新增 `countStats` 口径与两个方向的自洽性统计；`inheritMask`/`stencilType` 直方图；sub-motion 清点；每个 archive 打 3 行汇总 |
| `cpp/plugins/psbfile/CMakeLists.txt` | 把新头文件加进 `target_sources`（IDE 可见） |

**三个开关（`EmoteMaskBits.h::Switches`，默认全关 = 本轮不改变任何行为）**：

| 开关 | 翻成 true 的语义 | 先看哪条日志再决定 |
|---|---|---|
| `kGateColorByMask` | 位 9 为 0 时把顶点色复位为 `0xFF808080` | `unhandled=color(n)`、`colorKeyBit=[set / clear]` |
| `kGateCurvesByMask` | 位 11..15 为 0 时清掉对应通道曲线 | `resetTriggered=ccc/acc/zcc/scc/occ(n)` |
| `kBlendModeDefault16` | 位 17 为 0 时 `bm=16`（而不是 0） | `unhandled=bm(n)` + 真机 A/B 截图 |

---

## 3. 真机怎么读新日志（每个 archive 各 3 行）

```
emote mask stats [<archive>]: frames=<N> withMask=<N> bits=<位名(次数),…> resetTriggered=<…> tableMismatch=<…> unhandled=<…>
emote layer stats [<archive>]: inheritMask=<名(次数),…> stencilType=<值(次数),…> labels:<有声明>/<总数> colorKeyBit=[set <N> / clear <N>]
emote sub-motions [<archive>]: total=<N> emptyLayer=<N> emptyWithParam=<N> names=[scene/motion,…]
```

判读表：

| 观察 | 结论 / 动作 |
|---|---|
| `unhandled` 里出现 `color(n)` | 位 9 在真实数据里确实被用 ⇒ 值得实现；先翻 `kGateColorByMask` 跑一轮 A/B |
| `unhandled` 里出现 `bm(n)` | 先 A/B `kBlendModeDefault16`（0 vs 16） |
| `resetTriggered` 里出现 `color` / `ccc/acc/zcc/scc/occ` | 我们**确实在读作者声明"本帧没有"的值** ⇒ 翻 `kGateCurvesByMask` |
| `tableMismatch` 非 `-` | 位表或解析口径有误 ⇒ **先查这里**，别急着改渲染 |
| `withMask < frames` | 该 archive 有帧没有 `mask` 键（非 krkr spec / 旧数据）⇒ 复位口径要按"缺键"处理 |
| `bits` 里从不出现 `mesh`/`prt` | 该游戏不用这些特性 ⇒ 不必投入 |
| `stencilType` 只有**一个**含 bit2 的值 | `(stencilType & 4)` 无区分度 ⇒ 改用 `stencilMaskLabels` 判据 |
| `emptyLayer≈emptyWithParam` 且 names 命中 §5 那 15 条 | `layer: []` 是**数据本身**（参数轴时间轴），不是解析漏 key ⇒ §5 该条可结案 |
| `inheritMask` 里出现 `parent`/`shape`/`opacity`/`colorWeight` | 我们目前只门控仿射位 ⇒ 这三项可能还需要接进 Player.h |

---

## 4. 离线对照（Windows 侧运行 FreeMote，不进引擎）

用途：给"应然画面"和"全树 JSON"做独立的第三方口径，用于本地化几何/次序 bug
（例如 `HANDOFF-EMOTE.md §3.1` 的 Z 排序验收）。

```powershell
# 0) 下载 FreeMote Toolkit Release（含 PsbDecompile / EmtConvert / FreeMoteViewer）
# 1) 解出纯 PSB（加密/被 shell 包裹时需要 key，命令以各自 --help 为准）
PsbDecompile.exe -k <KEY> model.psb          # 输出 json + 资源，适合与我们的 node[i]/group[i] dump 对拍
EmtConvert.exe print -w 4096 -h 4096 model.psb   # 渲染成图（其自带说明写明 "not accurate"，只当粗对照）
FreeMoteViewer.exe                            # 交互式查看（底层是 FreeMote.NET）
```

⚠️ 其 `print`/Viewer 是**静态渲染器**（每帧取一帧，不插值、无 stencil 合成运行时），
只能对"部件落点/次序/尺寸"，不能对"动画语义"。

---

## 5. FreeMote 给不了的（别重复投入）

- **播放器语义**：没有 motionplayer；没有 stencil 蒙版合成运行时；不插值 `Tween/Continuous`。
- **商业加密 PSB 的 key**：需要自己提供（`FreeMote.Purify` 未发布）。
- **FreeMote.NET / WebGL 运行时**：emscripten/原生二进制 + 同一套 NC-SA 许可证 ⇒ 不可 vendor。
