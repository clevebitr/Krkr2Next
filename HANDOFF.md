# 交接文档（KrKr2Next / KiriNext）

> 用途：把**当前目标、已落地的东西、未完成项、硬约束、验证办法**交给下一个上下文。
> 读这份之前先读根目录 `AGENTS.md`（项目级规则）。兼容层的细节事实与逐条差异见
> `compat/README.md`，本文件是"接手第一份"。
> 渲染层的 open issue 明细（证据 + 下一步探针）在 **`compat/recon/render-issues.md`**。

---

## 0. 一句话现状（2026-09-22）

**当前主战场：渲染层**。三款游戏（NEKOPARA 4 / 千恋万花 / nainiuniu5krkr=G2）的问题都已
从"现象"推进到"可定位"，其中**四条已修好并在真机确认**、**一条已修待真机回归**：

- **G2 启动期 `diffimage2.tjs` 无限递归 → 已修**（A 块回退补上方法调用路径）
- **G2 Live2D 从未被驱动 + 图片以 ZIP 头加载失败 → 已修**（伴生脚本遮蔽游戏脚本，`TVPGetPlacedPath` 解析顺序 bug）
- **NEKOPARA 的 AMV 解码失败（视频帧当 CG 显示）→ 已修**（AlphaMovie 解码器下沉 core）
- **NEKOPARA 的 AMV 播放 → 已修**（完整移植 AlphaMovie 插件）
- **NEKOPARA 翻转动画位置偏移（c1/c2 一步对一步错）→ 已修待真机回归**（`showNextImage` 补上帧头裁剪偏移 + 越界裁剪，见 §1.4）

**当前未解决**（详见 `render-issues.md`）：

1. **G2 进动画卡 4.4s**：已细分到 `CreateRenderer(1920x1080)` 本身 2689ms
2. **G2 帧率 ~43–45（目标 60）**：每帧 1920×1080 GPU→CPU 回读（插件设计使然）
3. **千恋万花 `wave` 转场缺失 + `SystemWatchTimerTimer` 卡顿 + SD/logo 交付（已改待回归）**

其余两条目标的状态：

1. **兼容层**：`cpp/core/io/` 单一 IO 组件已建成；A 块、C1、C4、B1（部分）、B2 已完成；
   `kag` 渲染档已删除（能力归 AetherKiri 层）。剩余 C3（阻塞于 C2）、C5/C6/C7、E1、M6 分批、
   以及**一项等用户裁决的 M1 尾巴（I3）**。
2. **壳（Kotlin/Compose）**：九项改造全部落地；详情页已改成 **Steam 大屏式左右分栏**；
   自定义按键浮层**已完成**（2026-09-23，`core/KeyPadConfig.kt` + `ui/KeyPadOverlay.kt` +
   `ui/KeyPadConfigEditor.kt`）；壳的 Kotlin 编译**已能在本地跑通**（`scripts/build_shell_local.sh`）。
   待做一项（引擎菜单侧边栏）的规格在 **`SHELL_HANDOVER.md`**。

工作区除用户自己的 `.gitignore`/`README.md` 外干净（那两个文件**始终不要 add**）。

---

## 1. 本会话已落地的关键修复（理解现状必读）

按影响面排序，每条都能独立回答"为什么现在是这个样子"。

### 1.1 伴生脚本遮蔽游戏脚本（`cpp/core/io/IoStorage.cpp`）— 影响最广

`TVPGetPlacedPath()` 用 `TVPIsExistentStorageNoSearchNoNormalize()` 判断"当前目录是否已有"，
而后者末尾是 `return krkr::io::IsVirtualFile(name);`——伴生脚本名单（按 **basename** 匹配）
因此被当成"已找到"，**auto-path 搜索被整个跳过**，游戏真实的
`data.xp3>system/live2d.tjs` 永远没被尝试（KAG 用裸名 `live2d.tjs` 请求）。

症状：G2 的 Live2D 从未初始化（占位脚本顶掉真脚本），并连带出现"图片以 ZIP 头加载失败"
（`Unsupported image format (header 504b0304)`）。

修法：`TVPGetPlacedPath` 的"当前目录"判断改为**只查物理**
（`TVPIsRealStorageNoSearchNoNormalize`），虚拟文件改为 **auto-path 搜索失败后的最后兜底**。
这与 `IoVirtualFile.h` 自己写明的契约"**物理文件优先**"一致。

> 教训：虚拟文件（伴生脚本）的任何查询点都要排在物理与 auto-path **之后**。

### 1.2 A 块回退缺方法调用路径（`cpp/core/tjs2/tjsObject.cpp`）

`TJSCompatIsStartupNoOpFunction` 里本来就有 `commitSavedata`，但 A 块回退只挂在
`tTJSCustomObject::PropGet` 上；而 `Storages.commitSavedata()` 是**方法调用**，走 `FuncCall`、
不经 `PropGet` ⇒ 回退永远没被问到。

