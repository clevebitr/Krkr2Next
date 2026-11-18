# 对照证据：渲染层（KiriNext vs AetherKiri）

> 证据文件，非规范。`KN/` = `KiriNext`，`AK/` = `AetherKiri`（rev `bd14a986`）。
> 主题：**AetherKiri 兼容层与渲染层的耦合**，以及 `cpp/core/visual/ogl/` 相对上游 AetherKiri
> 的功能改动。**不**重复 `kag-script-diff.md`（KAG 脚本层）与 `io-loading-diff.md`（IO）。
>
> **当前未解决的渲染问题**（三款游戏实测）见 `render-issues.md`（新会话先读那份）。

---

## 0. 结论摘要

1. **本仓库的 GL 渲染器就是 AetherKiri 的**。`KN/cpp/core/visual/ogl/` 与 `AK/cpp/core/visual/ogl/`
   是同一套文件（`RenderManager_ogl.cpp` / `krkr_gl.*` / `krkr_egl_context.*` / `krkr_texture2d.h` /
   `ogl_common.h`）。KN 的改动集中在：**去 ANGLE → 平台原生 EGL**、**ES2 上下文 → ES3**、
   删掉 macOS IOSurface、GLES3 格式适配、纹理存储自愈、EGL context 重建时重建 FBO/清图形缓存。
2. **AetherKiri 兼容层与渲染只有一条耦合**：档 `aetherkiri` 映射 `ogldrawdevice_compat=alias`
   （渲染闸门），KAGWindow 接管则由激活的 AetherKiri 层在 krkrgles post-regist 安装。
   层本身（IO 策略 / TJS 回退 / 模块门）**不碰**渲染器。
   > 变更记录（2026-09-19）：旧的 `kag` 渲染档与 `krkrz-kag` 兼容档已删除，能力归本层。
3. **AetherKiri 的脚本级 GPU 兼容机制已部分移植**：伴生脚本替换（GPU 占位脚本 /
   motion-parameter / split-emote）经 `io/IoVirtualFile` + `compat/AetherKiriCompanions.cpp`
   落地，仅在 AetherKiri 层生效；`drawDevice`/`gpuDrawDevice`/`nativeDrawDevice` 别名与核心
   全局镜像已由 `krkrgles` 的 AetherKiri 层分支补上（见 §3.2）；`gfxEffect`/`logwindow`/
   `D3DEmote` 伴生脚本未移（见 §3.1）。
4. 因此"**AetherKiri 层渲染问题**"应拆成两类分别定位：
   - (a) `kag` 档本身的已知缺陷（`krkrgles.cpp` 注释已记：对 G2 有 `FBO incomplete 0x8CD6`）；
   - (b) `kag` 档下 AetherKiri 游戏需要的**脚本级 GPU 兼容缺失**（本仓库 `kag` 不是 AetherKiri
     的 `kag`）。

---

## 1. 耦合链路（壳 → 引擎 → 渲染）

```
设置里选 RunMode.AETHERKIRI
  → 壳下发两个原始值：game_compat_profile=aetherkiri、ogldrawdevice_compat=off
      app/.../core/RunMode.kt（AETHERKIRI 一对值；off = 不显式发，交给档）
      app/.../core/EngineSession.kt:420-421（off 之外显式下发 ogldrawdevice_compat）
      app/.../core/EngineSession.kt:474-476（openGame 前下发 game_compat_profile）
  → engine_api 解析档：aetherkiri v1 → ogldrawdevice_compat=alias + SetActiveLayer(AetherKiri)
      bridge/engine_api/src/engine_api.cpp（kCompatProfiles / ApplyCompatProfileLocked）
  → 兼容层激活：IO 策略 / TJS 内核回退（TJSCompatFallbacksEnabled）/ 模块归属门
      cpp/core/compat/CompatLayer.cpp:88-120
  → krkrgles 插件 post-regist 检查 ActiveLayer()==AetherKiri：
      挂 OGLDrawDevice/GLESAdaptor 别名 + 每帧重试装 KAGWindow_createDrawDevice
      cpp/plugins/krkrgles.cpp（KrkrGlesPostRegist）
```

要点：

