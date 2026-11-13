# 交接：NEKOPARA 4 E-mote 立绘（2026-09-27 会话）

> 本文是**单一议题**的交接：E-mote（PSB）立绘渲染。
> 本文配合根目录 `HANDOFF.md`、`compat/recon/render-issues.md` 一起读。
> 所有断言都给了"证据出处"，**凡标注 ❌ 的都是已被日志/截图推翻的假设，请勿重复**。

---

## 0. 一句话现状

- **引擎终于能编译**（此前工作区有一个语法错误，导致所有"改动无效"），**Live2D/Cubism 也第一次被编进包**。
- NEKOPARA 4 立绘仍有**两个**可见缺陷：
  1. **脸被"发色平涂"盖住** ← 唯一实质渲染缺陷，已定性为**绘制次序**；
  2. **FPS 2–3** ← PSB 图像缓存按条数爆表（`PSB 672/768`）。
- 下一轮要做的事只有一件、且已定案：见 **§3**（`drawAnimatedTree` 两趟化 + 按累积 Z 稳定排序）。

---

## 1. 本轮已落地的改动（全部在工作区，**未提交**）

| 文件 | 改动 | 依据 / 证据 |
|---|---|---|
| `cpp/plugins/psbfile/PSBMedia.cpp` | **补回丢失的 `{`**（HEAD 里包住 `zx`/`zy` 的裸作用域块左括号被一次"注释清理"删掉了）。**这是"改了代码画面没变化"的总根因：整个引擎编译不过，装上去的一直是旧 `.so`** | 编译报错一串 + `app/app/src/main/jniLibs/.../libengine_api.so` mtime 长期停更 |
| `scripts/restore_cubism_sdk.sh`（无改动，仅执行） | `CUBISM_SDK_URL=file:///home/clevebitr/CubismSdkForNative-5-r.5.zip bash scripts/restore_cubism_sdk.sh` 把 SDK 落到 `cpp/plugins/cubism/{Framework,Core}`（含 `patch_cubism_sdk.py` 的 `SetDrawableForceHidden`）。之后 configure 打印 `-- Live2D: using …/arm64-v8a/libLive2DCubismCore.a` | 修前 `~/krkr2next-build/logs/auto.log:2140`：`-- Live2D: Android arm64-v8a 下没找到 libLive2DCubismCore.a，跳过 Live2D（krkrlive2d.cpp 不参与编译）`；修前已装 `.so` 里 `CubismUserModel` 字符串计数 = 0 |
| `cpp/plugins/psbfile/PSBMedia.h` / `.cpp` | `PSBMotionFrame.mask` + `ApplyFrameContentMask()`：按帧 `content["mask"]` 位域，把**未置位**的字段复位回默认值（等价于参考的"只在位时读该键"） | 参考 `temp/aether_main/cpp/plugins/motionplayer/PlayerInternal.h:1874-1885`（`mergeFrameContent`，`value_or(0)` 缺键=什么都不读）与 `:1921-2035`（完整位表）。真机效果：`輪郭_le` 的 `f0scale` **30/30 → 1/1** |
| `cpp/plugins/psbfile/PSBMedia.h` / `.cpp` | `PSBMotionFrame.z`（来自 **`coord[2]`**），**不再当作缩放兜底**；删掉 `scaleFromCoordZ` 的赋值链 | 参考 `PlayerInternal.h:1929-1931`：`mask & 0x2` 的 `coord[x,y,z]` 写进 `state.z`；`z/scaleX/scaleY` 是三个独立字段 |
| `cpp/plugins/motionplayer/Player.h` | **`_le`"布局单位"hack 整体关闭**（`constexpr bool kEmoteLayoutUnitHack = false;`，一行可翻回） | 最新版 AetherKiri 全仓库 `grep -rn '_le\|layoutUnit\|wunit'` = **0 命中**；参考的缩放只做乘法累加（`PlayerUpdateLayers.cpp:1027-1028` / `:1258-1262`，仅受 `inheritMask` 0x020/0x040 门控） |
| `cpp/plugins/motionplayer/Player.h` | stencil **逐组折叠**：新增 `stencilRootOf`（最外层可折叠合成祖先）+ `flushStencilGroup()` lambda；不可折叠的 type-12 子树改为直画 `dest`（否则会画进组层却永远没人折而丢失） | 旧代码 `>=2 组就整个跳过折叠`，真机 `stencil: 6 composite groups; skipping stencil fold` ×5675/3min |
| `cpp/plugins/motionplayer/Player.h` | 蒙版引用计数 `maskRemaining`（折叠不再每折一组就清 `ml`） | 见 §4-#2（**该假设已被推翻**，代码保留但无效果） |
| `cpp/plugins/motionplayer/Player.h` | `expandSubMotionNodes` 里**同步平移 `stencilMaskNodeIndices`**（原先只平移了 `parentIndex`） | 修前 `pass mlNodesDrawn=0` → 修后 `3`（**有可观测变化，但画面未变**，见 §4-#3） |
| `cpp/plugins/motionplayer/Player.h` | 逐组/每轮诊断日志（见 §2） | 靠它才把 stencil 线结案 |
| `cpp/plugins/motionplayer/main.cpp` | owner 的 `hitThreshold` **0 → 256**（`hitThreshold=0` 让无位图层走 bounds-only 命中，整块角色层吞掉点击） | 真机"点哪都不推进对话" |
| `cpp/plugins/motionplayer/main.cpp` | 连续事件钩子**限频重挂（≥1s）**；`AutoDriveUnregister` 在表清空时**不再摘钩子** | 旧逻辑 `!g_autoDriveHooked \|\| added` 在"每帧注册→摘除"节奏下退化成每帧一次 `TVPRemove/AddContinuousEventHook`（真机 3 分钟重挂 5665 次、摘除 3828 次）；事件表压缩正好发生在 `.stall` 记的 `TVPDeliverAllEvents` 里 |
| `install.sh` | 选包改为**按 mtime 取新的那个**（原来固定"F: 侧存在就用 F:"） | 真机踩到：在 WSL 里 `./build.sh` 完再 `./install.sh`，把 **F: 上 09:33 的 Windows 旧包**装回去验证 10:02 的引擎修复，白跑好几轮 |
| 环境（非仓库） | WSL `~/.android/debug.keystore` 换成与 Windows/Android Studio **同一把**（旧份备份为 `debug.keystore.wslbak`） | 两边 key 不同 → 轮流装必然 `INSTALL_FAILED_UPDATE_INCOMPATIBLE` → 卸载 → **清 app 数据**（本轮被清过数次，用户需重新"添加游戏"） |
| 工作区卫生 | 我早期用 edit 工具把 3 个文件的行尾翻成 CRLF，已统一回 LF（`git diff --numstat` 现为小数字：`Player.h 322/86`、`main.cpp 32/8`、`PSBMedia.cpp 91/315`、`PSBMedia.h 21/0`） | |
| 日志归档 | 抓下来的真机日志放在 `temp/devlogs/`（`temp/` 已在 `.gitignore`） | |

