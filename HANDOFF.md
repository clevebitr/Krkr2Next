# 交接文档（KrKr2Next / KiriNext）

> 用途：把**当前目标、已落地的东西、未完成项、硬约束、验证办法**交给下一个上下文。
> 读这份之前先读根目录 `AGENTS.md`（项目级规则）。兼容层的细节事实与逐条差异见
> `compat/README.md`，本文件是"接手第一份"。
> 渲染层的 open issue 明细（证据 + 下一步探针）在 **`compat/recon/render-issues.md`**。

---

## 0. 一句话现状（2026-09-23，第二轮）

**本轮主线：上一轮真机新报的三条问题的修复**（两条引擎、一条壳），均已落地、待真机回归。

已真机确认的（上一轮）：

- **壳：自定义按键浮层 / 光标触控板模式 / 按键自动对齐参考线 / 引擎菜单侧边栏** → 已落地
  （详见 `SHELL_HANDOVER.md`）
- **壳：详情页左右分栏（封面左；标签→简介→按钮右）、库页/详情页 MD3 细节** → 已落地
- **触控板光标不显示** → 已修（§1.9.1：SurfaceView 监听按值捕获普通参数）
- **侧边栏点菜单项 SIGABRT（NEKOPARA 4）** → 已修（§1.9.2：`EAbort` 逸出 `engine_tick`）
- **classic 层缺 A 块常量回退（おっぱいスパイ学園 `Member "llsUserDirs"` 起不来）** → 已修（§1.9.3）

**本轮已修（代码已提交，待真机回归）**：

1. **おっぱいスパイ学園 切 CG 视频严重卡顿** → 已修（§1.10.1）：根因不是“解码线程不响应停止
   信号”，而是**根本没人叫停播放线程** —— `Release()` 等的是一个循环条件永不为假的线程。
2. **猫娘乐园（NEKOPARA 4）E-mote/Live2D 立绘加载不出** → 已修（§1.10.2）：`PSBMedia` 按
   第一个 '/' 切档案名，把 `lzfs:/x.psb/...` 切成假档案 `lzfs:`。
3. **壳：加载游戏时自动显示日志浮层、进游戏后自动关闭** → 已落地（`SHELL_HANDOVER.md §7`）。

**当前未解决**（详见 `compat/recon/render-issues.md`）：

1. **チート緊縛術（classic 层）`Member "showLayers" does not exist` → 引擎退出**：
   脚本层成员缺失（`showLayers` 在本仓库与 AetherKiri 都未注册）；该作带 `patch.xp3`
   + Claude 翻译补丁，疑似补丁替换的 `mainwindow.tjs` 少了该函数（§6.1）
2. **チート緊縛術（AetherKiri 层）字体渲染不正确**：**缺日志**，需要用户提供 AetherKiri
   层那次 `engine-*.log`（现目录里只有 classic 层那次）
3. 旧账未动：**G2 进动画卡 4.4s / 帧率 43–45**、**千恋万花 `wave` 转场缺失 + SD/logo 交付**、
   **`SystemWatchTimerTimer` 卡顿**

其余两条目标的状态：

1. **兼容层**：`cpp/core/io/` 单一 IO 组件已建成；A 块（含 classic 常量回退）、
   C1、C4、B1（部分）、B2 已完成；`kag` 渲染档已删除（能力归 AetherKiri 层）。
   剩余 C3（阻塞于 C2）、C5/C6/C7、E1、M6 分批，以及**一项等用户裁决的 M1 尾巴（I3）**。
2. **壳（Kotlin/Compose）**：`SHELL_HANDOVER.md` 里的清单**已全部落地**（含本轮最后一项）。

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

## 1.8 引擎无响应/崩溃后的强制退出（壳侧，2026-09-23）

**现象**：游戏卡死或崩溃后引擎不会被拆掉，残留的渲染线程/原生全局状态（EGL、TJS
运行时）让后续游戏都打不开，只能手动杀后台重进模拟器。

**原因**：`EngineSession.shutdown()` 把 `engineDestroy` post 到渲染线程就立即把
`thread/handler` 置空。渲染线程若卡在 native 里（死循环/死锁），那个 runnable 永远
排不进它的 Looper，而 `launchPath()` 已经接着开了新会话——两条线程抢同一份全局原生状态。

**做法**（分层，能优雅就优雅，不能就重启进程）：
1. `EngineSession.stalledMs()`：渲染线程每帧更新心跳（`lastTickNanos`），卡住就不再更新。
   **模态对话框期间豁免**（新增 `engine_is_modal_active()` ABI）：`Window.showModal`
   会让 `engine_tick` 阻塞在嵌套循环里，用户把弹窗开着不动不是卡死。