- 渲染器选择**与兼容层无关**：`TVPGetRenderManager()` 固定取 `renderer=opengl`
  （`KN/cpp/core/visual/RenderManager.cpp:4953-4969`；`REGISTER_RENDERMANAGER(TVPRenderManager_OpenGL,
  opengl)` 在 `RenderManager_ogl.cpp` 末尾，`EngineBootstrap.cpp:66` 强制链接）。
- 兼容层**唯一**影响渲染的就是 `ogldrawdevice_compat` 这个既有选项；没有第二个渲染开关。
- 壳把"兼容层"和"渲染档"焊成了固定组合（`RunMode`），所以用户**无法**单独选
  "AetherKiri 层 + off/ogl/alias"。已知 `kag` 对部分游戏有害（见 `engine_options.h:45-53`），
  这一焊死会把这些游戏的渲染问题直接暴露在"AetherKiri 层"名下。

---

## 2. 渲染实现溯源与功能差异

### 2.1 文件级对照

| 文件 | 关系 | 主要改动 |
|---|---|---|
| `ogl/ogl_common.h` | 改 | 去 ANGLE 注释；补 GLES3 枚举（`GL_R8/GL_RG8/GL_RED/GL_RG/GL_TEXTURE_SWIZZLE_*`）；新增 `TVPTextureHasStorage()` |
| `ogl/krkr_texture2d.h` | 改 | 删 `KRKR_ENABLE_GPU_BRIDGE` 双路（CPU 像素兜底路径整段删除，恒走 GL）；内部/客户格式分离；luminance→R8/RG8+swizzle；上传后校验存储并在失败时重建为 RGBA8 |
| `ogl/krkr_egl_context.{h,cpp}` | 改 | 删 ANGLE（`AcquireAngleDisplay` → `AcquireDisplay`）、删 IOSurface、删 `presented_frame_serial_`；EGL config/context 从 **ES2 → ES3**；日志文案 |
| `ogl/krkr_gl.cpp` / `krkr_gl.h` | 改（含格式） | 主要是缩进；`krkr_gl.cpp` 实质仅注释 |
| `ogl/RenderManager_ogl.cpp` | 改 | ES3 扩展枚举（`glGetStringi`）、luminance 格式适配、纹理存储自愈、context 重建时重建 FBO + `TVPClearGraphicCache()`、补 `<GLES3/gl3.h>`；少量 `snprintf→sprintf`、`switch((int)x)→switch(x)` |
| `ogl/krkr_gl3_shim.cpp` | **新增** | Android NDK 的 `libGLESv2.so` 存根不导出 `glGetStringi/glBlitFramebuffer/glMapBufferRange/glUnmapBuffer`，用 `eglGetProcAddress` 运行期解析 | 
| `ogl/angle_backend.h` | **删除** | ANGLE 后端枚举 |
| `ogl/PVRTDecompress.cpp`、`imagepacker.cpp`、`astcrt.cpp`、`etcpak.cpp`、`pvrtc.cpp` | 改 | 未逐一核对（本次未展开） |

### 2.2 EGL / 上下文：ANGLE → 原生，ES2 → ES3

- AK（Android）：`eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, …, Vulkan→OpenGLES)` +
  `EGL_OPENGL_ES2_BIT` + `EGL_CONTEXT_CLIENT_VERSION=2`。
- KN：`eglGetDisplay(EGL_DEFAULT_DISPLAY)` + `EGL_OPENGL_ES3_BIT` +
  `EGL_CONTEXT_CLIENT_VERSION=3`（`KN/cpp/core/visual/ogl/krkr_egl_context.cpp:106-107,136,364-365,402`）。
- **这是最大的一处语义切换**：整套 `RenderManager_ogl.cpp` 来自 ES2 时代，ES3 上下文把
  若干"ANGLE 曾容忍"的 ES2-only 用法暴露成错误（luminance、`glGetString(GL_EXTENSIONS)` 等）。
  KN 的本地修复是在**逐条补**，只覆盖了被发现的那几条（见 §4）。

### 2.3 GLES3 扩展枚举

- AK：`glGetString(GL_EXTENSIONS)`（ES2）。
- KN：ES3 上下文改用 `glGetStringi(GL_EXTENSIONS, i)` + `GL_NUM_EXTENSIONS`
  （`KN/cpp/core/visual/ogl/RenderManager_ogl.cpp:118-184`）。