**注意**：`cpp/plugins/cubism/Core/include/Live2DCubismCore.h` 会显示为 modified —— `restore_cubism_sdk.sh` 故意用与静态库同版本的 SDK 头覆盖仓库里那份旧头，**脚本注释明确写了不要 commit 回去**。

---

## 2. 已内置的诊断（下一轮直接用）

探针构建：`cmake -DENABLE_RENDER_PROBE=ON out/android/debug`（默认 OFF）。

| 日志 | 位置 | 说明 |
|---|---|---|
| `stencil: pass folded=… emptyMask=… labels=[…] groups=…` | Player.h，**非探针构建也打**（1/600 轮） | 本轮折叠了几组、哪几组蒙版是空的 |
| `stencil: pass mlNodesDrawn=… maskPendingRefsEnd=…` | 同上 | **本轮画进蒙版层的节点总数**（`=0` ⇒ 蒙版层整轮为空） |
| `stencil: group[i] '<label>' type= root= folded= maskDrawn= glNodes= subtree= maskIdx=[idx->rootN]` | 同上 | **把"组不折叠"拆成互斥原因**：`subtree=0`⇒`stencilRootOf` 算错；`subtree>0 且 glNodes=0`⇒子树节点被跳过（可见性），**不是 stencil 问题**；`maskIdx=[n->root-1]`⇒蒙版是组的**前一个兄弟**、不在组子树内 |
| `probe: item motion='…' label='…' src='…' dst= idx= parent='…' world=(x,y) coord=(x,y) srcSize=WxH m=[a,b,c,d] tx= ty= gameScale= boxScale= stencilGroup= opa= bm= nodeType= stencilType= frameScale= worldScale=` | Player.h（探针，每 key 前 64 条） | 部件级落点/矩阵/尺寸 |
| `probe: item meta … tex=WxH meta=WxH ic=1 anchor=(x,y) unit= mA=` | 同上 | **anchor 是图标原点**（已确认 = 纹理中心，见 §4-#7） |
| `probe: item chain … os= acc= un= inh=` | 同上 | 祖先链自身/累积缩放与 `inheritMask` |
| `node[i] '<label>' parent= type= stencil= box= inh= to= frames= firstsrc= f0scale= f0zscale= f0mask=0x…` | `PSBMedia.cpp` 解析期 | 节点树 + 首帧 scale/mask |
| `stencil group[i] '<label>' type= labels=[蒙版层名…] resolved=N` | `PSBMedia.cpp` 解析期 | 蒙版名解析结果 |

