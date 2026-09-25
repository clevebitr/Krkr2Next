# 交接文档（KrKr2Next / KiriNext）

> 用途：把**当前目标、已落地的东西、未完成项、硬约束、验证办法**交给下一个上下文。
> 读这份之前先读根目录 `AGENTS.md`（项目级规则）。兼容层的细节事实与逐条差异见
> `compat/README.md`，本文件是"接手第一份"。
> 渲染层的 open issue 明细（证据 + 下一步探针）在 **`compat/recon/render-issues.md`**。

---

## 0. 一句话现状（2026-09-23，第三轮）

**本轮主线：用户 2026-09-23 17:56–17:57 两次真机日志里的四条问题**（千恋万花 SD CG / 字体 /
logo 动效 + NEKOPARA 4 立绘），已定位到**一个共同根因**（§1.13.1）+ 一个引擎缺口（§1.13.2），
代码已落地、待真机回归。

已真机确认的（上一轮）：

- **壳：自定义按键浮层 / 光标触控板模式 / 按键自动对齐参考线 / 引擎菜单侧边栏** → 已落地
  （详见 `SHELL_HANDOVER.md`）
- **壳：详情页左右分栏（封面左；标签→简介右；主按钮全宽横排在最下）、库页/详情页 MD3 细节** → 已落地
- **壳：UI 预览层独立到 `app/app/src/debug/kotlin/.../ui/preview/`**（只 debug 变体编译，release 不带一行） → 已落地（`SHELL_HANDOVER.md §1.1`）
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
3. 旧账未动：**G2 进动画卡 4.4s / 帧率 43–45**、**千恋万花 `wave` 转场缺失**（需按 GPU render
   method 重做）、**`SystemWatchTimerTimer` 卡顿**；千恋万花 SD/logo 与 NEKOPARA 立绘已归入
   本轮 §1.13.1。

其余两条目标的状态：

1. **兼容层**：`cpp/core/io/` 单一 IO 组件已建成；A 块（含 classic 常量回退）、
   C1、C4、B1（部分）、B2 已完成；`kag` 渲染档已删除（能力归 AetherKiri 层）。
   剩余 C3（阻塞于 C2）、C5/C6/C7、E1、M6 分批，以及**一项等用户裁决的 M1 尾巴（I3）**。
2. **壳（Kotlin/Compose）**：`SHELL_HANDOVER.md` 里的清单**已全部落地**（含本轮最后一项）。

工作区除用户自己的 `.gitignore`/`README.md` 外干净（那两个文件**始终不要 add**）。

**开发环境（2026-09-25）**：模拟器上「装完 APK 一启动就 SIGABRT、进不去程序」的根因已定位并修复
（Berberis 翻译层给 arm64 guest 的伪 `/proc/cpuinfo` 触发 libomp 拓扑断言）——见 **§1.14**。
真机 arm64 没有这个翻译 bug，行为不变。

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

根因（**三轮**才定位准，前两轮均已被真机证伪）：
1. `Release()` 先有界等 4s 再销毁，但**没有任何路径叫停播放线程**（`m_bAbortRequest`
   只在 `CloseInputStream()` 里置位，而它只从 `~BasePlayer` 调）。
2. 补了 `AbortPictureWait()` 后仍卡满 4s —— 因为真因不是等待逻辑，而是**跨线程死锁**：
   - 渲染线程：`engine_tick` **整帧持有 `g_registry_mutex`** → 脚本 → `VideoOverlay.Close()`
     → `Release()` → 等播放线程退出；
   - 解码线程：`CRenderManager::DiscardBuffer()` → `TVPMoviePlayer::Flush()`，
     **持着 `m_mtxPicture` 调 `spdlog::info`** → `StartupLogSink` 要取 `g_registry_mutex`
     （`engine_api.cpp:748`）→ 永久阻塞。
   于是解码线程回不到 `Process()` 顶部看中断，`StopThread()` 的 join 永远回不来。
   **只在切换时发生 seek（有 Flush）的那些切换上复现** —— 没有 Flush 就没有锁内日志。

修法：
- **所有 `m_mtxPicture` 临界区改成“锁内只取值，日志在锁外打”**（与 `g_movieStatsMutex`
  同一条纪律）：`Flush`/`AddVideoPicture`/`PresentPicture`/两个 `OnContinuousCallback`。
- 新增 `BasePlayer::RequestStop()`（置 `m_bAbortRequest` + `m_bStop` + 中断 demuxer）；
  `Release()` 先 `AbortPictureWait()` + `RequestStop()` 再有界等待。
- `OutputPicture()` 在 `m_bStop` 时直接返回 `EOS_ABORT`（它开头会把 `m_bAbortOutput` 清掉）。
- `CDVDMessageQueue` 等待改为可中断；`StallWatchdog` 的影片阶段拆成
  **player / video / audio 三个槽位**（`.stall` 四行，能直接回答“哪条线程卡在哪”）。

验收：连续切 8 段 CG，`update_max` 不再出现 4000ms 量级，且不再出现「未退出，放弃销毁」。

### 1.10.2 NEKOPARA 4 E-mote 立绘（`cpp/plugins/psbfile/PSBMedia.cpp`）

现象：`drawFallback: trying psb://lzfs://./e-mote…psb/motion/…` 之后
`PSB lazy-load error: Not supported media type "" (lzfs:)` ×3230，立绘全空。

根因：`tryLazyLoadArchive()` 按**第一个 '/'** 切档案名；嵌套存储名 `psb://lzfs://./x.psb/motion/…`
经存储层规范化后是 `lzfs:/x.psb/motion/…`，于是切出假档案名 `lzfs:`。

修法：移植 AetherKiri 的 `ArchiveBoundaryKey()`（先按 `.mtn/`/`.psb/`/`.pimg/` 扩展名定位边界，
找不到才退回第一个 '/'）。`lzfs:/x.psb/…` 因此切出 `lzfs:/x.psb`，`TVPCreateStream` 会把它
还原成 `lzfs://./x.psb`。同一处也修掉了子目录档案（`motion/mono_loop.mtn/…`）被切成 `motion`。

真机已确认生效（`engine-20260923-000530.log`）：错误计数 3230 → **0**、
`drawFallback: no image loaded` 2321 → **0**，出现 `PSB lazy-load archive: lzfs:/e-mote*.psb` ×8
与 `Stored 12 layer positions`；`drawAnimated` 2600 次。

---

## 1.11 图形设置（本轮新增，用户要求）

**结论：这些高级图形功能引擎里早已实现（与 AetherKiri 一致），缺的是壳暴露 + 一条能
真正生效的通道。** 对照结果（两边的引擎配置键几乎相同，KiriNext 还多一个
`font_fallback_mode`）：

| 键 | 含义 | 取值 |
|---|---|---|
| `ogl_compress_tex` | 纹理压缩 | `none` / `half` / `etc2` / `pvrtc` |
| `software_compress_tex` | 软件渲染的纹理压缩 | `none` / `halfline` / `lz4` / `lz4+tlg5` |
| `ogl_accurate_render` | 精确渲染 | 布尔 |
| `ogl_max_texsize` | 最大纹理尺寸 | 整数，0 = 不覆盖 |
| `memusage` | 内存占用档 | `unlimited` / `low` / `medium` / `high` |
| `software_draw_thread` | 软件绘制线程（仅 `renderer=software`） | 整数 |

