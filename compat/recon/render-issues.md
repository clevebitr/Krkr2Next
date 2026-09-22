# 渲染问题追踪（open issues）

> **当前未解决**的渲染问题、已取得的证据、下一步探针。解决一条就删一条。
> 渲染器架构与"相对上游 AetherKiri 的差异"见 `render-diff.md`；层与兼容决策见
> `../README.md`。本文只写**现状事实 + 下一步动作**，不写推理过程。

---

## 0. 取证方式（新会话先看这里）

- 目标平台：Android arm64，**原生 EGL + GLES3**（无 ANGLE）。
- 探针总开关：编译期 `KRKR_RENDER_PROBE`（CMake `-DENABLE_RENDER_PROBE=ON`）。
  CI 用 `workflow_dispatch` 勾选 `enable_render_probe=true` 触发一版即可；
  **默认构建不受影响**（所有探针都在 `#if defined(KRKR_RENDER_PROBE)` 里）。
- 日志位置：
  - 每游戏引擎日志：`/storage/emulated/0/Android/media/org.dpdns.clevebitr/logs/games/<游戏名>-<短哈希>/engine-<时间戳>.log`
  - 宿主日志：`.../logs/app.log`；卡死转储：`.../engine-<时间戳>.log.stall`
- **帧率必须在普通构建（push 构建）上测**：探针构建每帧同步写日志（`HostWindowLayer::RTProbe`、
  `engine_tick: … enter/return` 且带 `flush()`），会把 fps 测低。
- **已内置探针**（默认关，日志前缀 `probe:`）：

  | 探针 | 位置 | 上限 |
  |---|---|---|
  | `probe: Storages.isExistentStorage(<arg>) #N` | `io/IoStorage.cpp` | 前 64 |
  | `probe: Layer.loadImages(<arg>) #N` | `visual/LayerIntf.cpp` | 前 64 |
  | `probe: PropGet miss '<member>' (compatFallbacks=N)` | `tjs2/tjsObject.cpp` | 前 32 |
  | `probe: PropSet miss 'diffEnterCount', flag=…` | `tjs2/tjsObject.cpp` | 前 32 |
  | `probe: TJS stack nearly exhausted block=… func=… start_ip=…` | `tjs2/tjsInterCodeExec.cpp` | 每次栈耗尽 |
  | `probe: AMV payload first32=… SOI_count=… SOI_offsets=[…] tail16=…` | `visual/LoadAMV.cpp` | 每帧 |
  | `probe: AMV variant revision=… qt_size_plus_hdr=… attr=… frame=… alpha=…` | 同上 | 每帧 |
  | `probe: AMV retry-from-SOI off=… len=… ok=… WxH` | 同上 | 每帧 |
  | `probe: 图片格式不支持 -> 1x1 占位 name=… ext=…` | `visual/GraphicsLoaderIntf.cpp` | 每次命中 |
  | `probe: AlphaMovie.showNextImage [<amv>] frame=n/N crop=(l,t) WxH pos=(x,y) screen=… layer=…` | `plugins/alphamovie.cpp` | 前 40 |
  | `probe: AlphaMovie.copyNextImageToTexture [<amv>] rect=(l,t)-(r,b) target=… atlasTop=…` | 同上 | 前 40 |
  | `krkrlive2d: 渲染器阶段细分 createRenderer=…ms bindTexture=…ms mvp=…ms` | `plugins/krkrlive2d.cpp` | 该阶段 >200ms |
  | `[probe] krkrlive2d: …` / `[probe] krkrgles: …` | 对应插件 | 多为首次/限频 |

- 触发探针构建：
  ```bash
  gh workflow run "Android 构建" --repo clevebitr/Krkr2Next --ref main \
    -f build_type=debug -f enable_render_probe=true
  ```
  （工作流 `concurrency: cancel-in-progress`：连推/连触发时前一条会显示 cancelled，属预期。
  CI 偶发 NDK 下载损坏 `Archive is not a ZIP archive` —— 基础设施问题，重跑即可。）

---

## 1. nainiuniu5krkr（G2）— 进动画卡 4.4s + 帧率 ~43–45

**状态：递归与 Live2D 已修好；剩性能问题**