真机日志：`/sdcard/Android/media/org.dpdns.clevebitr/logs/games/<游戏>-<hash>/engine-<时间戳>.log`（+ `.stall` 转储）；壳侧 logcat 落盘在 `logs/logcat.log`。

---

## 3. 下一轮要做的（唯一一件，已定案）

### 3.1 `drawAnimatedTree` 两趟化 + 按累积 Z 稳定排序

**参考实现出处**（`temp/aether_main/` = AetherKiri `origin/main` @ `803078d`）：

```cpp
// cpp/plugins/motionplayer/PlayerUpdateLayers.cpp
entry.sortKey = node.accumulated.posZ;                              // :4690
std::sort(items, [](lhs,rhs){ return lhs.sortKey < rhs.sortKey; }); // :5774
```

**参考在绘制前把所有渲染项按累积 Z 排序；我们是纯节点先序、并且此前把 `coord[2]`（=Z）当成了缩放，等于把 Z 信息丢掉了。**

**做法**：
1. 第一趟：把现有循环里"算完立刻画"的那段摘出去，只算累积变换/活跃帧。**这些值本来就已经存进 `wx[i]/wy[i]/wm11[i]…` 这些向量了**，所以第一趟基本是"把 draw 调用点从循环里挪出来"。
2. 累积 `posZ`：规则与 `px/py` 同一条父链（`posZ[i] = 父累加 + 自身 f.z`）。
3. 第二趟：按 `posZ` **稳定排序**（`std::stable_sort`）后统一绘制。
4. 加 `constexpr bool kEmoteZSort = true;` 之类的开关，**便于一键回退**。
5. 顺手在 `probe: item` 里加一列 `posZ`，方便用日志验证排序结果。

**预期**：后发 `posZ` < 脸 `posZ` → 排在前面先画 → **脸露出来**。

**风险**：`drawAnimatedTree` 是 1000+ 行、单函数、带大量 lambda/早退；中段拆两遍属于结构性改动。**建议用"延迟绘制队列"这种最小侵入方式**（把 draw 调用点替换成把参数推进 `std::vector<DrawItem{posZ, …}>`，循环结束后排序再统一执行），而不是把整个函数重写。

### 3.2（3.1 之后）另外两笔账

- **FPS 2–3**：`PSB 672/768 (hit 4.2万 / miss 19.7万)`、`PSB 117 MiB` ⇒ **卡在条数上限 768，不是字节上限 192 MiB**。
  - 入口：`PSBMedia::_configuredMaxEntryCount`（默认 2048；真机上被 `adaptBudgetByMemoryPressureLocked()` 压到 768）。
  - 方向：大分件单独计权（按字节折算条数）或直接提高条数上限；也可对"当前立绘的 archive"做 pin。