`astcrt.cpp` / `etcpak.cpp` / `imagepacker.cpp` / `pvrtc.cpp` 也都已在
`cpp/core/visual/CMakeLists.txt` 里编着。

### 两处真正的缺口

1. **通道是断的**：`engine_set_option` 结尾的通用分支只把键写进**命令行参数**
   （`TVPProgramArguments`），而渲染层这些键是通过 `IndividualConfigManager::GetValue`
   从 `Kirikiroid2Preference.xml` / 全局配置读的（`TVPGetCommandLine` 只认 `-xxx` 与
   `renderer`）。
   修法：新增“壳选项覆盖”表（`GlobalConfigManager.h` 的 `TVPSetShellOption` 等），由
   `IndividualConfigManager` 的四个 `GetValue<T>` 特化**最高优先级**查询。
   **不能**用 `SetValue()` 写进 `AllConfig`：`UsePreferenceAt(游戏目录)` 会先 `Clear()`
   再 `Initialize()`，壳在开游戏前设的值会被整份清掉。
2. **渲染器是进程级单例**：`TVPGetRenderManager(name)` 把实例缓存在工厂表里，而这些
   选项原先只在 `InitGL()` / `static` 局部里读一次 ⇒ “每游戏一套设置”只对第一个游戏
   生效。修法：惰性重算 + 可失效（`TVPInvalidateGraphicsOptionCaches()`，由
   `engine_set_option` 调用）：
   - `ogl_compress_tex`：`_CreateStaticTexture2D` 固定指向
     `CreateStaticTexture2D_auto`，每次创建静态纹理时按档位分发（带 GL 扩展校验）；
   - `ogl_max_texsize`：`GetMaxTextureWidth/Height` 惰性叠加用户上限；
   - `ogl_accurate_render`：收敛成 `TVPIsAccurateRenderEnabled()` 一处缓存，
     `LayerIntf.cpp` 的 `IsGPU()` / `LayerBitmapImpl.cpp` 的 `fastGPURoute` 不再各自存
     `static`。

### 壳侧

`core/GraphicsConfig.kt`（模型 + JSON）、`AppPrefs.graphicsConfig`（全局默认）、
`GameConfig.graphics`（null = 继承）、`ui/GraphicsConfigEditor.kt`（全局页与游戏页共用）、
`EngineSession.graphics`（`start()` 与 `openGame()` 里下发，只发非默认项）。

验收：设置 → 图形 改纹理压缩为 `etc2`，开游戏日志应有
`engine_set_option: ogl_compress_tex=etc2`；**换游戏后仍生效**（不必重启应用）。

---

## 1.12 千恋万花：字体颜色与 SD 交付（本轮照搬上游，待真机回归）

### 1.12.1 消息文字颜色（`Layer.drawTextVerticalGradient` + 脚本源码改写）

根因：`EdgeShadowDrawText` **不是引擎 API，而是游戏自带 `custom.tjs` 里的函数**，它用
「渐变图层 + `operateRect`」画字（顶 `0xFFFFFF` → 底 `col`），在本引擎里渲染成纯白。
上游的解法是**在脚本加载时改写这段源码**，换成一次原生渐变文字绘制。

两件都补齐了：
- `visual/FontBaseline.h` 补 `ComputeGlyphOriginY` / `ClampTextOriginToClipTop` /
  `ComputeTextShadowTopPadding`；`impl/LayerBitmapImpl.*` 实现
  `InternalBlendTextVerticalGradient` / `DrawTextVerticalGradient`
  （混合取上游的**软件分支**写法：逐行复用 `InternalBlendText`，它自带 GPU/软件两条路径的选择，
  于是不必再引入上游那条 `AlphaBlend_d` + scratch 纹理的批量分支）；
  `LayerIntf.*` 加 `tTJSNI_BaseLayer::DrawTextVerticalGradient` 与原生方法
  `Layer.drawTextVerticalGradient`（`TJS_END_NATIVE_METHOD_DECL` 自注册，不用改方法表）。
- `base/ScriptMgnIntf.cpp`：新增 `TVPApplyScriptCompatibilityPatches(shortname, buffer)`，
  在 `TVPExecuteStorage` 读完源码、`ExecScript` **之前**调用；首个补丁是 `custom.tjs` 的
  `EdgeShadowDrawText` 块替换。匹配用**结构化锚点**（函数签名 + 块内 `MakeGradationLayer`
  与 `d.operateRect` 特征串 + 括号配对），**不用行号** —— 整合包/汉化版会增删行。

日志判据：`Applied compatibility patch for native gradient text drawing (custom.tjs)`。

### 1.12.2 SD CG 不可见（方案 B：D3DEmote.tjs 覆盖）

四件工作全部落地：
1. `cpp/core/compat/resources/D3DEmote.tjs`（上游 1340 行，逐字节一致，已登记进移植清单
   `modifications: none`）；compat/CMakeLists.txt 按上游同款 `configure_file` + hex 字节数组
   生成 `D3DEmote_tjs.cpp`。
2. `io/IoVirtualFile.*` 新增**覆盖型 provider**（排在物理存储**之前**），
   `IoStorage.cpp` 的 `TVPGetPlacedPath` 与 `_TVPCreateStream` 各加一处判定。
   **为什么必须有这一档**：游戏自带的 `motion.tjs` 是**真实存在**的，兜底型 provider 永远
   不会被问到（“物理优先”是普通 provider 的契约）；按 §5 的裁决，覆盖只针对
   `motion.tjs` / `d3demote.tjs` 两个明确列出的名字。注册在 `AetherKiriCompanions.cpp`，
   **只对 AetherKiri 层生效**。
3. `plugins/motionplayer/main.cpp` 的 `D3DAdaptor` 类补
   `setPresentationTarget` / `clearPresentationTarget` / `presentationHold` /
   `removeAllTextures`。脚本 `drawAffine` 的写入路径是
   `if (!presentationHold) { _redrawImage(work); assignMotionImages(work) }`；参考实现会在
   原生 player 冷替换时把 hold 置真（需要它自己的渲染 surface），本壳没那条路径，
   所以 `presentationHold` **恒为 false ⇒ 每帧都交付**。
4. 清理（删方案 A 的兜底）留到真机确认方案 B 生效之后再动。

日志判据：`compat companion: 虚拟提供 motion.tjs [d3demote-override]`。

> ⚠️ **2026-09-23 第三轮真机证伪：这一条注册方式是错的**（见 §1.13.1）。
> 覆盖型 provider 把**游戏自带的** `system/motion.tjs` 顶掉了，而那个脚本定义了全局类
> `MotionResourceManager`，D3DEmote.tjs 又反过来 `new global.MotionResourceManager(...)`
> 依赖它 —— 结果千恋万花与 NEKOPARA 的 MTN/PSB 图像全部加载失败。已改回**兑底型**
> （物理/auto-path 优先，与上游 `!TVPIsRealStorageNoSearchNoNormalize` 同语义），并且把
> io 的“覆盖型 provider”整档删除。上方“日志判据”应读作 `compat companion: 虚拟提供
> d3demote.tjs [d3demote-script]`（只在游戏自己没有这个脚本时）。