G2 的完整因果链：`startup.tjs` → `mainwindow.tjs saveSystemVariables`
→ `Storages.commitSavedata` 不存在 → `catch` 里 `Storages.rollbackSavedata` 也不存在
→ 异常抛出 → 引擎兜底重跑 `system/Initialize.tjs` → `diffimage2.tjs` 执行两次
→ `Storages.isExistentStorage` 双重包装 → 该包装**调用时读全局** `diffOrigIsExistentStorage`
（已指向第一次的包装 W1）→ W1 调 W1（且它没有 `diffEnterCount` 兜底）→ 无限递归 → 栈耗尽。

修法：新增 `TJSCompatResolveFuncCallFallback()`，在 `FuncCall` 的 `!data` 分支挂上与
`PropGet` **同名单、同顺序**的回退链；`rollbackSavedata` 加入 no-op 名单。仅 AetherKiri 层生效。

### 1.3 AlphaMovie：插件完整移植 + 解码器下沉 core

NEKOPARA 的 AMV 帧载荷**不是标准 JPEG**：`AlphaMovie.dll` 自带 Huffman 编码，载荷无 SOI/DHT
（Huffman 用标准表），DQT 取自文件头 `quantaization_table_size_plus_hdr_size`。
这正是 turbojpeg 报 `Could not determine subsampling level` 的原因。

两条链路分开修：

| 链路 | 走法 | 修法 |
|---|---|---|
| **翻转动画（视频播放）** | `AlphaMovie` TJS 类 | **完整移植**上游 `cpp/plugins/alphamovie.cpp`（3519 行，原为 158 行 stub）；含 `GLAlphaMovie.dll` 别名 |
| **`Layer.loadImages("<amv>")`（视频帧当 CG）** | core 图形路由器 → `TVPLoadAMV` | **解码器下沉 core**（见下） |

为什么解码器进 core 而不是让插件注册 `.amv` 加载器（方案 B1）：`.amv` 本就由 core 的
`tTVPGraphicType` 注册（`GraphicsLoaderIntf.cpp:238`，与 `.tlg/.png/.jpg` 同表）；core 不得
依赖插件、插件可依赖 core（`compat/README.md §2`）；且目前没有任何插件用过
`TVPRegisterGraphicLoadingHandler`。

落地物：
- `cpp/core/visual/AlphaMovieDecoder.{h,cpp}`：片段移植自上游插件（Huffman 表规格与构建、
  DC/AC 解码、IDCT、zlib/jpeg 两条 MCU 循环、YUV→RGBA）。登记为 `partial-extract`。
- `LoadAMV.cpp`：jpeg 分支先试专用解码器，失败再走标准 JPEG 路径（两种变体靠载荷内容区分）。
  同时**纠正帧头命名**——那 4 个 uint16 是裁剪矩形 `left/top/width/height`（上游注释：它们是
  `copyNextImageToTexture` 返回的 `Rect.left/top`），不是 alpha 平面尺寸；历史命名
  `alpha_width/alpha_height` 有误导。
- `cpp/core/visual/CMakeLists.txt`：新增 `find_package(FFMPEG avutil swscale)`。
  **不能反向依赖 `core_movie_module`**（`movie → visual` 已是 PRIVATE 边，会成环）。

**未做（下一步）**：让 `alphamovie.cpp` 改为调用 core 解码器，删掉重复的 ~1700 行实现。

### 1.4 AlphaMovie 帧落笔点：补上帧头裁剪偏移（NEKOPARA 翻转动画位置错误）

`showNextImage()` 原来把解码帧画在 `(_left, _top)`——那是脚本 `setPosition` 设的值，
**丢掉了帧头的裁剪矩形**。NEKOPARA 4 的 AMV 把角色区域编进帧头：`c1` 变体＝角色在
右半屏＝`crop=(636,0) 656x720`，`c2` 变体＝`(0,0)`。于是同一套代码下 c2 正常、c1 整体
左移 636px——正是"一个位置正确、一个不正确"。

证据（真机 `engine-20260922-145722.log` + 直接解析 `vol4adult.xp3` 里 `neko4_h02c1a.amv` 的帧头）：
```
probe: AlphaMovie.showNextImage [neko4_h02c1a.amv] frame=2/60 crop=(636,0) 656x720 pos=(0,0) screen=1280x720 layer=1280x720
```
所有 `*c1*` 变体首帧均为 `(636,0) 656x720`，`*c2*`/`H10*` 为 `(0,0)`。

