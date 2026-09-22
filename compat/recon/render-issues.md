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

**状态：图片路径正常；三个独立问题待处理**

已核实的现状：
- `probe: Layer.loadImages(psb://quickmenu.pimg/*.tlg)` 一串 ⇒ 图片/E-mote 加载路径正常。
- **`Transition handler 'wave' not found, falling back to crossfade`** ⇒ `wave` 转场未实现。
- **卡死**：`.stall` 记 `render-thread-stall`，阶段 `Application::Run: SystemWatchTimerTimer`
  （与 G2 同源，见 §1 未解决 ③）。
- `convertImage: RL decode failed … raw palette` ⇒ **已知小图标回退**
  （`cpp/plugins/psbfile/PSBMedia.cpp` 有注释：m2logo icon32/icon18 的未压缩调色图被标成 RL），非根因。
- **SDCG 层级**（Yuzusoft 的 SD/emote 角色应绘在 UI 之上，实际落到错误层级）
  - 机制：SD 分件是 `data1080.xp3` 的 `sdNNN.mtn` + `SDNNNAA.png`，由 motionplayer 经
    `Motion.SeparateLayerAdaptor` 承载；`patch.tjs` 设 `Motion.Player.useD3D = 0`，
    于是走 adaptor 的私有渲染层——**它就是可见的呈现层**（脚本不会把它再拷回 owner）。
  - 根因：`motionplayer/main.cpp::GetSeparateAdaptorRenderTarget` 把该渲染层挂到
    `window.primaryLayer`。参考实现（krkrsdl3；AetherKiri
    `PlayerRender::resolveSeparateLayerRenderTarget`）把它建成**构造函数 owner 层的子层**
    （owner 是游戏放在正确 z 序上的 `AffineLayer`，脚本随后把 `owner.type` 改成
    `ltBinder`，渲染层紧贴其上）。挂错父层 ⇒ SD 整体层级不对。
  - 已改（2026-09-22）：owner 能解析为真实 Layer 时以 owner 为父层，并把子层 left/top
    归零（子层坐标相对 owner，否则被 owner 位置再偏移一次）；否则维持原 primaryLayer
    回退。另按参考实现把 `SeparateLayerAdaptor.assign` 补成 no-op（参考注释：拷回
    owner 会得到第二张偏移画面）。
  - 待验证：真机确认 SD 是否已绘在 UI 之上。插件新增一条一次性路由日志：
    `motion: SeparateLayerAdaptor 渲染层路由 owner=… parent=… parentIsOwner=… parentName=…`
    （每次创建 adaptor 一条，封顶 8 条），用于确认走的是哪条父层路径。

**下一步**：
1. `wave` 转场：按 KAGEX 规范补实现（确定的功能缺口，可独立做）。
2. SDCG 层级：已按参考实现改父层（见上），**待真机回归**；若仍不对，用那条路由日志
   确认 `parentIsOwner` 与 `parentName`，再判断是父层选择还是合成路径问题。
3. 卡死：与 §1 未解决 ③ 同一处理（`SystemWatchTimerTimer` 细阶段探针）。

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