---

## 1.13 第三轮（2026-09-23）：两条真机新报问题的根因修复

### 1.13.1 千恋万花 SD CG / m2logo 动效 / NEKOPARA E-mote 立绘：同一个根因（§1.12.2 的覆盖）

**现象**（用户 2026-09-23 17:56–17:57 两次真机日志）：

- 千恋万花：SD CG 不显示、m2logo 开场动效颜色异常（字体另见 §1.13.2）；
- NEKOPARA 4：E-mote 立绘不显示（上一轮 §1.10.2 已把 `PSB lazy-load error` 归零，
  这次是“图像加载正常但立绘不出”之外的另一种表现）。

**证据**：两次日志里同一条 TJS 异常反复出现（千恋万花 32 条、NEKOPARA 6 条）：

```
==== An exception occurred at motion.tjs(604)[(function) _loadImages], VM ip = 179 ====
#(604)  _motion_manager = new global.MotionResourceManager(_window);
Member "MotionResourceManager" does not exist at motion.tjs(604)[(function) _loadImages]
```

并且伴随 `script exception … at affinelayer.tjs(1)[(function) loadImages]`、`画像ロード失敗`。
千恋万花那次 `drawFallback: no image loaded` 归零的“好结果”是上一轮（覆盖之前）的。

**根因**：`MSGHACK`/Yuzusoft 的 `system/motion.tjs`（数据包里是**编译字节码**，标识符表可见
`MotionResourceManager` / `AffineSourceMotion.tjs` / `Motion.ResourceManager` / `lzfs://./`）
是 Motion 库本体：它定义全局类 `MotionResourceManager` 并 `execStorage("AffineSourceMotion.tjs")`。
而参考实现的 `D3DEmote.tjs` **依赖**这个全局类（`new global.MotionResourceManager(_window)`）。
方案 B 用覆盖型 provider 把这个文件顶掉，等于把库本体换成了库的**使用者**，于是所有走
`MotionResourceManager` 的图像加载（SD `sdNNN.mtn`、`m2logo.mtn`、`e-mote*.psb` 立绘）全部抛异常。

**参考实现到底怎么做**（读 AetherKiri `cpp/core/base/StorageIntf.cpp`）：
`TVPIsD3DEmoteCompanionScript` 的两个消费点（`TVPGetPlacedPath` 2023 行附近、
`_TVPCreateStream` 2178 行附近）都带 `!TVPIsRealStorageNoSearchNoNormalize(name)` —— 即
**物理/auto-path 命中时虚拟脚本永远不参与**；D3DEmote.tjs 是给“游戏自己没有这个脚本”的
作品用的伴生脚本（它的另一个真实用途：游戏 `D3DaffineSourceEmote.tjs` 会去要
`D3DEmote.tjs`，而数据包里没有）。参考实现对千恋万花这类游戏改的是**加载后**打补丁
（`TVPGetD3DEmoteGpuBatchPatchScript`，包 `AffineSourceMotion.drawAffine` 做 GPU 批处理），
不是换文件。

**做法**：`AetherKiriCompanions.cpp` 把 D3DEmote 从覆盖型 provider 改成普通（兑底）provider
（命中日志 `d3demote-script`）；`IoVirtualFile.{h,cpp}` 删掉覆盖型 provider 那一档，
`IoStorage.cpp` 的三处消费点回到“只查物理 → auto-path → 最后虚拟兑底”。`D3DAdaptor` 的
`setPresentationTarget` / `presentationHold` 等成员保留（D3DEmote.tjs 一旦被用到仍需要）。

**验收标准**（真机日志）：

- 不再出现 `Member "MotionResourceManager" does not exist` 与 `画像ロード失敗`；
- 千恋万花应看到 `motion.tjs を読み込みました`（游戏自己的那份，字节码）+ 首帧里的
  `AffineSourceMotion.tjs`；SD 与 m2logo 有图像；
- NEKOPARA 4：`e-mote*.psb` 立绘出现；
- 若某作品确实没有 `motion.tjs`/`D3DEmote.tjs`，日志里才应出现 `虚拟提供 d3demote.tjs`。

### 1.13.2 千恋万花消息文字颜色：补上原生 `Layer.EdgeShadowDrawText`

**现象**：消息文字颜色异常（§1.12.1 试图用源码改写修的“渲染成纯白”）。

**为何 §1.12.1 在这份整合包上必然不生效**：那个补丁只匹配**明文** `custom.tjs` 里的
`function EdgeShadowDrawText(...) + MakeGradationLayer + d.operateRect` 锚点。但本作的
`patch.xp3` 里 `custom.tjs` 是 147 KB UTF-16 明文且**没有这个函数定义**（只有两处调用；
`grep` 全包也找不到 `function EdgeShadowDrawText`）—— 真正的实现被汉化搬到了
`sysscn/msghack.tjs`，而那是**编译字节码**（`TJS2100` 段，标识符表里有
`Layer` / `drawPathString` / `MakeGradationLayer` / `DrawTextWithGradationColor` /
`EdgeShadowDrawText` / `EdgeShadowDrawTextKinsokuRect`）。真机日志也从没出现
`Applied compatibility patch for native gradient text drawing (custom.tjs)`。

**根因**：`msghack.tjs` 走的是 TextRender 插件的原生契约 —— 先看
`Layer.EdgeShadowDrawTextKinsokuRect` 在不在，在就用原生描边+渐变文字，不在就退回脚本的
“渐变图层 + drawPathString”路径；本仓库的 `textrender.cpp` 从未注册过这两个名字
（`grep EdgeShadowDrawText cpp/` 只命中 `ScriptMgnIntf.cpp` 的补丁与两处注释），所以真机
永远走脚本退化路径。

**做法**：按 AetherKiri `plugins/textrender.cpp`（`EdgeShadowDrawTextCompat`）移植这两个
Layer 方法：认参数里的 text / x / y / col / opa，再加上 `edgeColor` 描边光量（按半径圆内
偏移逐点 `DrawText`）；打包渐变颜色（bit63 置位）走 `Layer.DrawTextVerticalGradient(x, y,
"顶 0xFFFFFF → 底 col")`，普通色走 `DrawText`。命名层文字（5 参调用）保留
`GetFontGlyphDrawRect` + `ClampTextOriginToClipTop` 的顶部裁剪修正。探针用
`KRKR_RENDER_PROBE` 且只记前 8 次、不记文本内容。

**验收标准**：探针构建下出现 `probe: textrender EdgeShadowDrawText len=… x=… y=… color=…`，
且消息文字与设置/回想界面文字颜色正常（不再是纯白）。

### 1.13.3 第四轮（2026-09-23 18:51–18:56 真机日志）：§1.13.1 生效，剩下的是交付/描边