- 影响：修复前扩展集为空 → `GL_CHECK_unpack_subimage` / `GL_CHECK_shader_framebuffer_fetch`
  等全部为假，**静默性能回退**（无报错）。

### 2.4 Luminance / Alpha 格式 → R8/RG8 + swizzle

- 起因：ES3 不再接受 `GL_LUMINANCE`/`GL_LUMINANCE_ALPHA`/`GL_ALPHA` 作为内部格式；
  继续用会 `GL_INVALID_ENUM` 且**不分配纹理存储**。
- KN：`TVPLuminanceFormat`（`RenderManager_ogl.cpp:904-1000`）+ `krkr_texture2d.h` 的
  `resolveGLFormat` 内部/客户格式分离 + `applyLuminanceSwizzle`。
- 语义映射：`Gray→R8+(R,R,R,1)`、`GrayAlpha→RG8+(R,R,R,G)`、`Alpha→R8+(0,0,0,R)`。
  swizzle 是纹理对象状态，必须在绑定后设置。

### 2.5 纹理存储自愈（FBO incomplete 的兜底）

- KN 在每次 `glTexImage2D` 后调 `TVPTextureHasStorage()`（`ogl_common.h`），失败就打印参数并
  用 RGBA8 重建。覆盖：`tTVPOGLTexture2D` 构造、`InitPixel` 之外的多条上传/resize/restore、
  split 纹理、restore-new。
- 这是对真机 `SourceSample: FBO incomplete 0x8CD6` + 全黑的对症兜底，不是根因修复。

### 2.6 EGL context 重建

- KN：`krkr::gl::OnRendererRecreated` 回调里除重建 shader 外，**重建 `_FBO`/`_stencil_FBO`**
  并 `TVPClearGraphicCache()`
  （`KN/cpp/core/visual/ogl/RenderManager_ogl.cpp:3180-3228`）。
- 触发点在 `EngineBootstrap::InitializeGraphics()` → `krkr::gl::FireRendererRecreated()`
  （`KN/cpp/core/environ/EngineBootstrap.cpp:140-146`）。
- 背景：runtime-restart 会 `Shutdown()`（销毁 EGL）再 `Initialize()`；渲染器是进程级单例，
  旧 context 的 GL id 全部失效。

### 2.7 其它

- `krkr_texture2d.h` 删除了 `KRKR_ENABLE_GPU_BRIDGE` 的 CPU 兜底分支：现在**恒**走 GL。
  KN 全树已无 `KRKR_ENABLE_GPU_BRIDGE` 定义（`grep` 为空）⇒ 该头在 AK 里的
  "无 GL 桥时的纯 CPU Texture2D"能力**在 KN 不存在**。
- `RenderManager_ogl.cpp` 中 `snprintf(sCounter, sizeof(sCounter), …)` 被改成
  `sprintf(sCounter, …)`（两处），`switch((int)format)` 改成 `switch(format)`。前者丢掉了
  边界保护，属**行为回退**（当前 `sCounter[8]`、`i<m_nTex` 下不会溢出，但不能算改进）。

---

## 3. 脚本级 GPU 契约差异（`kag` 档 / AetherKiri 层）

AetherKiri 让 GPU 系游戏能渲染，靠的是**两套脚本侧机制**；KN 都没有完整移植。

### 3.1 GPU 伴生脚本的虚拟替换（**部分实施 2026-09-21**）

- AK `StorageIntf.cpp` 在存储层内置：
  - 谓词 `TVPIsGpuCompanionScript`（11 个名字：`gpulayer.tjs`、`gpuaffinelayer.tjs`、`d3d.tjs`、
    `d3daffinesource.tjs`、`d3daffinesourcepicture.tjs`、`d3daffinesourceimage.tjs`、
    `d3daffinesourcemotion.tjs`、`d3daffinesourcelive2d.tjs`、`d3daffinesourceemote.tjs`、
    `affinesourcelive2d.tjs`、`live2d.tjs`）——`AK/cpp/core/base/StorageIntf.cpp:380-393`
  - `TVPIsExistentStorageNoSearchNoNormalize` 认它为存在（`:1302-1304`）
  - `TVPNormalizeStorageName` 直接返回规范化名（`:2023-2026`）
  - `_TVPCreateStream` 在读且真实文件不存在时返回内嵌 `TVP_GPU_COMPAT_SCRIPT`（`:2178-2184`）
  - 脚本本体：`AK/cpp/core/base/impl/GpuCompatScript.h`