已修（真机确认）：
- **启动期 `diffimage2.tjs` 无限递归**：根因是 A 块回退只挂在 `PropGet`，而
  `Storages.commitSavedata()` 走 `FuncCall`；缺失成员导致 `startup.tjs` 抛错 → 引擎兜底重跑
  `system/Initialize.tjs` → `diffimage2.tjs` 执行两次 → `Storages.isExistentStorage` 双重包装自递归。
  现已在 `tjsObject.cpp` 补 `TJSCompatResolveFuncCallFallback()` 并把 `rollbackSavedata`
  加入 no-op 名单。
- **Live2D 从未被驱动 + 图片以 ZIP 头加载失败**：根因是 `TVPGetPlacedPath` 把伴生脚本的虚拟命中
  当成"当前目录已找到"，跳过了 auto-path 搜索，游戏的 `data.xp3>system/live2d.tjs` 永远没被尝试。
  现已改为**物理优先、auto-path 次之、虚拟最后兜底**。

未解决 ①：**进动画同步卡 4.4s**（`engine-20260922-130246.log`）：
```
krkrlive2d: 渲染器阶段细分 createRenderer=2689ms bindTexture=0ms mvp=0ms（1920x1080，1 张纹理）
krkrlive2d: 加载耗时 [ev_cg001_02s] zip=372ms moc3=3ms 纹理=1297ms 渲染器=2689ms 动作=40ms 合计=4404ms
```
⇒ 卡顿就在 `CreateRenderer(1920×1080)` 本身（该阶段其余两项 0ms）。第二次加载命中缓存只剩 95ms。

**下一步**：在 `CreateRenderer` 前后加更细计时（Cubism `CubismRenderer_OpenGLES2` 初始化会创建
掩码缓冲/着色器等 GL 资源），确认是掩码缓冲分配、着色器编译，还是首次 GL 同步。

未解决 ②：**帧率 ~43–45（不到 60）**（同一日志，普通构建需复测）：
```
frame_perf: fps=45.5 update_avg=9.62ms post_avg=0.87ms update_max=133.98ms slow(>33ms)=3
```
- `update_avg` 只有 ~10ms，却只有 ~45fps ⇒ 差额在宿主侧（vsync 调度/交付），**先用普通构建复测**。
- 每帧一次 **1920×1080 GPU→CPU 回读**，来自 `capture` 路径**刻意优先 CPU**
  （源码注释：引擎随后按 CPU 位图重传纹理会把"只写纹理"的内容覆盖回去）：
  ```
  [probe] krkrgles: copy path=sync-read(bgra) fbo=4 ... copy=1920x1080 pitch=8192 err=0x0502
  [probe] krkrgles: layer CPU buffer copied=1 1920x1080 ...
  ```
  主窗口走的是 `HostWindowLayer::UpdateDrawBuffer: path=GPU`，只有 Live2D 这条图层路径退化。
- 附带：该回读用 `GL_BGRA_EXT` 调 `glReadPixels`，ES3 不是合法格式（首次 `err=0x0502`）。

未解决 ③：**`Application::Run: SystemWatchTimerTimer` 卡顿**（1.5–1.9s，`.stall` 两条）：
内层 MarkStage（`定时器: COMPACT_IDLE` / `RunMemoryGovernor` / `SystemWatchTimerTimer 返回`）
**一条都没触发** ⇒ 卡在 `tTVPSystemControl::SystemWatchTimerTimer()` 的
`DeliverEvents()` 或 per-window `TickBeat()` 循环里。

**下一步**：在 `cpp/core/environ/win32/SystemControl.cpp` 的 `DeliverEvents()` 与
`TickBeat()` 循环内加 MarkStage，把阶段收窄到具体子步骤。

---

## 2. 千恋万花（`KRKR汉化高压_千恋万花`）— 多个独立问题

**状态：图片路径正常；SD/logo 交付与字体颜色待处理**