修法（`cpp/plugins/alphamovie.cpp`，属 `local-fix`）：落笔点改为
`(_left + frame_left, _top + frame_top)`，并**自行裁剪越界部分**——GL 的
`glTexSubImage2D` 对 `x+w>width` 报 `GL_INVALID_VALUE` 并丢弃整块，不会裁剪；c1 的
`636+656=1292` 正好超出 1280。同一语义在校验过的两条路径上一致：core 的
`LoadAMV.cpp`（视频帧当 CG）与插件的 `copyNextImageToTexture`（GL 路径）都用帧头裁剪偏移。

### 1.5 SeparateLayerAdaptor 渲染层父层：SD/emote 层级（千恋万花）

Yuzusoft 的 SD/Q 版动效（`data1080.xp3` 的 `sdNNN.mtn` + `SDNNNAA.png`）由 motionplayer
经 `Motion.SeparateLayerAdaptor` 承载。`patch.tjs` 设 `Motion.Player.useD3D = 0` ⇒ 走
adaptor 的私有渲染层；参考实现里**该层就是可见的呈现层**（脚本不再把它拷回 owner），
所以它的父层直接决定 SD 画在 UI 的上面还是下面。

KiriNext 的 `GetSeparateAdaptorRenderTarget`（`cpp/plugins/motionplayer/main.cpp`）把它挂到
`window.primaryLayer`；参考实现（krkrsdl3；AetherKiri `PlayerRender::resolveSeparateLayerRenderTarget`）
把它建成**构造函数 owner 层的子层**——owner 是游戏放在正确 z 序上的 `AffineLayer`，脚本随后
把 `owner.type` 改成 `ltBinder`，渲染层紧贴其上。

改法：owner 能解析为真实 Layer 时以 owner 为父层，并把子层 `left/top` 归零（子层坐标相对
owner，否则会被 owner 位置再偏移一次）；否则维持原 `primaryLayer` 回退。另按参考实现把
`SeparateLayerAdaptor.assign` 补成 no-op（参考注释：拷回 owner 会得到第二张偏移画面）。
插件加了一条一次性路由日志 `motion: SeparateLayerAdaptor 渲染层路由 owner=… parent=…
parentIsOwner=… parentName=…`（每次创建 adaptor 一条、封顶 8 条）用于确认父层选择。
**待真机回归。**

> ⚠️ 2026-09-22 真机修正：千恋万花实际走的是 **D3DAdaptor** 路径
> （`engine-20260922-155105.log`：`D3DAdaptor.captureCanvas` 301 次、
> `SeparateLayerAdaptor` 0 次），所以这一处改动**不是本作的解**（对别的 Yuzusoft
> 标题可能仍有用）。本作的真因见 §1.6。

### 1.6 D3DEmote scratch 交付：补 `Layer.assignMotionImages` + `AssignImages` 路由（千恋万花 SD）

真机日志把本作 SD 的链路摸清了：SD 分件（`data1080.xp3` 的 `sdNNN.mtn` +
`SDNNNAA.png`）由 motionplayer 的 D3DEmote 路径渲染，最后由游戏脚本
`system/AffineSourceMotion.tjs`（编译字节码；字符串表里只有 `assignImages`，没有
`assignMotionImages`）把 scratch 层交给角色层，池层名 `AffineSource情報プール用`
（见 `system/AffineSource.tjs`）。

`AssignImages` 走 `MainImage->Assign()`，会让角色层与 scratch **共享同一张纹理**；
下一帧重写 scratch 就把刚交付的画面抹掉 ⇒ 真机表现：SD 显示一两秒后消失 / 只剩背景
UI / 残留矩形。参考实现为此提供 `Layer.assignMotionImages`（把完成的纹理**换**进目标
层），并在 `AssignImages` 内部识别 scratch 交付后路由过去。

改动：`cpp/core/visual/LayerIntf.{h,cpp}` 新增 `AssignMotionImages`（移植自 AetherKiri
`LayerIntf.cpp:6065-6265`，去掉其 KAG 转场/exchanged-page 路由与 profile/trace 埋点）
+ 注册 `Layer.assignMotionImages`；并在 `AssignImages` 里加
`TVPIsAffineSourceMotionScratch()`（目标可见有名、源隐藏无名、源父层是
`AffineSource情報プール用`、目标父层不在池内）路由到交换语义。**待真机回归。**

### 1.7 其他

- `cpp/core/visual/LayerIntf.{h,cpp}`：补 `ExchangeMainImage`（AlphaMovie 移植所需的唯一外部
  API 缺口；片段移植，落点有注释）。
- 探针构建：`ENABLE_RENDER_PROBE` 选项与 CI 管道本就正确，真正的问题是**探针代码自身编译不过**
  （`ttstr(...).c_str()` 交给 fmt → `const char16_t*` 被判为非法指针格式）——已改为 `AsStdString()`。

---

## 2. 目标一：KAG 兼容层对齐 AetherKiri

用户确认的范围是五块（原话概括）：