- 另有 `TVPGetMotionParameterCompanionInfo`（`motion_<asset>.psb/.mtn.tjs` →
  `%["storage" => "<asset>.psb"]`，E-mote 动作解析的关键）与 `TVPIsSplitEmoteVirtualStorage`
  （`*emo.psb/.mtn/.mt` → 空流），消费点同上。
- **KN 已实施（2026-09-21）**：`io/IoVirtualFile.{h,cpp}` 提供泛型虚拟文件注册点（provider 只
  产出内容，io 包成流，且**物理文件优先**）；`compat/AetherKiriCompanions.cpp` 注册 provider，
  覆盖 GPU 占位脚本（11 名）+ motion-parameter + split-emote，**只在 AetherKiri 层生效**，
  每个名字打一条命中日志（`compat companion: 虚拟提供 …`）。
- **未移**：`gfxEffect`（`gfx_fire.tjs`/`gfx_flash.tjs`，需模块存在性检查）、
  `logwindow.tjs`（~90 行内嵌 KAGEX LogWindow 类）、`motion.tjs`/`d3demote.tjs`
  （需 36KB 生成 blob `D3DEmote.tjs`）。
- 注意：**真实文件存在时仍加载真实脚本**（与上游一致），所以走真 GPU 层脚本的作品仍会因
  KN 没有 Canvas/Texture/ShaderProgram 而抛 `mixinclass.tjs [(function) missing]`——这不是
  伴生脚本能修的，属于渲染器能力差。

### 3.2 `drawDevice` 别名成员

- AK `InstallDrawDeviceScriptAliases`（`AK/cpp/plugins/krkrgles.cpp:130-188`）对
  `{KAGWindow, kag, KAGWorldPlugin}` 逐个写：
  - `drawDevice` = `new OGLDrawDevice()/GLESAdaptor()` 实例
  - `gpuDrawDevice` = 同一实例
  - `OGLDrawDevice` / `GLESAdaptor` = 全局类引用（镜像）
  - 若 `TVPMainWindow` 存在：`nativeDrawDevice` = 主窗口的 CPU draw device
  - 另把 `System/Storages/Scripts/Dictionary/Debug/Math/Plugins/Window/dm` 镜像到三目标
- AK 在 `KrkrGlesPostRegist` **无条件**安装（`AK/cpp/plugins/krkrgles.cpp:2538-2546`），失败则
  每 tick 重试、上限 600（`:198-232`）。
- **已实施（2026-09-21）**：KN `krkrgles.cpp` 的 AetherKiri 层分支已补上上述契约
  （`KrkrOglAetherKiriContract`）：`drawDevice`/`gpuDrawDevice`/`nativeDrawDevice` +
  核心全局镜像，对 `{KAGWindow, kag, KAGWorldPlugin}` 安装。采用上游的
  `EnsureScriptMember` 语义（`TJS_MEMBERMUSTEXIST` 探测，**已存在不覆盖**），
  避免破坏游戏框架自己的 `drawDevice`。安装一次（等 4 个别名目标全部就位后），
  日志 `krkrgles: AetherKiri draw-device 契约已安装（… ok=N）`。
- 原有的 `KrkrOglAliasFanOut`（写 `OGLDrawDevice`/`GLESAdaptor` 类引用）保持不变；
  两者互补：扇出负责闸门（刻意覆盖），契约负责实例与全局镜像（不覆盖）。

### 3.3 全局镜像 vs A 块回退

- AK 的镜像列表有 `dm`；A 块全局回退（`KN/cpp/core/tjs2/tjsObject.cpp:1499-1526`）的 34 名里
  **没有** `dm`，也没有 `nativeDrawDevice`/`gpuDrawDevice`/`drawDevice`。
- 因此"靠 A 块回退顶替镜像"只对 `System/Storages/Scripts/…` 等有交集的名字成立；GPU 设备名不在内。

---

## 4. 仍未覆盖的 ES2-only / 潜在风险