已核实的现状：
- `probe: Layer.loadImages(psb://quickmenu.pimg/*.tlg)` 一串 ⇒ 图片/E-mote 加载路径正常。
- **`Transition handler 'wave' not found, falling back to crossfade`** ⇒ `wave` 转场未实现。
  **2026-09-23 取证：不能照搬 AetherKiri 的实现。** 试过逐字节移植 AK 的
  `cpp/plugins/extrans_precise/wave.{cpp,h}`（CPU 扫描线实现），CI run 35724476705
  的「构建引擎」失败：
  `wave.cpp:215/217/219: error: no member named 'GetScanLineForWrite'/'GetScanLine' in 'iTVPScanLineProvider'`。
  原因：本仓库转场管线已改为 **GPU render method**（`TransIntf.cpp` 的
  `tTVPCrossFadeTransHandler::Blend` 用 `iTVPRenderManager::OperateRect` + 纹理，
  CPU 扫描线路径整段 `#if 0`），并据此把 `iTVPScanLineProvider` 的
  `GetPixelFormat`/`GetPitchBytes`/`GetScanLine`/`GetScanLineForWrite` 全部 `#if 0`
  （`transhandler.h` / `TransIntf.h` / `TransIntf.cpp`）。
  ⇒ **wave 必须按本仓库的 GPU render method 机制重做**（新增一条 shader 转场，
  逐行位移 + 背景填充 + 混合），属引擎侧工作。
- **卡死**：`.stall` 记 `render-thread-stall`，阶段 `Application::Run: SystemWatchTimerTimer`
  （与 G2 同源，见 §1 未解决 ③）。
- `convertImage: RL decode failed … raw palette` ⇒ **已知小图标回退**
  （`cpp/plugins/psbfile/PSBMedia.cpp` 有注释：m2logo icon32/icon18 的未压缩调色图被标成 RL），非根因。