| 块 | 内容 | 状态 |
|---|---|---|
| A | TJS2 内核兼容读写：未定义全局回退(34 名) + 启动名回退 + `touchImage`/`renderCount` 合成 + 8 名启动期**写**白名单 | ✅ 完成（2026-09-22 补上 **FuncCall** 路径） |
| B | KAGWindow / krkrgles 脚本别名与绘制设备接管 | 🟡 B2 扇出/重试/卸载/契约已完成；B1 伴生脚本部分实施（GPU 占位 11 名 / motion-parameter / split-emote），gfxEffect/logwindow/D3DEmote 未移；**2026-09-22 修掉伴生脚本遮蔽游戏脚本（见 §1.1）** |
| C | KAGParser / extkagparser / kagparserex 行为对齐 | 🟡 C1 ✅、C4 ✅；C3/C5/C6/C7 未做 |
| D | 插件模拟层（旧 Windows 插件全量照搬，分批） | 🟡 已移 6 个 AetherKiri 层专属模块 + 8 个（systemEx/registory/stdio/javascript/messenger/msgreceiver/tasktray/adjustMonitor）+ **AlphaMovie（2026-09-22）**；其余约 50 个缺失模块待分批 |
| E | 启动与资源加载顺序（startup/patch/auto-path/插件解析） | 🟡 I 系列大部分已做；`patch.tjs` 分两层（E1）未做 |

架构要求（用户明确）：

- 先做 **M1：把文件 IO/加载逻辑收敛为单一隔离组件**（新目录 + 稳定接口，现有公开 API 保持兼容）
  —— **已完成**：`cpp/core/io/`，公开声明仍在 `base/StorageIntf.h` 与 `base/impl/StorageImpl.h`。
- 兼容层按游戏选择：`krkr2next.json` 的 compat 字段 → 引擎选项 `game_compat_profile`；
  **缺省走旧版 krkr2 层**（`krkr2-classic`），AetherKiri 层显式开启（`aetherkiri` 档）。
- **架构收敛为两层**：删除 `kag` 渲染档与 `krkrz-kag` 兼容档，其 KAGWindow 接管能力归
  AetherKiri 层（`krkrgles` post-regist 按 `ActiveLayer()==AetherKiri` 安装）；`auto` 判到
  `motionplayer*` 直接激活 AetherKiri 层。Live2D 仍用本仓库原生 Cubism 实现。
- 两层共用同一 IO 组件，不重复实现。

---

## 3. 目标二：壳（Kotlin/Compose）改造清单

| 要求 | 状态 |
|---|---|
| 引擎日志**按游戏分开** | ✅（剪枝/清空/分享/记住上一局） |
| 点**封面**进详情页；启动/移除游戏**只在详情页** | ✅ |
| 移除游戏**不删文件**，只从库移除 | ✅（UI 已写明） |
| 详情页按 **MD3** 实现 | ✅ |
| **游戏设置单独页面** | ✅ `ui/GameSettingsScreen.kt` + 路由 |
| **导航栏**，适配平板与手机 | ✅ 底部 `NavigationBar` / ≥600dp 左侧 `NavigationRail` |
| 去掉设置里多余说明（开发测试游戏名） | ✅ 改通用表述 |
| 文件浏览器：两个 topbar 合一 + 完整路径跳转 + 目录收藏 | 🟡 顶栏合一 ✅、路径跳转 ✅、目录收藏数据层 ✅ + UI 改动 ✅，**交互待上机确认** |
| 游戏库：收藏游戏 + 分组（便签式） | ✅ |
| 关于页：作者/协议/仓库/技术栈/版本号 | ✅（版本号已带 git 短哈希：`v0.1.0-<hash6>-<YYMMDD>`） |
| 详情页布局：**封面在左、按钮在右**（Steam 大屏式） | ✅ 2026-09-22（`ui/GameDetailScreen.kt`） |
| 自定义按键浮层（位置/大小/文字/MD3 图标/颜色/透明度/描边，每游戏 + 全局模板） | ✅ 2026-09-23，规格与实现落点见 `SHELL_HANDOVER.md §3` |
| 游戏中右下角按钮 → 右侧悬浮侧边栏显示**引擎注册的窗口菜单** | ⬜ **未做**（需新增 C ABI + JNI），规格见 `SHELL_HANDOVER.md §4` |

> **壳的开发交接文档是 `SHELL_HANDOVER.md`**（本地编译闭环、文件/接口索引、两项待做功能的
> 数据模型与落点、验收标准）。改壳前先读它。

---

## 4. 硬约束（踩过的坑，别再犯）

1. **不要提交/推送用户自己的改动**：`.gitignore`、`README.md` 在会话开始时就是 modified，
   始终不要 `git add`；也不要 `reset --hard` / `checkout --` / `clean -f`。