**§1.13.1 已生效**（新日志证据）：`Member "MotionResourceManager" does not exist` 由 32 条降为 **0**；
NEKOPARA `lzfs://./e-mote*.psb` → `PSB lazy-load archive` + `Stored 12 layer positions`；
千恋万花 `sd301.mtn` → `Stored 21 layer positions` / `cachePSBImages: 21 layer positions`，
`TVPLoadGraphic` 在拉 `psb://…/pixel.png`。即“图像加载”这一段已经通了。

但仍然看不到画面，日志指向两条不同的东西：

1. **运动帧被交付到不可见层**（两作同构）：
   - NEKOPARA：首次 `AssignImages` → `target='ショコラ' parent='表-背景(vis=1)'`，
     之后同一层变成 `parent='裏-背景'(vis=0)`；
   - 千恋万花：`target='CG View LayerAffineLayer'`，`parent='CG View Layer : SDxxxAA'(vis=0)`
     （标题图则是 `parent='裏-背景'(vis=0)`，另有一份到 `表-背景` 的 `trans_title_bg`）。
   而 `CG View Layer` / `表-背景` 里都有同名同尺寸的可见兄弟层 —— 正是
   `TVPResolveExchangedKagAssignmentTarget`（KAG 表/裏 页面改投）要处理的形状，但它
   **一条 `LayerAssign route=`/拒绝原因都没打**（该函数当时既无 route 日志也无拒绝原因日志）。
   本轮补上 `probe: exch-route denied reason=…`（`bad-signature` / `no-page-root` /
   `page-visible` / `not-known-stale` / `no-visible-page` / `no-name-match`，每个
   (目标名,原因) 一条，封顶 40），下一份 **探针构建**日志就能定死是哪道门拦的。
2. **文字描边颜色参数路由**（真正的修法）：本作的 `msghack.tjs` 是字节码，
   AetherKiri 对它的处理是“脚本跑完后再包一层 `global.EdgeShadowDrawText`”（把看起来
   是颜色值的 `e`/`ecol` 换回 `owner.edge`/`owner.edgeColor`）。本仓库原先**只有前置
   源码改写、没有后置钩子**，所以这条修复一直缺失。本轮新增
   `TVPApplyPostScriptCompatibilityPatches(shortname)`（移植自 AetherKiri
   `ScriptMgnIntf.cpp`），在字节码/明文两条执行路径**之后**调用；首个钩子就是
   `msghack.tjs` 的那条，日志判据：
   `Applied compatibility hook for message edge argument routing (msghack.tjs)`。

**还需要的下一步（探针构建）**：`Motion.enableD3D` / `Motion.Player.useD3D` /
`window.d3dMotion` 这三个值决定游戏走 D3DAdaptor 的 `captureCanvas` 还是
SeparateLayerAdaptor 的私有渲染层，而它们从来没在日志里出现过。本轮把它们加进
`KRKR_RENDER_PROBE` 快照：`probe: motion d3d decision after motion.tjs: …`
（脚本 `motion.tjs` / `affinesourcemotion.tjs` / `d3daffinesourcemotion.tjs` 执行后各一条）。

相关事实：两作的 `system/motion.tjs`（字节码）都带 `-nod3dm | System | getArgument | yes`
与 `motionplayer_nod3d.dll`；千恋万花自带 `patch.tjs`（明文）会显式
`&Motion.Player.useD3D = 0;`（即作者本意就是不要 D3D 路径），而真机日志显示 D3D
路径仍在跑（`D3DAdaptor.captureCanvas` 累计 901 次，`SeparateLayerAdaptor` 0 次）。

### 1.13.4 第五轮：把参考实现的“隐藏页孤儿层搬回可见页”补齐

真机（19:26/19:27，非探针构建）与上一轮完全同形：NEKOPARA 头两帧交付到
`表-背景(vis=1)`，随后全部变成 `裏-背景(vis=0)`；千恋万花 SD 交付到
`CG View LayerAffineLayer`（`parent='CG View Layer(vis=1)'`，但整条链 `parentVisible=0`），
标题图是 `裏-背景(vis=0)` + 一份 `trans_title_bg`。即图像已加载、帧已画好，但停在隐藏页。

对照参考实现（AetherKiri `LayerIntf.cpp:359-535`）发现本仓库的移植**只搬了一半**：
原版除了“改投到可见页同名同尺寸的兄弟层”之外，还有两道兑底：

1. 可见页里**没有**同名兄弟时，若该隐藏层被**持续交付**（同一目标连续 12 次、相邻两次
   间隔 ≤ 250ms），且可见页没有压得住它的内容层、两页都不在转场中 ⇒ 把**层本身**
   `SetParent` 搬到可见页（保持 order，记入 `TVPMotionSwapAssignmentTargets`）；
2. 可见页里的 `trans_*` 兄弟是即将到来的 crossfade 目标，转场前要**让路**；且后续交付
   要走 `AssignMotionImages` 交换语义（`visible_target == this` 分支）。

本轮把这三块补齐（`LayerIntf.{h,cpp}`：新增 `DebugIsInTransition()` 访问器、
`TVPHiddenKagAssignmentStreaks`/`TVPMotionSwapAssignmentTargets` 状态、
`LayerAssign route=reparent-hidden-page` 日志、`probe: exch-route denied reason=page-busy/
streak-not-reached`）。

### 1.13.9 第十轮：探针给出真数值 —— 缺的是 `setScale`，而仿射是「替代居中」不是「叠加」

真机探针（2026-09-24 20:32/20:34）第一次把游戏的实际调用打出来了：

```
probe: Player.setScale count=1 values=[0.750002]                                   ← NEKOPARA
probe: Player.setRotate count=1 values=[0.000000]
probe: Player.setDrawAffineTranslateMatrix count=6 values=[1,0,0,1,585,705]        ← NEKOPARA ショコラ站位
probe: Player.setDrawAffineTranslateMatrix count=6 values=[1,0,0,1,1335,735]       ← NEKOPARA バニラ站位
probe: Player.setDrawAffineTranslateMatrix count=6 values=[1,0,0,1,960,540]        ← 千恋万花 = 画布中心
```

两个结论：

1. **立绘过大 = `setScale(0.75)` 被丢弃**：游戏用 0.75 把 PSB 原生尺寸缩进画面，
   而本壳的 `setScale` 一直是空实现（`MotionPlayer_ignoreArgs`）。→ 这条必须实现。
2. **上一轮把仿射叠在“已经居中过”的坐标上 ⇒ 双重居中**：千恋万花传的正是
   `translate(960,540)`（= `halfCw/halfCh`），叠加上去等于把所有东西再推 960/540，
   真机就是“主界面与 SD 渲染错位”。仿射是**运动空间 → 画布空间的完整映射**，
   应当**替代**引擎自己的半画布居中。

做法（`motionplayer`）：

- `setScale` 真正实现（`Player::setDrawScale`，1 参=等比、2 参=分轴），在绘制时作用在
  **源坐标**上（x 轴乘 a/c、y 轴乘 b/d）；