- **SDCG 只显示背景 UI / 显示一两秒后消失（+ 残留矩形）** ← 当前主问题
  - 机制（真机日志实证）：SD 分件是 `data1080.xp3` 的 `sdNNN.mtn` + `SDNNNAA.png`，
    由 motionplayer 的 **D3DEmote 路径**渲染：`engine-20260922-155105.log` 里
    `D3DAdaptor.captureCanvas` 301 次、`SeparateLayerAdaptor` **0 次** —— 本作走
    D3DAdaptor（与 `patch.tjs` 设 `useD3D=0` 的意图相反；脚本实际读的是 `Motion.enableD3D`）。
    因此上一版改 `SeparateLayerAdaptor` 父层**对本作无效**（对别的 Yuzusoft 标题仍可能有用）。
  - 根因：游戏自带的 `system/AffineSourceMotion.tjs`（编译字节码，字符串表可查）用
    **`Layer.assignImages`** 把 scratch 层交给角色层；该池层名为
    `AffineSource情報プール用`（见 `system/AffineSource.tjs`）。`AssignImages` 走
    `MainImage->Assign()`，目标层与 scratch **共享同一张纹理**，下一帧重写 scratch 就把
    刚交付的画面抹掉。参考实现为此提供 `Layer.assignMotionImages`（把完成的纹理**换**进
    目标层），并在 `AssignImages` 内部识别 scratch 交付后路由过去。
  - 已改（2026-09-22）：`cpp/core/visual/LayerIntf.{h,cpp}` 新增 `AssignMotionImages`
    （移植自 AetherKiri `LayerIntf.cpp:6065-6265`，去掉其 KAG 转场/exchanged-page 路由与
    profile 埋点）+ 注册 `Layer.assignMotionImages`；并在 `AssignImages` 里加
    `TVPIsAffineSourceMotionScratch()`（同名判据：目标可见有名、源隐藏无名、源父层是
    `AffineSource情報プール用`、目标父层不在池内）把该交付路由到交换语义。
  - 待验证：真机确认 SD 正常出现并持续（不再一两秒后消失）、残留矩形是否消失。
  - **2026-09-22 二轮真机：无变化。** 已用 APK 内 `libengine_api.so` 字符串校验确认改动确实在包里
    （`AffineSource情報プール用`×1、`assignMotionImages`×3、`渲染层路由`×1），所以**该假设不足以解释**。
  - 新增事实（游戏自带脚本字符串表实证）：`AffineSourceMotion.tjs` 用的成员是
    `clearEnabled` / `captureCanvas` / `unloadUnusedTextures` / `assignImages` /
    `_redrawImage` / `motionD3DAdaptor` / `motionWorkLayer` / `_motionSeparateAdaptor`；
    **不用** `setPresentationTarget` / `clearPresentationTarget` / `presentationHold` /
    `assignMotionImages` / `removeAllTextures`。⇒ 它不是 AetherKiri 自研的那份脚本。
  - 已加判定性探针（封顶 24 条）：`probe: AssignImages target=… name='…' visible=…
    parent='…' | source=… | motionScratch=0/1`，并在 `D3DAdaptor` 壳补上游戏脚本确实会写的
    `clearEnabled` 属性（之前缺失）。下一轮日志据此直接判定：交付是否走了 assignImages、
    scratch 签名是否匹配、目标层是否可见/在树上。
  - **第三轮真机（`engine-20260922-164741.log`）**：
    - 探针生效但封顶被启动期 KAG 页面交换刷满（`SquareMaskLayer2`/`TouchUiLayer:*`，
      `motionScratch=0`）⇒ 看不到 SD 的交付。已改为只记录「隐藏且无名的源层」或路由命中的交付。
    - `AssignImages` 确认被大量使用（页面 `表-背景`↔`裏-背景` 交换），主路是 assignImages。
    - `probe: D3DAdaptor.clearEnabled = 1/0/1/0` ⇒ **游戏确实写这个属性**，之前壳里缺失
      是真的缺口（已补）；参考实现该属性有完整语义。
    - `drawOnto ... capture target=… visible=0`：D3D 捕获目标层始终不可见（工作层），
      与预期一致。
  - **第四轮真机（`engine-20260922-171124.log`）——路由从未命中**：
    - `AssignMotionImages` 命中 **0 次**：池判据 `TVPIsAffineSourceMotionScratch` 不成立。
    - 真实交付签名（探针抓到）：
      `target name='ev' visible=1 parent='表-背景'/'裏-背景'` ←
      `source name='' visible=0 parent='トップレイヤ'`。
      即：**工作层挂在 `トップレイヤ`（页面根）下，不是 `AffineSource情報プール用`**；
      参考实现里对应的是 KAG 页面交换分支（`TVPResolveExchangedKagAssignmentTarget`），
      其结构前置（`source->GetParent() == target->GetParent()->GetParent()`）成立。
    - 第二版探针的 40 条封顶仍被“池内拷贝 + `ev` 页面交换”吃光，SD 未进记录 ⇒
      已改为**按 (target,source) 对去重**（封顶 60），保证不漏。
    - 同时加了一个与参考一致的定向修复：目标层与「隐藏无名工作层」交付后若共享同一张
      纹理，则 `MainImage->Independ()` 断开别名（参考对 KAG `syslay` scratch 就是这么做的，
      且 `Independ()` 是 GPU 侧拷贝、不丢像素）。
  - **第五轮真机（`engine-20260922-173215.log`，交付对去重生效）**：
    - **SD 不走 `assignImages`**：去重后全部「可见有名目标 ← 隐藏无名源」只有
      `target='ev'`（事件 CG）与 `target='title_bg'`（标题背景），源都在 `トップレイヤ` 下，
      **没有任何一条目标层是 SD 角色层**。
    - 全日志计数：`D3DAdaptor.captureCanvas` 4 次 / `drawOnto` 4 次 / `Player_clear` 6 次
      —— **全部发生在 logo 阶段**；SD 窗口只有 `drawPSBImages`(24) → `drawAnimated` →
      `compositeStatic`(19)，**没有 captureCanvas / drawOnto / assignImages**。
    - 所有 motion（logo / title / SD）都画进同一个共用工作层
      `realLayer=0xb400007859e4c6e0`；而 assignImages 的源是另外两个层指针
      （`0xb40000785afa6e00`/`0xb40000786605dc80`，即 `captureCanvas(work)` 的 `work`）。
      ⇒ 交付链是：drawPSBImages→共用工作层 → captureCanvas(work) → assignImages(目标, work)。
      **SD 在第一步之后就没有后续**，所以帧永远没离开工作层。
    - 待确认的关键量：SD 的绘制目标（`target=0xb4000076e7b1ef40`）与共用工作层
      `0xb400007859e4c6e0` 的**名字与父层**。已给 motionplayer 的
      `DescribeLayerState` 加 `name/parent`、接入 `drawPSBImages`，并加
      `probe: resolveRealLayer target=… isSeparateAdaptor=… adaptorTarget=… adaptorOwner=…`
      （每种目标只记一次），下一轮即可判定 SD 走的是哪条分支、帧落在哪一层。
  - **第六轮真机（`engine-20260922-175650.log`）——根因定位**：
    - `drawPSBImages: 24 images … realLayer=0xb40000785a1ebfa0
      layer(name='トップレイヤ',parent='プライマリレイヤ',visible=1,1920x1080)`：
      **SD 被画进了 `トップレイヤ`**。而 `トップレイヤ`（= `Window.primaryLayer`）就是
      页面 `表-背景`/`裏-背景` 的**父容器**（参考实现自己的注释：“The scratch layer used
      by D3DEmote lives under トップレイヤ, not under 裏-背景”）；KiriKiri 里父层先画、
      子层后画 ⇒ 画进容器 = **被背景与 UI 盖住**。
    - `resolveRealLayer … isSeparateAdaptor=0`（全部）：不用 SeparateLayerAdaptor；
      游戏把 **D3DAdaptor 壳**当绘制目标，`resolveRealLayer` 处理不了（它没有 `window`
      成员）→ 兜底到 `window.primaryLayer`（即 `トップレイヤ`），于是发生上面的错层。
    - 已改：参考实现的 D3DAdaptor 有**自己的 surface**；本壳没有，于是用游戏每帧
      `captureCanvas(work)` 传入的 `work` 层作为其 surface（那正是游戏随后
      `assignImages` 到可见层的**来源层**）。`D3DAdaptor_captureCanvas` 记录该层，
      `resolveRealLayer` 的“无 window 成员”分支优先返回它，不再兜底到页面容器。
    - 待验证：SD 是否已可见（预期在背景之上；若被消息窗盖住再调插入点）。