2. **构建只在 CI**：本机没有 NDK/vcpkg/cmake，`./build.sh` 跑不了。可用的只有
   `bash scripts/check_static.sh`（JNI 符号 / 移植清单 / 语法）。
3. **推送会取消上一条 run**（`concurrency: cancel-in-progress`）：中间 run 显示 `cancelled`
   属**预期**；以最后一次为准。**多次提交攒成一次 push**，别每条提交都推。
4. **壳的 Kotlin 编译本地能跑**：`bash scripts/build_shell_local.sh`（缓存的 Gradle 8.14.5 + termux 原生 aapt2 覆盖 AGP 自带的 linux-x86_64 版）。改壳**先本地编译过再推**，别为拼错一个 import 等一次 CI；新加跨文件符号仍要顺手补 import。完整 APK 仍由 CI 出（本地 jniLibs 没有 libengine_api.so）。
5. **移植纪律**：独立新文件的移植登记 `compat/upstream/aetherkiri_ports.json`
   （片段移植用 `partial-extract` + `source_ref`），并跑 `python3 scripts/check_port_drift.py --update`；
   **既有文件内部的片段移植不进清单**，改为在落点写"移植自 AetherKiri <文件>:<行范围>"。
   改了 ported 文件必须显式更新 `modifications`，否则静态检查**刻意硬失败**。
6. **不要动内存预算/压力相关逻辑**（用户明确禁止）；**渲染改动要小**。
7. **ffmpeg 头必须包 `extern "C"`**：本仓库 `movie/` 下的头统一这么写（`AEUtil.h:7` 等）。
   不包会按 C++ 生成修饰名，而 `libswscale.a` 提供 C 符号 → 链接期 `undefined symbol`。
   不要把它当"冗余包裹"删掉（本会话踩过）。
8. **不要按行号切代码**：本会话用行号切片生成 core 解码器时，起始行记错 2 行就切掉了
   `struct BufferManager` 的头，连锁报出"unknown type name"一串。要用**模式匹配 + 括号配对**
   定位边界，并在生成后静态复核（残留引用计数、括号平衡）。
9. **探针不得改变正常结果、时序、性能**（AGENTS §10）。**帧率必须在普通构建上测**：
   探针构建 30 秒写几百 KB 日志，`HostWindowLayer::RTProbe` / `engine_tick: … enter/return`
   （带 `flush()`）都是每帧同步写盘，会把 fps 测低。高频日志要采样/限频/只打边沿。
10. **真机验证只能靠用户装 CI 产物**；本机看不到设备。
11. **别用 `rg "A\|B"`**：rg 用 Rust 正则，`\|` 是**字面量管道符**而非 alternation，
    会静默匹配为空。本会话因此两次误判（"CMake 没这个 option"、"日志里没有问题"）。
    一律写 `rg "A|B"`。
12. **commit message 必须带 Conventional 前缀**：`<type>(<scope>): <中文主题>`（type/scope 取值与正文要求见 `AGENTS.md` 的「Git 协作」）。
    本会话（2026-09-22）的 **17 条提交全部漏了前缀**，是近 300 条里唯一一批不带的（其余 200 条中 164 条带前缀）。
    开工第一条提交前先 `git log --format='%s' -20` 对齐格式。
13. **commit message 别用双引号 + 反引号**：shell 会把反引号当命令替换，把内容吃掉（本会话踩过）。
    用 heredoc（`git commit -F - <<'EOF'`）或 `git commit -F <file>`。
14. **push 会取消正在跑的 CI**：要交给用户验收的构建（特别是探针构建）在跑时，先把改动**只提交不推送**，
    等构建产出产物后再推。

---

## 5. 已裁决的决策（不要重新讨论）

| 议题 | 裁决 |
|---|---|
| A 块回退（全局名/启动名/renderCount/touchImage，**含 FuncCall 路径**） | **只给 AetherKiri 层**（`TJS::TJSSetCompatFallbacksEnabled`，缺省关） |
| A3 启动期**写**白名单（8 名） | **两层都要**（无开关） |
| C1 `taglist` + `copyTag` | **两层都要** |
| C3 `GetNextTag` 文本段聚合 | 只给 AetherKiri 层，但**阻塞**于 C2 翻译层 |
| C4 `.scn` 容错 + 标签回调 | 两层（未注册回调时行为与移植前逐字相同） |
| E1 `patch.tjs` 时机 | **分两层**：classic 保持 startup 之前；AetherKiri 层照搬上游（**必须连晚 patch 韧性层一起**） |
| I2 首次读 `startup.tjs` 被原版压住 | **在 classic 层修**（已做） |
| 缺省兼容层 | **旧版 krkr2 层**；`aetherkiri` 档显式开启 |
| **虚拟文件（伴生脚本）与物理文件的优先级** | **物理优先，auto-path 次之，虚拟最后兜底**（2026-09-22 定；实现见 `TVPGetPlacedPath`） |
| **`.amv` 解码器放哪** | **core**（`AlphaMovieDecoder`），插件复用；不允许插件反向注册 core 的格式处理项 |
| **AlphaMovie 插件** | 按上游**完整移植**（用户明确选"一次性完整移植"） |
| **千恋万花 SD 交付的修法** | 用户 2026-09-22 选 **B：搬参考的 `D3DEmote.tjs`** 作 `system/motion.tjs` 的覆盖（兼容性优先），而不是继续按结构打补丁（方案 A）。实施规格见 §6.1 |