2. `EngineSession.shutdown(timeoutMs, onDone)`：带看门狗，2.5s 内渲染线程没退出就
   回调 `onDone(false)`。
3. `MainActivity.launchPath()` **等旧会话确实拆掉再开新的**；不干净就 `restartProcess()`。
4. 看门狗协程：`stalledMs() ≥ 10s` → `forceExitGame()`（日志/Toast + 强拆，必要时重启）。
5. `onFatal`（启动失败）→ 弹只能"返回游戏库"的对话框；`onEngineUnresponsive`
   （连续 120 帧 tick 报错）→ 同样强制退出。
6. 悬浮菜单里加"强制退出"（error 色 + 二次确认），"退出游戏"也改成 error 色 + 二次确认。

**重启是最后手段**：渲染线程卡在 native 里时，除了结束进程没有可靠的恢复手段；
不重启的话下一次开游戏必然失败（用户现在的做法就是手动杀后台）。

---

## 1.9 本轮（2026-09-23）落地的引擎侧修复

### 1.9.1 触控板光标不显示（壳侧）

`AndroidView` 的 `factory` **只跑一次**，其 `setOnTouchListener` 闭包**按值**捕获了普通参数
`touchpadMode`（永远是最初的 `false`），于是触控板触摸分发从未生效、光标一直停在 `0,0`
（半个圆点在屏幕外，看起来像"没有光标"）。修法：监听改读 `rememberUpdatedState` 的最新值；
并在模式开启时把光标放到画面中央（`TouchpadState.prime`）。

> 教训：`AndroidView`/`remember` 的闭包里引用 **Compose 状态**（`by mutableStateOf`）没问题，
> 引用**普通参数**必须经 `rememberUpdatedState`。

### 1.9.2 侧边栏点菜单项 → SIGABRT（`EAbort` 逸出 `engine_tick`）

真机（NEKOPARA 4，classic 层）：
```
FATAL SIGNAL 6
[8] TVPShowScriptException ← [9] TVPPostEvent ← [10] tTJSNI_BaseMenuItem::OnClick
 ← [11] TVPInvokeMainWindowMenuItem ← [13] engine_tick
```
根因：`EAbort` **只在 `Application::Run()` 的 try 里被接住**（`environ/Application.cpp:452/604`），
而菜单触发跑在 `engine_tick` 早期、在 `Application->Run()`（同文件 :2143）之前；原实现直接调
`item->OnClick()` 同步跑脚本 onClick，脚本一抛异常，`throw EAbort` 就穿过 JNI 边界 →
`std::terminate` → abort。
修法（`cpp/core/visual/impl/MenuItemImpl.cpp` + `bridge/engine_api/src/engine_api.cpp`）：
- 改为**投 `tTVPOnMenuItemClickInputEvent` 输入事件**，交给引擎自己的事件派发（在 `Run()` 的
  try 内）执行；整段包 `try/catch`。
- 菜单快照改为**按需刷新**（只有壳调过 `engine_list_window_menu` 才在下一次 tick 读菜单树）：
  菜单注册表 `MENU_LIST` 按窗口指针索引、跨会话可能残留陈旧项，游戏不开侧边栏就完全不该碰它。

### 1.9.3 classic 层补上 A 块「常量回退」（用户 2026-09-23 裁决）

真机（おっぱいスパイ学園，classic 层）：`Member "llsUserDirs" does not exist` @ `initialize.tjs`
→ `游戏请求退出（TVPExitApplication）`；切 aetherkiri 层则正常（`startup state 2`、`fps 120`）。

根因：`llsUserDirs` 等常量在 `cpp/core/tjs2/tjsObject.cpp` 的启动期回退里，而整张表由
`TJSCompatFallbacksEnabledFlag` 门控，`cpp/core/compat/CompatLayer.cpp` 只在 AetherKiri 层置 true。

修法：把 A 块回退**拆成两档**：
- **常量回退**（`archiveUniqueKey`/`inXP3archivePacked`/`llsDllLoadDir`/`llsApplicationDir`/
  `llsUserDirs`/`llsSystem32`/`llsDefaultDirs`/`kirikiriz`/`kirikiriz_generic`/
  `debugWindowEnabled`/`developMode`）→ 新增 `TJSSetCompatConstantFallbacksEnabled`（缺省 true），
  **两层都给**；
- no-op 函数、空 `ShortCut` 键表、`touchImage`、`TextRender.renderCount`、34 名全局回退 →
  仍只给 AetherKiri 层（它们会改变"未定义成员就报错"的语义）。

裁决已记入 `compat/README.md §5`。同时 `DetectCompatProfileByMarkers` 除 `plugin/` 外**也看游戏根目录**。

---

## 1.10 本轮（2026-09-23 第二轮）引擎侧修复