> 临时探针（`probe: AssignImages[pair]` / `probe: AssignMotionImages` /
> `probe: resolveRealLayer` / `probe: D3DAdaptor.clearEnabled`，以及
> `DescribeLayerState` 的 name/parent）定案后一并删除。

  - **第八轮真机（`engine-20260922-185120.log`）——别名已排除**：
    - `probe: AssignImages detach(Independ)` 出现 12 次、`no-shared-texture` **0 次**
      ⇒ 共享纹理（别名）确实存在、也确实被 `Independ()` 断开，**SD 不可见不是别名问题**。
    - SD 的交付签名：`target='CG View LayerAffineLayer' visible=1 parent='CG View Layer'`
      ← `source='' visible=0 parent='トップレイヤ'`。`CG View Layer` 位于隐藏页下
      ⇒ 帧落在**隐藏页**里，永远不显示。
    - 已实现参考实现的 **KAG 页面交换路由**（`TVPIsKagBackgroundPair` +
      `TVPResolveExchangedKagAssignmentTarget` 含 `known_stale`）：隐藏页上的运动帧改投
      到可见页（`表-背景`/`裏-背景`）里同名同尺寸的兄弟层。落点：`LayerIntf.cpp`
      `AssignImages` 最前，先于普通赋值。
    - 待验证：SD 是否已可见。下一轮 `probe: AssignImages[pair]` 会带上
      `parentVisible/opacity/type/pos/size/image + 父/祖层名与可见性`，足以定案。
- **启动 logo（`m2logo.mtn` / `yuzulogo.mtn`）**：颜色偏淡蓝而非红、播完残留两个矩形。
  - 已排除「PSB 解码通道序」：`PSBMedia.cpp` 全量输出 BGRA、全游戏一致，非本资源专属。
  - 已排除 `blandlogo1.png` 缺失：参考引擎在同一作同样报 85 次（游戏自带脚本引用了这个
    不存在的品牌 logo 资源），非本仓库 IO 问题。
  - 很可能与上面的纹理别名同源（logo 也走 D3DEmote 交付），先看 SD 回归结果。