---

## 6. 未完成 + 阻塞

### 6.1 渲染层（当前主线，详见 `compat/recon/render-issues.md`）

| 项 | 规模 | 状态/阻塞 |
|---|---|---|
| G2 **进动画卡 4.4s** | 中 | 已细分：`createRenderer=2689ms bindTexture=0ms mvp=0ms`（1920×1080，1 张纹理）⇒ 卡在 `CreateRenderer`；需继续查 Cubism 渲染器/掩码缓冲创建 |
| G2 **帧率 ~43–45** | 中 | 每帧 1920×1080 **GPU→CPU 回读**（`capture` 路径**刻意优先 CPU**：引擎随后按 CPU 位图重传纹理会覆盖只写纹理的内容）；主窗口走 `path=GPU`，只有 Live2D 图层退化。附带：该回读用 `GL_BGRA_EXT` 调 `glReadPixels`，ES3 非法 → `err=0x0502` |
| G2 / 千恋万花 **`SystemWatchTimerTimer` 卡顿**（1.5–1.9s） | 中 | 卡在 `DeliverEvents()` 或 `TickBeat()` 循环（内层 MarkStage 未触发）；需在该函数内加细阶段探针 |
| 千恋万花 **`wave` 转场缺失** | 中 | **需按 GPU render method 重做**：2026-09-23 试过逐字节移植 AetherKiri 的 CPU 扫描线实现，CI 编译失败（`iTVPScanLineProvider::GetScanLine*` 在本仓库被 `#if 0`）；根因与结论见 `render-issues.md §2` |
| 千恋万花 **SD/logo 交付（D3DEmote）** | 中 | 已补 `Layer.assignMotionImages` + `AssignImages` scratch/页面交换路由（均未解决本作）；**已裁决走方案 B**，见下 |
| 千恋万花 **字体/文字颜色偏白、logo 色偏与残留矩形** | 中 | 候选根因：参考引擎为本作应用的 7 个标题 hook（含 `message edge argument routing`）KiriNext 全缺；未定位到 code path |
| **AlphaMovie 插件复用 core 解码器** | 中 | 未做；完成后删掉重复 ~1700 行 |

#### 千恋万花 SD：方案 B（搬参考的 D3DEmote.tjs）实施规格

真机已排除的：纹理别名（`Independ` 已断）、目标层自身参数（`visible=1/opacity=255/ltAlpha/1920x1080`）。
剩下的事实：SD 的目标层 `CG View LayerAffineLayer` 的 **`parentVisible=0`**（在
`CG View Layer` → `裏メッセージレイヤ2` 这条“裏”链上），而 `ev`/`title_bg` 会被游戏换到可见页。
根因是**游戏自带的 `system/motion.tjs` 的交付目标与本引擎的图层语义不匹配**；参考引擎
不中招是因为它**用自己的 `D3DEmote.tjs` 替换了 `motion.tjs`**。

上游源（本地已有检出）：`../AetherKiri/cpp/core/base/resources/D3DEmote.tjs`（1340 行）
@ `bd14a986`；嵌入方式见 `../AetherKiri/cpp/core/base/CMakeLists.txt:7-13` 与
`resources/D3DEmote_tjs.cpp.in`（`configure_file` 生成字节数组）；覆盖判定见
`../AetherKiri/cpp/core/base/StorageIntf.cpp` 的 `TVPIsD3DEmoteCompanionScript()`
（**只匹配 `motion.tjs` 与 `d3demote.tjs`**）与 `TVPOpenD3DEmoteCompanionScript()`。

四件工作：

1. **脚本落地 + 嵌入**：把上游 `D3DEmote.tjs` 拷到 `cpp/core/compat/resources/`，按上游
   同款 `configure_file` 生成 C 数组（别手写 C++ 字符串字面量，1340 行日文脚本易错）。
2. **覆盖优先级**：上游是在存储读取路径**早期拦截**这两个名字（不是“虚拟文件兜底”）。
   注意本条与 §5 的“物理优先、auto-path 次之、虚拟最后兜底”不矛盾：**伴生覆盖只针对
   这 2 个明确列出的名字**，其余仍享物理优先。落点在 `cpp/core/io/`（KiriNext 的单一 IO
   组件），并按 §5 的裁决**只对 AetherKiri 层生效**。