- `setDrawAffineTranslateMatrix` 非单位阵时用 `dam ∘ (px,py)` **替代** `halfCw/halfCh`
  居中（单位阵或未设置时行为与旧版逐位一致）；探针改记实际数值。

### 1.13.8 第九轮：合成组判据放宽成 `type==12` —— 普通图层被当蒙版吃掉

对照参考实现发现了更根本的一条差异：合成组（stencil composite）的判据必须是
`node.nodeType == 12 && (node.stencilType & 4) != 0`（krkr2 `PlayerRenderItems.cpp:581`
的蒙版表遍历条件），而且 e-mote 系 PSB 常把 `stencilType` 写成 **0**（`NodeTree.cpp:254`
把 0 归一化为 1，因为有 content 的图层应当正常绘制）。

本仓库 `PSBMedia.cpp` 以前用 `node.type == 12` 一刀切 ⇒ E-mote 树里大量**普通**图层被判定
为合成组：其后所有节点被送进离屏**组层**，收尾又只折叠第一个组的蒙版 ⇒

- 千恋万花 SD 的**背景**（节点名 `SD202/mask`、src=`src/SD202/背景`）被当蒙版吃进蒙版层，
  永远不当作内容绘制 ⇒ “人物出来了、背景没有”；
- NEKOPARA 每帧 `multiple composites (2..6), only first folded`、`stencil mask label
  '■耳L/R' not found` ⇒ 立绘缺件。

本轮把判据改成与参考实现一致（`type==12 && (stencilType&4)`，`stencilType==0` 的普通层不再
参与合成），蒙版名解析同样只在真正的合成组上做（那一处本来就以 `hasStencil` 为门）。

### 1.13.7 第八轮：用户反馈（立绘出来了但过大 / SD 人物出来了背景没有）+ 变换入口被丢弃

**用户真机反馈（a535966 之后）**：

- NEKOPARA 4：立绘**能出来了**（多合成组不再被第一组蒙版抹掉），但**比画面还大**，
  只看到一小部分 → 缩放没生效；
- 千恋万花 SD：**人物出来了，背景没有**。

**变换入口被整批丢弃（本轮修）**：`motionplayer` 里 `setRotate` / `setScale` / `setMirror`
都注册成 `MotionPlayer_ignoreArgs`（空实现），`setDrawAffineTranslateMatrix` 走到
`Player::setDrawAffineTranslateMatrix(...) {}`（空实现），而且入口要求 `count >= 6` ——
而游戏按参考实现的约定**传 1 个 AffineMatrix 对象**（`m11/m21/m12/m22/m14/m24`，
见 `AffineSourceMotion.tjs` 的标识符表），于是调用直接吃 `TJS_E_INVALIDPARAM`（被游戏自己的
try/catch 吞掉），整套缩放/平移全丢 ⇒ 立绘按 PSB 原生尺寸画，超出画面。

做法：

- `main.cpp` 的 `Player_setDrawAffineTranslateMatrix` 按参考实现接受**两种形式**
  （6 个实数 m11,m21,m12,m22,m14,m24 / 1 个 AffineMatrix 对象），转成内部
  `(a,b,c,d,tx,ty)` 存下；
- `Player.h` 新增 `_drawAffineMatrix`（默认单位阵），并在 `operateAffine` 参数组装处把
  它作为**外层仿射**叠到每个条目上（`x''=a·x'+b·y'+tx` 形式，单位阵时与旧行为逐位一致）；
- 探针构建下，`setDrawAffineTranslateMatrix` / `setScale` / `setRotate` 的调用与数值会各记
  一条（`probe: Player.setDrawAffineTranslateMatrix count=… values=[…]`），
  下一份日志就能确认缩放系数与真实入口。

**尚未定位**：千恋万花 SD **背景**缺失（人物已在）。SD301 的节点表里有
`char301/mbg src=src/SD301/背景２`、`char301/bg src=src/SD301/背景`；背景属于哪个合成组/
是否被 `str_clip` 裁掉，需要下一轮日志里的 `drawAnimatedTree` 节点明细来定位。

### 1.13.6 第七轮：非 D3D 路径上线后暴露的真因 —— 私有渲染层是 0×0

`Motion.enableD3D` 恒返回 0 之后（§1.13.5），真机（20:50/20:51）确认选路成功：

- `Motion.enableD3D: 脚本请求开启 D3D 路径，已忽略`；
- `D3DAdaptor.captureCanvas` **归零**、`drawOnto` 归零；
- `motion: SeparateLayerAdaptor 渲染层路由 owner=… parent=… parentIsOwner=1
  parentName='ショコラ' / 'バニラ' / 'ev'` —— 私有渲染层确实挂到了游戏自己的 owner 层下；
- `probe: exch-route denied` 里终于出现 `reason=streak-not-reached` 与 `reason=page-busy`
  （说明 §1.13.4/§1.13.5 补的兜底路径已经可达）。

但画面依旧全黑，日志给出最后一环：

```
drawAnimated: drew 39 images at tick=0 (…) real[layer(name='ショコラ',parent='ショコラ',
    visible=1,opacity=255,count=-1,size=0x0,pos=0,0)]
```

**私有渲染层的 size 是 `0×0`** —— 帧照样“画”，但没有一个像素能落地。原因是本壳只是把
owner 的 `width`/`height` 抄给渲染层，而 owner 是角色的 AffineLayer（NEKOPARA
`ショコラ`/`バニラ`、千恋万花 `ev`），它的 width/height 常为 0；参考实现的
`queryLayerCanvasSize` 会退到 **image 尺寸**并要求非零（拿不到就不建渲染层），随后
`SetSize/SetClip/SetHasImage/SetImageSize` 显式定尺寸。

修法（`motionplayer/main.cpp` 的 `GetSeparateAdaptorRenderTarget`）：尺寸按
owner width/height → owner imageWidth/imageHeight → `window.scWidth/scHeight`
（游戏自己的 `motionWorkLayer` 就是 `setSize(scWidth, scHeight)`）逐级兜底，拿不到就
不建渲染层并警告；然后用 `setSize(canvasW, canvasH)` 一次性定 box+image，补上
`type=ltAlpha(2)`，路由日志带上 `canvas=WxH visible=… type=…` 便于下一轮核对。

**下一轮判据**：`motion: SeparateLayerAdaptor 渲染层路由 … canvas=1920x1080 visible=1
type=2`，且 `drawAnimated: drew N images … size=1920x1080`。

### 1.13.5 第六轮：探针给出答案 —— 文字已修好，剩下的全部是 D3D 路径选错了

**用户真机反馈（probe 构建 de1450b）**：

| 问题 | 现象 |
|---|---|
| 千恋万花 消息文字/字体 | **基本修好**（msghack 钩子 + 原生 `Layer.EdgeShadowDrawText` 生效）；只剩“CG9 页面的音乐名称”一处 |
| 千恋万花 SD CG | 完全看不到 |
| 千恋万花 m2logo | 有色块 / 残留矩形 |
| NEKOPARA 4 立绘 | 完全看不到 |