- **字体/文字颜色偏白**（应为厂商预设淡灰）：参考引擎为本作应用了
  `message edge argument routing`（`EdgeShadowDrawText` 的 e/ecol 参数路由）等 7 个标题
  专属 hook，KiriNext **一个都没有**（`AetherKiri cpp/core/base/ScriptMgnIntf.cpp`）。
  这是下一个候选根因，尚未定位到具体 code path。
- 参考引擎（AetherKiri）为**本作**应用的 hook 全清单（KiriNext 全缺）：
  `layered PIMG source routing`、`D3DEmote GPU transaction batching`(×2)、
  `action layer properties`、`world layer clone state`、
  `compiled world title motion resolver`、`message edge argument routing`、
  `quick-menu hover sound fallback`；另有合成 UI 存储（`aetherui://` 的 `.func` /
  `scenelist*.csv`）。

**下一步**：
1. **SDCG / D3DEmote 交付**：已按参考实现补 `AssignMotionImages` + scratch 路由（见上），
   **待真机回归**；若仍不对，再对照上面的 hook 清单逐项补。
2. 启动 logo 颜色/残留、字体颜色：先看 SD 回归结果，再按 hook 清单定位。
3. `wave` 转场：**必须按 GPU render method 重做**（CPU 扫描线移植已证实不适用，见上）。
3. 卡死：与 §1 未解决 ③ 同一处理（`SystemWatchTimerTimer` 细阶段探针）。

---

## 2.5 おっぱいスパイ学園（`おっぱいスパイ学園`）— 切 CG 视频严重卡顿

**状态：已修两轮（2026-09-23），第二轮待真机回归**

### 第一轮（已证实不够）

根因：`TVPMoviePlayer::Release()` 先有界等 4s 再销毁，但**没有任何路径叫停播放线程** ——
`BasePlayer::Process()` 的循环条件只有 `m_bAbortRequest`，而该标志原先只在
`CloseInputStream()` 里置位，`CloseInputStream()` 又只从 `~BasePlayer` 调用。
修法：新增 `BasePlayer::RequestStop()`；`CDVDMessageQueue` 等待改为可中断（唤醒序号 + 中断标志）。

### 第二轮（真机 `engine-20260923-000714.log` 定位）

第一轮后仍每次卡满 4s：
```
[00:07:39.449] movie: 停播请求后影片线程 4000ms 仍未退出，放弃销毁并泄漏该影片对象
[00:07:37.093] 卡死探针：渲染线程 1.6s 没推进，最后阶段＝movie: Close→Release()
              （影片线程阶段＝movie: 解码线程→处理消息/解码）
[00:07:34.950] MoviePlayer Flush: 丢弃 4 帧待呈现缓冲（curPicture=3）
```
停播请求发出去了，但解码线程仍停在"处理消息/解码"里。真因：`AddVideoPicture()`
的 50ms 分片等待**只认 `m_pictureWaitAbort`**，不认 `m_bAbortRequest`/`m_bAbortOutput`；
消费者是渲染线程每帧的 `GetFrontBuffer()` —— 渲染线程此刻正卡在 `Release()` 里，队列必然
填满，于是 `OnExit → CloseStream(video) → StopThread()` 的 join 永远回不来。
`AbortPictureWait()` 原先只在 `Stop()` 与析构里调，而 `VideoOvlImpl::Close()` 走的是
`Pause() → Release()`，**从不经过 `Stop()`**。

修法：`Release()` 在有界等待**之前**调 `AbortPictureWait()`（与 `RequestStop()` 并列，
缺一不可）；并在 `AddVideoPicture` 的两个槽位等待循环与 `BasePlayer::OnExit` 里加
阶段标记，若下次仍卡，`.stall` 能直接指出是哪个 join。

原始证据（真机 `engine-20260922-224405.log`，classic 层）：
```
.stall : render: movie: Close→Release()（销毁播放器/join 解码线程）
         movie : movie: 解码线程→等消息(Get) / →处理消息/解码
[error] movie: 影片线程 4000ms 未退出，放弃销毁并泄漏该影片对象（渲染线程绝不 join 它；…）
frame_perf: fps=7.2  update_max=4553.92ms  slow(>33ms)=17
frame_perf: fps=3.5  update_max=4731.04ms
frame_perf: fps=2.5  update_max=4152.34ms
```