3. **D3DAdaptor 壳成员**：参考脚本会调 `setPresentationTarget(target)` /
   `clearPresentationTarget()` / `presentationHold` —— KiriNext 的 D3DAdaptor 壳都没有
   （目前只有 `captureCanvas`/`unloadUnusedTextures`/`canvasCaptureEnabled`/`clearEnabled`）。
   参考实现：`../AetherKiri/cpp/plugins/motionplayer/D3DAdaptor.h:145-176` 与 218。
4. **清理**：方案 B 生效后，本会话为方案 A 加的那些兜底（pool 判据 `TVPIsAffineSourceMotionScratch`、
   按结构改投的 KAG 页面交换路由、`Independ` 断开别名）要重新评估是否还需要：
   脚本不再走那条路时它们是死代码，但**删除前先真机回归**至少一作。

验证：SD 是否正常显示、logo 颜色与残留矩形是否消失、字体颜色是否正常；
并跑一遍其它 Yuzusoft 作品（NEKOPARA 4、咖啡馆）确保伴生覆盖不伤它们。

### 6.2 兼容层 / 插件 / 壳

| 项 | 规模 | 阻塞 |
|---|---|---|
| **I3（唯一等用户一句话）**：classic 层在档案工程直启 `.../data.xp3>` 时是否挂兄弟 `patch*.xp3`？ | 小 | 决定 M1 最后一个策略开关 `mountSiblingsForArchiveProject` |
| C3 `GetNextTag` 文本段聚合 | ~230 行 | **阻塞**：上游依赖 C2 `TVPTransformText`/`TVPPrefetchText` |
| C5 每帧 KAG 修复（`envclear` 复位、`[endtrans]` 无 trans 等待） | ~120 行 | 依赖 C6 部分前提 |
| C6 KAG 运行时补丁层（27 文本补丁 + 11 类包装） | ~1500 行 | 前提是 patch.tjs 晚执行（E1） |
| C7 `ExtKAGParser`（第二解析器） | ~4700 行 | 先改 `ExtKAGParser.hpp` 的 `KAGParserH` 保护宏、定 `paramMacros`/`copyTag` 缺失、与 `kagparserex` 空壳互斥 |
| E1 `patch.tjs` 分两层（含韧性层） | 中 | 无（已裁决），影响面大需逐游戏回归 |
| B1 伴生脚本虚拟替换（gfxEffect/logwindow/D3DEmote 未移） | 中 | 无；见 `compat/README.md` |
| I13 `arc`(PackinOne) / `mem` / `zip` 存储媒体 | 中 | **待裁决**；G2 的 ZIP 头症状已由 §1.1 修复，故优先级下降 |
| M6 其余插件（约 50 个缺失） | ~3300 行 | 无；清单见 `compat/recon/plugin-compat-diff.md` |
| 壳：目录收藏交互打磨、平板双栏、库页/详情页 MD3 细节 | 小-中 | 无 |

---

## 7. 怎么验证

**本地（每次改完都要跑）**
```bash
bash scripts/check_static.sh          # JNI 符号 / 移植清单 / 语法（有失败项会硬失败）
bash scripts/build_shell_local.sh     # 壳的 Kotlin 编译（改壳必跑）
python3 scripts/check_port_drift.py   # 只查移植漂移（改了 ported 文件要 --update）
git diff --check                      # 空白/冲突标记
```

**CI（唯一真构建）**
```bash
# 普通构建（push 自动触发；测帧率用这个）
git push origin main
# 探针构建（要 probe: 日志时用这个）
gh workflow run "Android 构建" --repo clevebitr/Krkr2Next --ref main \
  -f build_type=debug -f enable_render_probe=true
RID=$(gh api "repos/clevebitr/Krkr2Next/actions/runs?per_page=1" --jq '.workflow_runs[0].id')
gh api "repos/clevebitr/Krkr2Next/actions/runs/$RID" --jq '.conclusion'    # 以此为准
gh api "repos/clevebitr/Krkr2Next/actions/runs/$RID/artifacts" --jq '.artifacts[].name'
gh run view "$RID" --repo clevebitr/Krkr2Next --log-failed | rg -i "error:|undefined symbol|FAILED:"
```
产物：`KrKr2Next-apk-debug`、`libengine_api-debug`。CI 偶发 **NDK 下载损坏**
（`Archive is not a ZIP archive`）——那是基础设施问题，**重跑即可**，不是代码错。

**真机回归清单（交给用户）**
- 每游戏日志：`/storage/emulated/0/Android/media/org.dpdns.clevebitr/logs/games/<游戏名>-<短哈希>/engine-<时间戳>.log`；卡死另有同前缀 `.stall`
- 关键行：`compat layer: 激活层切换为 …`、`io policy: tie-break=… patch-rule=…`、
  `AetherKiri 层接管完成（…别名 N/4…）`、`FontSystem: 已注册字体 N 个 -> …`