**探针实测（19:56/19:57 日志）**：

1. `probe: exch-route denied reason=not-known-stale`（NEKOPARA `ショコラ`/`バニラ`）、
   `reason=page-visible`（当前可见页，正常）、
   `reason=no-page-root`（千恋万花 SD：`target='CG View LayerAffineLayer'`，
   `page='CG View Layer'`，`root='裏メッセージレイヤ2'` —— 目标在“消息层页面”里，
   而源工作层挂在 `トップレイヤ`，参考实现的 `source->GetParent() == page_root` 判据
   天然不成立，参考实现也只覆盖 `表/裏-背景` 这一对）。
2. `page-busy` / `streak-not-reached` / `LayerAssign route=reparent-hidden-page` **一次都没有** →
   说明上一轮刚补的“把孤儿层搬回可见页”从来没被执行到。根因是移植时多加了
   `if(!known_stale) return nullptr;`（参考实现只在“同名兄弟层改投”那一支用 known_stale，
   兜底搬页路径不看它）。**已修（e45d941）**。
3. `probe: motion d3d decision` 第一次实现整段抛异常（`String(stub 对象)` 触发 E_CONVERT），
   已改为逐项 try；但即使没有这些值，`D3DAdaptor.captureCanvas` 仍在跑、`SeparateLayerAdaptor`
   0 次，足以确认两作都走了 D3D 路径。

**本轮做法（真正的选路修复）**：`motionplayer` 的 `Motion.enableD3D` 以前返回一个**字典 stub
对象**（truthy），于是游戏脚本的
`_useD3D = Motion.enableD3D && (typeof window.d3dMotion != "undefined") && window.d3dMotion`
恒为真，一律走 D3DAdaptor 的 `captureCanvas` 交付链。本壳的 D3D 只是空壳（没有参考实现那种
render texture），帧画出来也 assign 出去，却停在隐藏页 —— 这就是 SD / m2logo / 立绘共同的现象。

现在 `Motion.enableD3D` 恒返回 `0`（Integer；setter 吸收脚本赋值并只记一条日志），让游戏按
自己的降级路径走：`SeparateLayerAdaptor` + 私有渲染层（§1.5 已移植，层挂在 owner 之下、z 序与
页面归属都是游戏自己的）。依据：Android 上根本没有 D3D；这些作品的工具链自带非 D3D 路径
（游戏自己的菜单项“モーション表示にDirect3D描画を使用しない”=`-nod3dm`，千恋万花 `patch.tjs`
还显式 `&Motion.Player.useD3D = 0;`）。

**回滚办法**：把 `getEnableD3D` 改回返回字典 stub 即回到 D3D 路径（一行）。

**下一步看什么**：真机日志里应出现 `Motion.enableD3D: 脚本请求开启 D3D 路径，已忽略`，
`SeparateLayerAdaptor` 计数 > 0、`D3DAdaptor.captureCanvas` 归零，
以及 `motion: SeparateLayerAdaptor 渲染层路由 owner=… parent=…`（§1.5 的路由日志）。

**同一轮日志里的另两条线索**：

- 千恋万花：`convertImage: key='m2logo.mtn/source/logo/icon/icon32/pixel.png' RL decode
  failed for 3x16 raw=48B; falling back to raw palette decoding` —— “m2logo 动效颜色异常”
  的可疑点（该资源标 RL 却解不出 RLE；上一轮已改成回退原始字节 + 格式推断，若颜色仍不对
  就要查调色板字节序/格式推断，而不是继续调 RL 解码）。
- NEKOPARA：`stencil mask label '■耳L/R' not found for 'stencil'` 与大量
  `expandSubMotionNodes: no nodes for 'motion/general_obj_*'` —— E-mote 节点树有分支没被
  展开，属于“立绘只剩部分部件”方向的待查点。

---

## 1.14 x86_64 模拟器上「引擎加载即 SIGABRT」（2026-09-25，根因：Berberis 伪 cpuinfo 触发 libomp 断言）

**用户报的现象**：在 Android Studio 里跑/预览 UI 时“无法进入程序”。

**结论（已实测验证）**：与 Compose 预览、AS instrumentation、壳代码全部无关。
真正的失败链是：`System.loadLibrary("engine_api")` → `libengine_api.so` 的**静态构造函数**触发
libomp 18 运行时初始化 → libomp 按 `/proc/cpuinfo` 建 CPU 拓扑 → 数量一致性断言失败 → `abort()`。
所以进程死在 `krkr2 JNI_OnLoad` 日志之前（普通 `am start` 一样崩；探针 APK 只 `loadLibrary` 也崩）。

**为什么只有这台模拟器**：`emulator-5554` 是 x86_64 + 16KB 页，跑 arm64 库靠
Berberis(`libndk_translation`，日志 `I berberis: Initialized Berberis ((null)), version 16.0.0`) 翻译。
翻译层给 **arm64 guest** 的 CPU 视图自相矛盾：

| 来源（guest 侧） | 看到的 CPU 数 |
|---|---|
| `sysconf(_SC_NPROCESSORS_ONLN)` / `sched_getaffinity` / `/sys/devices/system/cpu/online` | 4 |
| `/proc/cpuinfo`（且**没有** `physical id`/`core id`/`apicid` 字段 → libomp `parsedEntries=0`） | **2** |
| 宿主(x86_64) `/proc/cpuinfo`（对照组） | 4（`physical id 0–3`，AMD Ryzen 9 3900X） |

libomp 的 verbose 日志把过程写得很清楚（`KMP_AFFINITY=verbose,none`）：
`#155 Initial OS proc set respected: 0-3` → `#220 parsing /proc/cpuinfo.` → `#157 2 available OS procs`
→ `#191 2 sockets x 1 core/socket x 1 thread/core (2 total cores)` →
**`OMP: Error #13: Assertion failure at kmp_affinity.cpp(4567).`**。

**取证要点（下次遇到同类问题照这个流程走）**：

- tombstone 里**没有** `Abort message:`、host 侧只有 3 帧
  （`libc.so (syscall+24)` → `libndk_translation.so (berberis::RunKernelSyscall+81)` → `/memfd:exec`），
  guest 侧 2 帧（`arm64/libc.so (abort+152)` + `base.apk (offset 0x110000)` = libomp 的 LOAD1）。
  原因：libomp 的致命信息**只写 stderr** 再直接 `abort()`，不走 `__android_log_assert`。
- 所以必须在 app 里**把 fd 1/2 重定向到文件**才能拿到那句话（探针用
  `android.system.Os.dup2(fd, 1/2)`）。tombstone 只给「谁 abort 了」，不给「为什么」。
- `ro.debuggable=0` → 没有 root、`/data/tombstones` 读不到；改用
  `adb shell dumpsys dropbox --print SYSTEM_TOMBSTONE`（shell 有 DUMP 权限；但**没有 memory map 段**）。
- 本机没有 `python`/`readelf`/`zip`：APK 用 `jar --no-compress --update` 拼、用自写
  `temp/ziplist.js` 解析 zip 中央目录拿精确 payload 偏移，ELF 用 NDK 的
  `llvm-readelf/llvm-nm/llvm-addr2line`。