验收：连续切 8 段 CG 视频，`update_max` 不再出现 4000ms 量级；不再出现
`影片线程 …ms 未退出，放弃销毁并泄漏`；出现 `movie: 停播请求→影片线程退出耗时 Nms`
且 N 远小于 4000。

---

## 2.6 猫娘乐园（NEKOPARA 4）— 游戏内 E-mote/Live2D 立绘加载不出

**状态：已修（2026-09-23），真机日志已确认生效**

真机验证（`engine-20260923-000530.log`，AetherKiri 层）：`PSB lazy-load error: Not supported
media type ""` **3230 → 0**、`drawFallback: no image loaded` **2321 → 0**、`drawFallback`
本身也不再出现；新增 8 条 `PSB lazy-load archive: lzfs:/e-mote*.psb` + `PSB objectTree` +
`Stored 12 layer positions`，`drawAnimated` 2600 次。（立绘是否肉眼正常请用户确认。）

根因：`PSBMedia::tryLazyLoadArchive()` 按**第一个 '/'** 切档案名。motionplayer 发的是
嵌套存储名 `psb://lzfs://./x.psb/motion/...`，经存储层规范化后是 `lzfs:/x.psb/motion/...`，
于是切出假档案名 `lzfs:`，`loadPSBFile("lzfs:")` 必然抛 `Not supported media type ""`。
同一切法也会把子目录档案（`motion/mono_loop.mtn/...`）切成 `motion`。

修法：移植 AetherKiri `cpp/plugins/psbfile/PSBMedia.cpp:111-134` 的 `ArchiveBoundaryKey()`
—— 先按 `.mtn/` / `.psb/` / `.pimg/` 扩展名定位档案边界，找不到才退回第一个 '/'。
`lzfs:/x.psb/...` 因此切出 `lzfs:/x.psb`，而 `TVPCreateStream("lzfs:/x.psb")` 经存储层
会还原成 `lzfs://./x.psb`，`LzfsStorageMedia` 照常打开。

验收：立绘显示出来；`PSB lazy-load error: Not supported media type ""` 计数归零；
每个档案一条 `PSB lazy-load archive: lzfs:/e-mote*.psb`。

原始证据（真机 `engine-20260922-230250.1.log`，**AetherKiri 层**，`motionplayer.dll Success`）：
```
drawFallback: storage=lzfs://./e-moteバニラ冬制服b.psb chara=all_parts motion=タイムライン構造
drawFallback: trying psb://lzfs://./e-moteバニラ冬制服b.psb/motion/all_parts/タイムライン構造
… /normal … /show … /source/title/motion/normal … /source/title/icon/bg/pixel
[warning] PSB lazy-load error: Not supported media type "" (lzfs:)     ← ×3230
[warning] drawFallback: no image loaded for lzfs://./e-moteバニラ冬制服b.psb  ← ×2321
```

关键事实：传给 PSB 加载器的名字是 **`lzfs:`**（只剩 media 名），所以 PSB 把它当档案名
去 `loadPSBFile` 必然失败。注意**路径并没有在存储层被丢掉**：`psb://lzfs://./x.psb` 规范化后
是 `lzfs:/x.psb`，信息完整，丢的只是"档案边界应该切在 `.psb/`"这件事（见上）。

对照：AetherKiri 同一条链路用的是扩展名感知的 `ArchiveBoundaryKey()`，所以不吃这个亏。

参照：另一款游戏（G2）的 Live2D 走的是本仓库原生 Cubism（`krkrlive2d`），不是 E-mote；
本作走 motionplayer 的 `drawFallback` 路径，两者不共用加载器。

---

## 2.7 チート緊縛術（`KRKR_チート緊縛術で爆乳孕ませハーレム…`）— 两个独立问题

**状态：均未修；一缺日志、一缺脚本对照**

### 2.7.1 classic 层：`Member "showLayers" does not exist` → 引擎退出（脚本层）

```
#(1) showLayers(sf.effection_page)
Member "showLayers" does not exist at anonymous@0x…(1)[(top level script) global]
trace : mainwindow.tjs(5777)[(function expression) (anonymous)] <-- conductor.tjs(440)[(function) onTag] <-- …
ファイル : scenario.ks  行 : 223   タグ : eval
TVPShowSimpleMessageBox: title='Information' … → TVPExitApplication → engine_destroy
```