- 探针行以 `probe:` 开头；几何探针见 `probe: AlphaMovie.showNextImage/copyNextImageToTexture`

---

## 8. 关键文件与证据索引

| 东西 | 位置 |
|---|---|
| 兼容层事实/约束/阶段表/差异清单/裁决记录 | `compat/README.md`（§1 层与选择、§2 依赖不变量、§3 目录边界、§4 阶段、§5 差异+裁决、§6 移植溯源、§7 恢复指引） |
| **壳开发交接（本地编译闭环 + 两项待做功能规格）** | **`SHELL_HANDOVER.md`** |
| 渲染问题追踪（open issues + 探针清单 + 取证命令） | **`compat/recon/render-issues.md`** |
| IO 对照证据 | `compat/recon/io-loading-diff.md` |
| KAG 脚本层对照证据 | `compat/recon/kag-script-diff.md` |
| 插件层对照证据（约 100 个模块覆盖表） | `compat/recon/plugin-compat-diff.md` |
| 渲染层对照证据（兼容层↔渲染耦合、ES2-only 残留） | `compat/recon/render-diff.md` |
| 移植溯源清单 | `compat/upstream/aetherkiri_ports.json`（12 个文件） |
| IO 组件 | `cpp/core/io/`（`StoragePolicy.h` 策略契约；`IoPolicy.*`；`IoModuleLocator.*`；`IoVirtualFile.*` 虚拟文件注册点） |
| 兼容层框架 | `cpp/core/compat/`（`CompatLayer.*`、`ModuleGate.*`、`AetherKiriCompanions.*`） |
| A 块 TJS 回退 | `cpp/core/tjs2/tjsObject.cpp`（`TJSCompatResolve*`，含 **FuncCall** 链） |
| **AlphaMovie** | 插件 `cpp/plugins/alphamovie.cpp`（上游逐字节 + `local-fix` 几何探针 + 帧落笔点裁剪偏移修复）；core 解码器 `cpp/core/visual/AlphaMovieDecoder.{h,cpp}`（`partial-extract`）；接入点 `cpp/core/visual/LoadAMV.cpp` |
| 图形加载器注册表 | `cpp/core/visual/GraphicsLoaderIntf.cpp`（`.amv` 在第 238 行；`TVPRegisterGraphicLoadingHandler` 是对外注册 API） |
| StallWatchdog（卡死探针） | `cpp/core/utils/StallWatchdog.h`（阈值 1500ms，卡死写 `<log>.stall`） |
| 层专属插件 | `cpp/plugins/compat/aetherkiri/` |
| 壳 UI | `app/app/src/main/kotlin/org/dpdns/clevebitr/ui/` |
| 每游戏日志 | `app/.../core/LogFiles.kt`、`core/EngineSession.kt` |

---

## 9. 下一轮建议顺序

1. **NEKOPARA 翻转动画位置（已修，先做真机回归）**：装最新构建跑一次 c1/c2 变体，确认
   右半屏角色（`neko4_h02c1a.amv` 等 `*c1*`）与左半屏角色（`*c2*`）都落在正确位置。
2. **AlphaMovie 插件复用 core 解码器**：删掉插件内重复实现（单独提交、便于回退）。
3. **G2 进动画 4.4s**：继续查 `CreateRenderer(1920x1080)` 为何 2689ms
   （Cubism 掩码缓冲/GL 资源创建；可在 `CreateRenderer` 前后加更细计时）。
4. **G2 帧率**：先用**普通构建**复测确认基线（探针构建会测低）；再评估
   `capture` 的 CPU 回读能否改走 GPU（改动面较大，用户要求渲染改动小，需先确认收益）。
5. **`SystemWatchTimerTimer` 卡顿**：在 `cpp/core/environ/win32/SystemControl.cpp` 的
   `DeliverEvents()` 与 `TickBeat()` 循环内加 MarkStage（当前内层阶段一条都不触发）。
6. **千恋万花**：按**方案 B** 搬参考的 `D3DEmote.tjs`（规格见 §6.1）→ 字体/logo 颜色与 7 个标题 hook；
   `wave` 转场需**按 GPU render method 重做**（CPU 扫描线移植已证实不适用，见 §6.1）。
7. **兼容层**：若用户回了 **I3**，接完 `mountSiblingsForArchiveProject`（M1 收尾）；
   否则继续 **M6 小模块批次**（每批 2–4 个，机械可验证）。
8. **C3 已阻塞**于 C2；壳侧见 **`SHELL_HANDOVER.md §7`**（自定义按键浮层 → 引擎菜单侧边栏）。