### 1.10.1 切 CG 视频卡 1.5–4.7s（おっぱいスパイ学園）—— `cpp/core/movie/ffmpeg/`

现象：每次切视频 `frame_perf update_max=4382/4553/4731ms`、fps 掉到 3–18，每次都打
`movie: 影片线程 4000ms 未退出，放弃销毁并泄漏该影片对象`。

根因：上一轮的兜底只做了「有界等」，**没有任何路径叫停播放线程**。
`BasePlayer::Process()` 的循环条件只有 `m_bAbortRequest`，而该标志原先只在
`CloseInputStream()` 里置位，`CloseInputStream()` 又只从 `~BasePlayer` 调用 ——
等的是一个没人叫停的线程，必然等满整个窗口然后泄漏。

修法：
- 新增 `BasePlayer::RequestStop()`（置 `m_bAbortRequest`、`m_pDemuxer->Abort()`、唤醒
  `m_ready`；`m_bAbortRequest` 改 `std::atomic`）。`Release()` 先请求再有界等待，并打一条
  `movie: 停播请求→影片线程退出耗时 Nms`；`~TVPMoviePlayer` / `~MoviePlayerOverlay` 也先请求（幂等）。
- `CDVDMessageQueue`：`Put`/`Abort` 先自增唤醒序号再在 `m_mtxEvent` 上通知，`Get` 同锁求值谓词
  （去掉丢唤醒、只能空等 timeout 的路径）；`CThread::StopThread` 在锁内改 `m_bStop`。

验收：连续切 8 段 CG，`update_max` 不再出现 4000ms 量级，且不再出现「未退出，放弃销毁」。

### 1.10.2 NEKOPARA 4 E-mote 立绘（`cpp/plugins/psbfile/PSBMedia.cpp`）

现象：`drawFallback: trying psb://lzfs://./e-mote…psb/motion/…` 之后
`PSB lazy-load error: Not supported media type "" (lzfs:)` ×3230，立绘全空。

根因：`tryLazyLoadArchive()` 按**第一个 '/'** 切档案名；嵌套存储名 `psb://lzfs://./x.psb/motion/…`
经存储层规范化后是 `lzfs:/x.psb/motion/…`，于是切出假档案名 `lzfs:`。

修法：移植 AetherKiri 的 `ArchiveBoundaryKey()`（先按 `.mtn/`/`.psb/`/`.pimg/` 扩展名定位边界，
找不到才退回第一个 '/'）。`lzfs:/x.psb/…` 因此切出 `lzfs:/x.psb`，`TVPCreateStream` 会把它
还原成 `lzfs://./x.psb`。同一处也修掉了子目录档案（`motion/mono_loop.mtn/…`）被切成 `motion`。

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
| 游戏中右下角按钮 → 右侧悬浮侧边栏显示**引擎注册的窗口菜单** | ✅ 2026-09-23（`engine_list_window_menu` / `engine_invoke_window_menu` + JNI + `ui/EngineMenuSidebar.kt`），规格见 `SHELL_HANDOVER.md §4` |
| 光标触控板模式（模拟触控板驱动光标） | ✅ 2026-09-23（`ui/Touchpad.kt`；每游戏可覆盖，悬浮菜单可切） |
| 触控板光标不显示 | ✅ 2026-09-23（SurfaceView 监听按值捕获普通参数，改经 `rememberUpdatedState`；并在模式开启时把光标放到画面中央） |
| 按键布局自动对齐参考线 | ✅ 2026-09-23（`ui/KeyPadOverlay.kt` 的 `snapPosition`） |
| 右下角按钮抽屉（引擎菜单/更多收进抽屉，自动隐藏） | ✅ 2026-09-23（`ui/GameScreen.kt`） |
| 引擎菜单侧边栏可折叠展开 | ✅ 2026-09-23（`core/EngineMenu.kt` 的 `parseTree` + `ui/EngineMenuSidebar.kt`） |
| 退出/强制退出（红色 + 二次确认） | ✅ 2026-09-23（`ui/GameScreen.kt`） |
| 引擎无响应看门狗 + 强制退出/进程重启 | ✅ 2026-09-23（`EngineSession.stalledMs/shutdown(onDone)` + `MainActivity.forceExitGame/restartProcess`；新增 `engine_is_modal_active` 豁免模态） |
| 加载游戏时自动显示日志浮层、进游戏后自动关闭 | ✅ 2026-09-23（`debug.auto_log_on_launch` + 每游戏覆盖；`GameScreen` 的 `autoShowLogs`，规格见 `SHELL_HANDOVER.md §7`） |