关键事实：`showLayers` **在本仓库与 AetherKiri 都未注册**（`grep -rn showLayers cpp/` 两边都空），
所以它不是引擎 API。TVP2 的语义是未捕获脚本异常 → `TVPTerminateAfterScriptException` →
运行时终止（这就是引擎退出的原因）。

该作目录带 `patch.xp3`（1 dir, 29 files）+ `claude-3-5-sonnet-20240620翻译补丁备份` +
`hook.ini` + `FONTCHANGER.dll`（本引擎加载失败）——**疑似翻译补丁替换的 `mainwindow.tjs`
与游戏本体 KAG 版本不一致、少了 `showLayers` 的定义**。

需要用户提供：`data.xp3>mainwindow.tjs` 与 `patch.xp3` 里的同名文件（若存在）第 5777 行附近，
以及 `showLayers` 的定义处。拿到就能判定是补丁缺函数，还是要引擎补。

### 2.7.2 AetherKiri 层：字体渲染不正确（渲染/字体层）

用户反馈：AetherKiri 层能正常进、对应场景不崩，但**字体渲染不正确**。

**当前缺日志**：该游戏目录里只有 classic 层那次 `engine-20260922-224201.log`。
要 AetherKiri 层那次的：
- `FontSystem: 已注册字体 N 个 -> …`（看游戏自带的 `ShiraYukiNoa.otf` 有没有注册进去）；
- `Specified option(s) … font_fallback_mode=…`（该作 per-game 配置写的是 `legacy`，
  AetherKiri 层跑的是 `auto`）；
- `GetBeingFont` / 缺字 / 字形回退行。

候选根因（未证实）：该作自带 `ShiraYukiNoa.otf` + `hook.ini` + `FONTCHANGER.dll`，而
`FONTCHANGER.dll` 在本引擎**加载失败**（日志有 `Loading Plugin: …/FONTCHANGER.dll Failed`）
——字体很可能就是靠这个插件换的。

可立即自测：在游戏设置 → 字体回退里把 `legacy` / `auto` 各试一次（两者实现不同：
单一回退字面 vs 逐字回退链 + 基线对齐），能区分"字体没注册"与"回退策略差异"。

---

## 3. 渲染器架构评估（背景，非 issue）

详见 `render-diff.md`。三点结论：
- **耦合**：接口 `iTVPRenderManager` / `iTVPTexture2D` 干净，但实现层 GL 知识散落在
  `core/visual/ogl/`、`plugins/krkrgles.cpp`（3764 行）、`environ/stubs/ui_stubs.cpp`、
  `environ/EngineBootstrap.cpp`；核心接口泄漏 `GLuint`（`TVPSetRenderTarget`）与
  `krkr::Texture2D`；EGL 是进程级单例 + 临时重建钩子（**没有 context generation 概念**）。
- **性能**：纹理缓存 / shader 缓存 / framebuffer-fetch 都在；但
  `TVPTextureHasStorage` **每次建纹理都做一次 FBO 校验**（固定开销）、
  `krkr_gl.cpp` 的状态缓存是空壳（注释自述 "Always call GL directly"）、
  ES3 上 `GL_CHECK_unpack_subimage` 多半为 false（退化成逐行拷贝）。
- **兼容**：原生 EGL+GLES3（上游是 ANGLE+GLES2）。格式/扩展覆盖好、降级齐全；
  ES2-only 残留只剩 **PVR3 `InitPixel`**（`render-diff.md §4` R1，未修）；
  NDK 存根缺的 ES3 符号由 `krkr_gl3_shim.cpp` 运行期解析。

---

## 4. 本地验证命令

```bash
bash scripts/check_static.sh          # JNI 符号 / 移植清单 / 语法
python3 scripts/check_port_drift.py   # 移植漂移（改了 ported 文件要 --update）
# 探针构建（唯一能拿到 probe: 日志的方式）：
gh workflow run "Android 构建" --repo clevebitr/Krkr2Next --ref main \
  -f build_type=debug -f enable_render_probe=true
```