- **无侵入复现**：`temp/probe/`（探针 APK 工程，未跟踪）能单独验证「某个 .so 能不能在这台机器上 dlopen」。
  注意三点：① NDK 编的 .so 必须 `-Wl,-z,max-page-size=16384`（否则 16KB 页设备 dlopen 报
  "program alignment (4096) cannot be smaller than system page size (16384)"）；
  ② 改完 Java 必须 `cp dex/classes.dex .` 再拼 APK（否则打进旧 dex，看起来像“改动无效”）；
  ③ 换 intent extra 重跑前必须 `am force-stop`（否则 intent 送到已运行实例，`onCreate` 不重跑）。
- 另一条死路（别重走）：arm64 可执行文件（`aarch64-linux-android21-clang -pie`）在这台机器上跑不起来，
  报 `missing DT_SYMTAB`；`app_process` 起的 shell 进程也没启用 native bridge，
  load arm64 库直接 `is for EM_AARCH64 (183) instead of EM_X86_64 (62)`。测 arm64 库必须用真 app 进程。

**修复（用户选的“app 侧最小改动”方案，已落地并端到端验证）**：

| 文件 | 改动 |
|---|---|
| `bridge/engine_api/src/android/omp_env_compat.c`（新增，未跟 CMake，只给 NDK clang 直接编） | 构造 `__attribute__((constructor(101)))` + `JNI_OnLoad` 里 `setenv("KMP_AFFINITY","disabled",1)`；已有值则尊重不覆盖；日志 tag `KrKr2Next/omp` |
| `scripts/build_engine_android.sh` | 新增一步「编译 libomp_env_compat.so」→ `app/app/src/main/jniLibs/arm64-v8a/libomp_env_compat.so`（`aarch64-linux-android24-clang -shared -fPIC -O2 -Wl,-z,max-page-size=16384 ... -llog`） |
| `app/app/src/main/kotlin/org/dpdns/clevebitr/core/NativeEngine.kt` | `init` 里：`Build.SUPPORTED_ABIS.firstOrNull() != "arm64-v8a"` 时先 `loadLibrary("omp_env_compat")`，再 `loadLibrary("engine_api")` |

为什么必须在 arm64 原生代码里 `setenv`：**Java 侧 `android.system.Os.setenv` 无效**（实测
`Os.setenv(KMP_AFFINITY, disabled) ok` 之后 `guest getenv` 仍读到 `(null)`，engine 照崩）——
Java/ART 是宿主 x86_64 libc 的 `setenv`，改的是宿主 `environ`，而 libomp 是 arm64 guest 库。
为什么是 `disabled` 而不是 `none`：`verbose,none` 仍会建拓扑、仍会断言，必须**完全跳过** affinity 初始化。

**验证记录（2026-09-25 08:45，emulator-5554 实测）**：

```
I KrKr2Next/omp:    omp_env_compat loaded: KMP_AFFINITY="disabled" (set by omp_env_compat)
I KrKr2Next/Engine: omp_env_compat loaded (translated env: x86_64, arm64-v8a)
I KrKr2Next/App:    libengine_api.so loaded, Application Context handed over   ← 修复前死在这里
I KrKr2Next/App:    engine log -> /storage/emulated/0/Android/media/org.dpdns.clevebitr/logs/engine.log (rc=0)
I KrKr2Next/Main:   onCreate (recovery=PreviousExit(kind=CLEAN, detail=null)) / onResume
```

进程存活、无 SIGABRT。**没验证过的**：真机 arm64（按主 ABI 判定会跳过垫片，逻辑上行为不变，但没实机跑过）；
把 AVD 改成 `-cores 2`（伪 cpuinfo 恒为 2，理论上也能避开断言）也没试。