> **壳的开发交接文档是 `SHELL_HANDOVER.md`**（本地编译闭环、文件/接口索引、各项功能的
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
| おっぱいスパイ学園 **切 CG 视频严重卡顿** | 中 | ✅ **已修（§1.10.1），待真机回归**。根因是 `Release()` 等的是一个**没人叫停**的播放线程（`m_bAbortRequest` 只在 `~BasePlayer` 里置位），必然等满 4s 并泄漏 |
| チート緊縛術（classic）**`Member "showLayers" does not exist` → 引擎退出** | 小-中 | 脚本层成员缺失：`showLayers` 在本仓库与 AetherKiri 都**未注册**（`grep -rn showLayers cpp/` 两边都空）。日志：`trace : mainwindow.tjs(5777)[(function expression)] <-- conductor.tjs(440)[onTag]`、`scenario.ks 行 223 タグ eval`。该作目录带 `patch.xp3` + `claude-3-5-sonnet-…翻译补丁备份` + `hook.ini` + `FONTCHANGER.dll`（加载失败），**疑似翻译补丁替换的 `mainwindow.tjs` 少了该函数**。需要用户提供 `data.xp3>mainwindow.tjs` 与 `patch.xp3` 里的同名文件对照 |
| チート緊縛術（AetherKiri）**字体渲染不正确** | 小-中 | **缺日志**：该游戏目录里只有 classic 层那次 `engine-*.log`。要 AetherKiri 层那次的 `FontSystem: 已注册字体 N 个`、`font_fallback_mode=`、缺字/`GetBeingFont` 行。该作自带 `ShiraYukiNoa.otf` + `FONTCHANGER.dll`（本引擎加载失败）⇒ 字体很可能靠该插件换 |
| **猫娘乐园（NEKOPARA 4）游戏内 E-mote/Live2D 立绘加载不出** | 中 | ✅ **已修（§1.10.2），待真机回归**。`PSBMedia::tryLazyLoadArchive()` 按第一个 '/' 切档案名，把 `lzfs:/x.psb/...` 切成假档案 `lzfs:`；已换成 AetherKiri 的 `ArchiveBoundaryKey()`（按 `.psb/` 等扩展名定位边界） |

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
| 壳：**加载游戏时自动显示日志浮层、进游戏后自动关闭** | 小 | ✅ 已完成（`SHELL_HANDOVER.md §7`） |

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
| **影片停播/销毁** | `cpp/core/movie/ffmpeg/`（`BasePlayer::RequestStop()`、`CDVDMessageQueue` 可中断等待、`TVPMoviePlayer::Release()` 先请求再有界等待） |
| **PSB 档案边界** | `cpp/plugins/psbfile/PSBMedia.cpp` 的 `ArchiveBoundaryKey()`（按 `.mtn/`/`.psb/`/`.pimg/` 定位边界，移植自 AetherKiri） |
| 层专属插件 | `cpp/plugins/compat/aetherkiri/` |
| 壳 UI | `app/app/src/main/kotlin/org/dpdns/clevebitr/ui/` |
| 每游戏日志 | `app/.../core/LogFiles.kt`、`core/EngineSession.kt` |

---

## 9. 下一轮建议顺序

1. **真机回归本轮三项修复**（都要看日志，不能只看“感觉好了”）：
   - おっぱいスパイ学園连续切 8 段 CG：`update_max` 不再出现 4000ms 量级、不再出现
     「未退出，放弃销毁」、出现 `movie: 停播请求→影片线程退出耗时 Nms`（N 应远小于 4000）；
   - NEKOPARA 4：立绘显示出来、`PSB lazy-load error: Not supported media type ""` 归零、
     每个档案一条 `PSB lazy-load archive: lzfs:/e-mote*.psb`；
   - 壳：打开「加载游戏时自动显示日志」→ 开游戏立即看到日志、`startup state -> 2` 后自动消失。
2. **チート緊縛術**：等用户给 AetherKiri 层日志（字体）+ `mainwindow.tjs`/`patch.xp3` 对照
   （`showLayers`）。
3. **NEKOPARA 翻转动画位置**（上一轮已修）：真机回归确认 `*c1*`/`*c2*` 变体位置。
4. **千恋万花**：按**方案 B** 搬参考的 `D3DEmote.tjs`（规格见 §6.1）→ 字体/logo 颜色与 7 个标题 hook；
   `wave` 转场需**按 GPU render method 重做**（CPU 扫描线移植已证实不适用，见 §6.1）。
5. **G2**：进动画 4.4s（`CreateRenderer` 计时细分）→ 帧率（普通构建复测基线）。
6. **`SystemWatchTimerTimer` 卡顿**：`SystemControl.cpp` 的 `DeliverEvents()`/`TickBeat()` 加 MarkStage。
7. **兼容层**：若用户回了 **I3**，接完 `mountSiblingsForArchiveProject`（M1 收尾）；
   否则继续 **M6 小模块批次**。C3 已阻塞于 C2。