- **`.stall` 1.5–1.9s**，阶段恒为 `渲染线程 → TVPDeliverAllEvents（执行排队事件）`。属老账（`HANDOFF.md` §1.13 / `render-issues.md` §1 未解决③），需要在 `DeliverEvents()` / `TickBeat()` 内加细阶段探针。

---

## 4. 已排除的假设（⚠️ **不要重复**）

1. ❌ **"stencil 蒙版没解析出来"** —— 解析没问题：`group[11] … maskIdx=[10->root-1]`，`maskRefs=1`，蒙版节点号就是"组号−1"（与解析期 dump 里 `■輪郭`=node[5]、`stencil`=node[6] 的相对关系一致）。
2. ❌ **"蒙版被前一组的收尾清掉了"** —— 加了 `maskRemaining` 引用计数后，`emptyMask` **完全没变**（4 → 4）。
3. ❌ **"`stencilMaskNodeIndices` 没随子运动展开重映射"** —— 修了，`mlNodesDrawn` 0 → 3（**有可观测变化**），但**截图逐像素未变**。
4. ❌ **"`■耳L/■耳R` 缺失是主因"** —— 该 PSB 里**确实没有** `■耳*` 节点，`resolved=1/3` 属数据本身；只有 1 个蒙版名解析不到不影响主链路。
5. ❌ **"`_le` 是真实的'布局单位'概念"** —— 参考实现里不存在。那些 30/47/22/14 的值**其实就是 Z（`coord[2]`）**。
6. ❌ **"整体恒定偏大 1.5–1.7×"** —— 那是在**画布仿射缩放 = 1.5 的那一刻**量的。`setDrawAffineTranslateMatrix count=6`，游戏是在**动画**它；实测同一角色的仿射是 `[1,0,0,1,585,705]`（缩放=1）。当前 `■胴体`：`srcSize=506x583`、`m` 幅值 = `0.750` = `gameScale`，四边形 ≈ **379 px 画布宽**，与 Kirikiroid2 参考图里躯干宽度**大体吻合** ⇒ 尺寸这条线基本清了。
7. ❌ **"右移 160/285 px 出自 anchor（图标原点）"** —— `anchor=(253.0,291.0)` **正好是 506×583 的一半**（纹理中心），锚点没错。（仿射本身也对：两角色 translate 差 750 px，Kirikiroid2 实测两人头部中心差 745 px。）
8. ❌ **"PSB 容器/树解析错"** —— 猫耳、蝴蝶结、发丝、部件拼装形状都正确，树的构建是对的。容器层的开源对照可看 `emote-psb`(Rust) / FreeMote(C#) / `GMWare.M2` / GARbro `ArcPSB.cs`，但**播放器语义没有现成轮子**（官方就是 M2 闭源 `motionplayer.dll`；`FreeMote-SDK` 的 WebGL 运行时是 emscripten 二进制、CC BY-NC-SA，不可 vendored）。

**本轮最后一份日志的判读结果（stencil 线结案依据）**：

```
group[11]  'stencil'            folded=0 glNodes=0 subtree=2  maskIdx=[10->root-1]
group[29]  'stencil'            folded=0 glNodes=0 subtree=2  maskIdx=[28->root-1]
group[61]  'stencil'            folded=0 glNodes=0 subtree=2  maskIdx=[60->root-1]
group[245] 'stencil'            folded=0 glNodes=0 subtree=2  maskIdx=[244->root-1]
group[417] 'ステンシル(shirome)' folded=1 glNodes=2 subtree=12 maskIdx=[413->root-1,416->root-1]
group[432] 'ステンシル(shirome)' folded=1 glNodes=2 subtree=12 maskIdx=[428->root-1,431->root-1]
pass folded=4 emptyMask=3 labels=[ステンシル(shirome)(2)×3] groups=6
pass mlNodesDrawn=3 maskPendingRefsEnd=4
```

⇒ `subtree>0 但 glNodes=0` = 子树节点**全被跳过**（可见性判定），按判读表 **⇒ 不是 stencil 问题**。

---

## 5. 其它未处理的账（与立绘几何无关，但都记着）

- `expandSubMotionNodes: no nodes for 'motion/general_obj_a..e' / 'ex_parts_a..e' / 'ex_body_a..e'`（15 条）：这些 sub-motion 的 `layer: []`，被引用但内容为空。**待确认是数据本身为空、还是解析漏了 key**（`parts_add` / `test` 两个场景的 motion 字典也是空的）。
- `.stall` 与 FPS 见 §3.2。
- 帧顶点色默认值差异：参考 `packedColors` 默认是 **`0xFF808080`**（`PlayerInternal.h:1441`，注释写明 "not four scalar channels"），我们用 `0xFFFFFFFF`。属着色问题，迟早要对齐。
- 截图里出现过 `AutoPath 0/96`（正常时应为 96/96），值得留意。

---

## 6. 可复现的工作流

```bash
# ── 引擎（WSL）。探针开/关都支持；改探针开关需要重新 configure（会全量重编，ccache 会兜）
cd /mnt/f/Projetcs/Krkr2Next
cmake -DENABLE_RENDER_PROBE=ON out/android/debug     # 或 OFF
./build.sh debug --engine-only                       # 产物 -> app/app/src/main/jniLibs/arm64-v8a/libengine_api.so
./build.sh debug --apk-only
bash install.sh debug --no-launch                    # 已按 mtime 取新包；签名与 Android Studio 一致（不再清数据）
```

```bash
# ── 抓真机截图（Windows 侧 adb）。注意 MSYS/Git-Bash 会改写 /sdcard 路径，**必须 MSYS_NO_PATHCONV=1**
ADB=/c/Users/xiaol/AppData/Local/Android/Sdk/platform-tools/adb.exe
MSYS_NO_PATHCONV=1 $ADB shell "screencap -p /sdcard/shot.png"
MSYS_NO_PATHCONV=1 $ADB pull /sdcard/shot.png "F:/Projetcs/Krkr2Next/temp/shot.png"
# 读图：直接用 read 工具读那个 png 即可（能看图）
```

```bash
# ── WSL 里的后台构建（**必须 setsid**，否则外层 bash 退出会把构建杀掉，
#    日志里表现为 "ninja: build stopped: interrupted by user."）
setsid bash -c "cd /mnt/f/Projetcs/Krkr2Next && ./build.sh debug --engine-only" \
  </dev/null > /home/clevebitr/krkr2next-build/logs/build.log 2>&1 & disown
```

**参考图与产物**（都在 `temp/`，已 gitignore）：
- `temp/shot-kirikiroid2.png` —— **正确渲染**（Kirikiroid2 = `com.akira.tyranoemu/Kirikiroid139`，同一句、两角色、柜台侧、名牌**巧克力**）
- `temp/shot-krkrnext5.png` —— 我们最新的错误画面
- `temp/app-debug.apk` —— 最近一次导出的包
- `temp/devlogs/` —— 已抓的真机日志

---

## 7. 参考源码位置（下一轮必备）

| 路径 | 是什么 |
|---|---|
| `temp/aether_main/` | **AetherKiri `origin/main` @ `803078d`**（`git worktree`）。**最新、最完整**：`cpp/plugins/motionplayer/{PlayerRender.cpp 659KB, PlayerUpdateLayers.cpp 315KB, PlayerQuery.cpp, PlayerResource.cpp, PsbValueReader.cpp, NodeTree.cpp, PlayerInternal.h}` |
| `temp/aether_ref/` | 同一仓库的**旧快照** @ `bd14a98`（`codex/fix-emote-atlas`）。ⓘ **本仓库里所有 "ported from AetherKiri" 的注释引的都是这个旧快照，落后约 200 commit —— 需要重新对基** |
| `temp/freemote-sdk/` | `Project-AZUSA/FreeMote-SDK` @ `4340e8f`。`WebGL/driver/emoteplayer.js` 只是高层 API 包装；`FreeMoteDriver.js` 是 51 行 emscripten glue，**核心运行时是二进制**（CC BY-NC-SA，不可 vendored）。同仓库 `Windows-DX9/iemote.h` = 官方 E-mote SDK 的 C API 头，`E-moteSDKReferenceManual.chm` = 官方参考手册 |
| `temp/amadeus-ref/` | 用户给的 `Mieluoxxx/amadeus-next-emote`（同样只是"把 JSON/PNG 组装成 PSB 再交给 FreeMote 运行时"的加载器，**不含播放器**） |

**结论（关于"是不是重复造轮子"）**：
- **容器解析**：有现成实现（`emote-psb` Rust / FreeMote C# / `GMWare.M2` / GARbro），这块确实在重复造轮子，**可拿来做金标准校验**；
- **播放器语义**（树求值 / 变换累加 / stencil / mesh / Z 排序 / `_le`）：**没有任何可用的开源轮子**，只能照 `temp/aether_main`（AetherKiri）与 `libkrkr2.so` 对齐。**NEKOPARA 的立绘问题全部属于这一类。**

---

## 8. 本轮追加：FreeMote **容器层**对照（只参考，未引入其代码）

**许可证红线**：FreeMote = CC BY-NC-SA 4.0 ⇒ **不可 vendor、不可复制代码**。
只取"格式事实"（位号 / 键名 / 默认值 / 枚举取值）并自行书写为常量。
参考副本在 `temp/freemote/`（`9773f50`，已 gitignore；上一轮的
`temp/freemote-sdk/` 是它的 SDK 仓库，另一个东西）。细则见
**`compat/recon/freemote-crosscheck.md`**。

落了什么（**不改任何渲染行为**，只统计 + 集中常量）：

| 文件 | 改动 |
|---|---|
| `cpp/plugins/psbfile/EmoteMaskBits.h` | 新增：`ContentMask` 全 20 位、`InheritMask` 11 项、`Stencil`、`Defaults`、三个默认全关的 A/B 开关、`MaskStats` |
| `cpp/plugins/psbfile/PSBMedia.cpp` | `ApplyFrameContentMask()` 改用常量（行为逐字节不变，已用 `-fsyntax-only` 验过）；新增"位=1 但键缺失 / 位=0 但键存在"双向自洽统计、`inheritMask`/`stencilType` 直方图、sub-motion 清点；每个 archive 打 3 行汇总 |

真机新日志（每个 archive 3 行，判读表见 crosscheck 文档 §3）：

```
emote mask stats [<archive>]: frames= withMask= bits= resetTriggered= tableMismatch= unhandled=
emote layer stats [<archive>]: inheritMask= stencilType= labels:<有声明>/<总数> colorKeyBit=[set / clear]
emote sub-motions [<archive>]: total= emptyLayer= emptyWithParam= names=[…]
```

它一次回答三个悬案：
1. **`content["mask"]` 位 9(color)/11..15(曲线)/17(bm) 到底要不要实现** —— 看
   `unhandled=` 与 `resetTriggered=`；
2. **`(stencilType & 4)` 判据是否有区分度** —— 看 `stencilType=` 直方图；
3. **§5 那 15 条 `layer: []` 是数据空还是解析漏** —— 看 `emptyLayer/emptyWithParam/names`。

下一步（依赖真机一轮）：拿上面 3 行日志 → 按 crosscheck 文档 §3 的判读表决定翻哪个
开关（`EmoteMaskBits.h::Switches`，一次只翻一个，翻完重测）。

---

## 9. 给下一轮的一句话行动

> 打开 `temp/aether_main/cpp/plugins/motionplayer/PlayerUpdateLayers.cpp:4690` 与 `:5774`，把 `drawAnimatedTree` 的绘制改成"**先算全部节点累积变换（含 `posZ`），按 `posZ` 稳定排序后再画**"，带一键回退开关，构建装机，让用户停在同一句、抓一张图对照 `temp/shot-kirikiroid2.png`。