**顺手记一条环境坑**：WSL 里 Gradle 用的是 `~/.android/debug.keystore`，与 Windows/AS 的那把**不是同一个 key**，
所以 `adb install -r` 装 WSL 产物的 APK 会 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`。
要覆盖 AS 装上去的包，就在 Windows 侧构建：
`cd app && JAVA_HOME="/c/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot" ./gradlew.bat :app:assembleDebug`。

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
| 关于页：作者/协议/仓库/技术栈/版本号 + 运行环境（包名/系统/ABI/机型） | ✅（版本号已带 git 短哈希：`v0.1.0-<hash6>-<YYMMDD>`；仓库/Kirikiroid2/krkrz/AetherKiri 可点按钮跳系统浏览器） |
| 启动图标：自适应（API 26+）+ MD3 主题图标（API 33+ 单色层） | ✅ 2026-09-24（`mipmap-anydpi-v26/ic_launcher.xml` 三层；API 24–25 用 `mipmap-anydpi/ic_launcher.xml`），见 `SHELL_HANDOVER.md §5` |
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
| 图形设置页（纹理压缩 / 精确渲染 / 最大纹理尺寸 / 内存档） | ✅ 2026-09-23（`core/GraphicsConfig.kt` + `ui/GraphicsConfigEditor.kt`；引擎侧见 §1.11，含“壳选项覆盖”通道与惰性缓存失效） |

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
| **千恋万花 SD 交付的修法** | 用户 2026-09-22 选 **B：搬参考的 `D3DEmote.tjs`**。**2026-09-23 修正**：只把它当**兑底伴生脚本**（游戏自己没有 `motion.tjs`/`D3DEmote.tjs` 时才提供）；**不得**覆盖游戏自带的 `system/motion.tjs`（它定义 `MotionResourceManager`，而 D3DEmote.tjs 依赖它）—— 见 §1.13.1 |

---

## 6. 未完成 + 阻塞

### 6.1 渲染层（当前主线，详见 `compat/recon/render-issues.md`）

| 项 | 规模 | 状态/阻塞 |
|---|---|---|
| G2 **进动画卡 4.4s** | 中 | 已细分：`createRenderer=2689ms bindTexture=0ms mvp=0ms`（1920×1080，1 张纹理）⇒ 卡在 `CreateRenderer`；需继续查 Cubism 渲染器/掩码缓冲创建 |
| G2 **帧率 ~43–45** | 中 | 每帧 1920×1080 **GPU→CPU 回读**（`capture` 路径**刻意优先 CPU**：引擎随后按 CPU 位图重传纹理会覆盖只写纹理的内容）；主窗口走 `path=GPU`，只有 Live2D 图层退化。附带：该回读用 `GL_BGRA_EXT` 调 `glReadPixels`，ES3 非法 → `err=0x0502` |
| G2 / 千恋万花 **`SystemWatchTimerTimer` 卡顿**（1.5–1.9s） | 中 | 卡在 `DeliverEvents()` 或 `TickBeat()` 循环（内层 MarkStage 未触发）；需在该函数内加细阶段探针 |
| 千恋万花 **`wave` 转场缺失** | 中 | **需按 GPU render method 重做**：2026-09-23 试过逐字节移植 AetherKiri 的 CPU 扫描线实现，CI 编译失败（`iTVPScanLineProvider::GetScanLine*` 在本仓库被 `#if 0`）；根因与结论见 `render-issues.md §2` |
| 千恋万花 **SD/logo 交付（D3DEmote）** | 中 | **已回退覆盖、改为兑底（§1.13.1）**：旧方案把游戏自己的 `system/motion.tjs` 顶掉，导致 `MotionResourceManager` 未定义、图像加载全部失败。参考实现对这两作改的是“加载后打补丁”而非换文件（`TVPGetD3DEmoteGpuBatchPatchScript` 尚未移植）。待真机回归；方案 A 的兜底（`assignMotionImages` 路由）保留 |
| 千恋万花 **字体/文字颜色偏白、logo 色偏与残留矩形** | 中 | 文字颜色：§1.12.1 的**源码改写补丁在本整合包上不生效**（`custom.tjs` 里没那个函数，实现已被搬到字节码 `msghack.tjs`）→ 改为**移植原生 `Layer.EdgeShadowDrawText` / `…KinsokuRect`（§1.13.2）**，同理修复 logo 色偏（同一根因：MotionResourceManager 缺失会让 `m2logo.mtn` 取不到图）。待真机回归 |
| **AlphaMovie 插件复用 core 解码器** | 中 | 未做；完成后删掉重复 ~1700 行 |
| おっぱいスパイ学園 **切 CG 视频严重卡顿** | 中 | 🟡 **已修三轮（§1.10.1），第三轮待真机回归**。前两轮（`RequestStop()`、`AbortPictureWait()`）均被真机证伪；真因是**跨线程死锁**：解码线程在 `Flush()` 里持 `m_mtxPicture` 打 spdlog，而 `engine_tick` 整帧持有 `StartupLogSink` 要的 `g_registry_mutex`，而渲染线程正在 `Release()` 里等它退出。已把所有 `m_mtxPicture` 临界区改成“锁内取值、锁外打日志” |
| チート緊縛術（classic）**`Member "showLayers" does not exist` → 引擎退出** | 小-中 | 脚本层成员缺失：`showLayers` 在本仓库与 AetherKiri 都**未注册**（`grep -rn showLayers cpp/` 两边都空）。日志：`trace : mainwindow.tjs(5777)[(function expression)] <-- conductor.tjs(440)[onTag]`、`scenario.ks 行 223 タグ eval`。该作目录带 `patch.xp3` + `claude-3-5-sonnet-…翻译补丁备份` + `hook.ini` + `FONTCHANGER.dll`（加载失败），**疑似翻译补丁替换的 `mainwindow.tjs` 少了该函数**。需要用户提供 `data.xp3>mainwindow.tjs` 与 `patch.xp3` 里的同名文件对照 |
| チート緊縛術（AetherKiri）**字体渲染不正确** | 小-中 | **缺日志**：该游戏目录里只有 classic 层那次 `engine-*.log`。要 AetherKiri 层那次的 `FontSystem: 已注册字体 N 个`、`font_fallback_mode=`、缺字/`GetBeingFont` 行。该作自带 `ShiraYukiNoa.otf` + `FONTCHANGER.dll`（本引擎加载失败）⇒ 字体很可能靠该插件换 |
| **猫娘乐园（NEKOPARA 4）游戏内 E-mote/Live2D 立绘加载不出** | 中 | §1.10.2 修好了 PSB 档案名切分（错误计数 3230→0，该轮日志确认）；但 2026-09-23 17:57 真机日志里立绘仍不出来，根因是 §1.12.2 的覆盖把 `system/motion.tjs` 顶掉、`MotionResourceManager` 未定义（`motion.tjs(604) _loadImages` 抛异常）—— **已修（§1.13.1），待真机回归** |

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
| 移植溯源清单 | `compat/upstream/aetherkiri_ports.json`（14 个文件） |
| IO 组件 | `cpp/core/io/`（`StoragePolicy.h` 策略契约；`IoPolicy.*`；`IoModuleLocator.*`；`IoVirtualFile.*` 虚拟文件注册点） |
| 兼容层框架 | `cpp/core/compat/`（`CompatLayer.*`、`ModuleGate.*`、`AetherKiriCompanions.*`） |
| **壳选项通道** | `cpp/core/environ/ConfigManager/GlobalConfigManager.h`（`TVPSetShellOption` 等）+ `IndividualConfigManager.cpp` 的四个 `GetValue<T>` 特化 |
| **图形设置** | 壳 `core/GraphicsConfig.kt` / `ui/GraphicsConfigEditor.kt`；引擎 `cpp/core/visual/RenderManager.{h,cpp}`（`TVPInvalidateGraphicsOptionCaches`）、`ogl/RenderManager_ogl.cpp`（`CreateStaticTexture2D_auto`） |
| **FONTCHANGER 兼容** | `cpp/plugins/fontchanger_compat.cpp`（读 `hook.ini` 注册字体 + 强制字面）；强制字面在 `visual/FontImpl.{h,cpp}`、`FontSystem.cpp` |
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

1. **真机回归本轮修复**（都要看日志，不能只看“感觉好了”）：
   - **§1.13.1（两作）**：`Member "MotionResourceManager" does not exist` 与 `画像ロード失敗` 归零；
     千恋万花看到游戏自己的 `motion.tjs を読み込みました`、SD 与 m2logo 出图；NEKOPARA 4 E-mote 立绘出图；
   - **§1.13.2（探针构建）**：`probe: textrender EdgeShadowDrawText len=…` 出现，消息文字颜色正常；
   - おっぱいスパイ学園连续切 8 段 CG：`update_max` 不再出现 4000ms 量级、不再出现
     「未退出，放弃销毁」、出现 `movie: 停播请求→影片线程退出耗时 Nms`（N 应远小于 4000）；
   - 壳：打开「加载游戏时自动显示日志」→ 开游戏立即看到日志、`startup state -> 2` 后自动消失。
2. **チート緊縛術**：等用户给 AetherKiri 层日志（字体）+ `mainwindow.tjs`/`patch.xp3` 对照
   （`showLayers`）。
3. **NEKOPARA 翻转动画位置**（上一轮已修）：真机回归确认 `*c1*`/`*c2*` 变体位置。
4. **千恋万花**：若 SD 仍“显示一两秒后消失”，接着查方案 A 的 `assignMotionImages` 路由（§1.6）；
   参考实现对这两作的 `motion.tjs` 是**加载后打补丁**（`TVPGetD3DEmoteGpuBatchPatchScript`），尚未移植；
   `wave` 转场需**按 GPU render method 重做**（CPU 扫描线移植已证实不适用，见 §6.1）。
5. **G2**：进动画 4.4s（`CreateRenderer` 计时细分）→ 帧率（普通构建复测基线）。
6. **`SystemWatchTimerTimer` 卡顿**：`SystemControl.cpp` 的 `DeliverEvents()`/`TickBeat()` 加 MarkStage。
7. **兼容层**：若用户回了 **I3**，接完 `mountSiblingsForArchiveProject`（M1 收尾）；
   否则继续 **M6 小模块批次**。C3 已阻塞于 C2。