| # | 位置 | 现象 | 级别 |
|---|---|---|---|
| R1 | `KN/cpp/core/visual/ogl/RenderManager_ogl.cpp:4503-4519`（PVR3 → `InitPixel`，`:1821`） | PVR3 非压缩 `A8/L8/LA88` 走 `InitPixel`，把 `GL_ALPHA`/`GL_LUMINANCE`/`GL_LUMINANCE_ALPHA` 当内部格式传给 `glTexImage2D`；该段**上游同源**（未在 diff 内），但其 `pixfmt` 实参传的是 GL type（`GL_UNSIGNED_BYTE`），在 ES2 下也属非法。ES3 下又叠加 luminance 内部格式非法 → 双重失效，且该路径**没有** §2.5 的自愈 | 高（仅 `.pvr` 非压缩 A8/L8/LA88；上游遗留，非 KN 回归） |
| R2 | `GL_CHECK_unpack_subimage`（`RenderManager_ogl.cpp:256`） | ES3 驱动通常不再列出 `GL_EXT_unpack_subimage`（已是 ES3 核心）⇒ 标志为假，走逐行拷贝回退。正确性不变，**性能**回退 | 中 |
| R3 | `RenderManager_ogl.cpp` 两处 `sprintf(sCounter[8], "%d", i)` | 丢掉 `snprintf` 边界保护，属回退 | 低 |
| R4 | `krkr_texture2d.h` 删 CPU 兜底 | AK 用 `KRKR_ENABLE_GPU_BRIDGE` 区分 GL/CPU 两路；KN 删掉该宏、恒走 GL。无 GL 时的 CPU 纹理能力在 KN **不存在**（信息项，非缺陷） | 信息 |
| R5 | 壳把兼容层与渲染档焊死 | 无法"AetherKiri 层 + off/ogl/alias"；已知 `kag` 对 G2 有害却被强制 | 设计 |
| R6 | `AK` 伴生脚本/别名未移植 | AetherKiri 系 GPU 游戏在 KN `kag` 档下拿不到 AetherKiri 的脚本契约（§3） | 高 |

> 说明：R1 只在 `.pvr` 非压缩 A8/L8/LA88 路径成立，尚未真机证实有游戏命中；但它与 §2.4
> 修的是**同一类** ES3 错误，只是漏了 `InitPixel` 这一条路径。

---

## 5. 建议的下一步

### 5.1 先取证（避免继续猜）

1. 真机对同一游戏分别跑 `classic` / `kag` / `aetherkiri` 三档，收集引擎日志里的：
   - `compat layer: 激活层…`、`compat profile: … -> ogldrawdevice_compat=…`
   - `krkrgles: ogldrawdevice_compat=…`、`AetherKiri 层接管完成（…别名 N/4…）`
   - `krkrgl: texture storage missing …` / `krkrgl: … rebuilding as RGBA8`
   - `SourceSample: FBO incomplete`、`BlackScreen`
2. 在 GPU 脚本加载点加一条**默认关闭**的 `KRKR_RENDER_PROBE` 探针，只记录
   "脚本名 + 解析到真实文件还是缺失/异常"，用来区分 §3.1 的两种情况。

### 5.2 修复候选（按性价比）

1. ~~**补 §3.2 别名成员**~~ **已实施（2026-09-21）**：AetherKiri 层分支已补
   `KrkrOglAetherKiriContract`（`drawDevice`/`gpuDrawDevice`/`nativeDrawDevice` + 核心全局
   镜像），对 `{KAGWindow, kag, KAGWorldPlugin}` 安装，采用**非覆盖**语义。
2. ~~**解耦 §R5**~~ **已实施（2026-09-19）**：删除 `kag` 档，KAGWindow 接管归 AetherKiri 层；
   `aetherkiri` 档映射 `alias`，`auto` 对 `motionplayer*` 直接激活 AetherKiri 层。
3. **补 §3.1 伴生脚本替换**（未做）：在 `io` 存储层加一个**泛型虚拟文件**提供点，由
   AetherKiri 层注册 `TVP_GPU_COMPAT_SCRIPT`，消掉"真实 GPU 脚本抛 `mixinclass.tjs missing`"。
4. **补 §R1**（未做）：把 PVR3 的 `InitPixel` 也走 `TVPLuminance*` + swizzle，或复用 §2.5
   的存储校验。

> 状态（2026-09-21）：§3.2 与 §R5 已实施；§3.1、§R1 未做。渲染改动要小（AGENTS §6），
> AetherKiri 层是实验档，不默认开启。
