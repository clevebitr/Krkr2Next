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
- **已内置探针**（默认关，日志前缀 `probe:`）：
  | 探针 | 位置 | 次数上限 |
  |---|---|---|
  | `probe: Storages.isExistentStorage(<arg>) #N` | `cpp/core/io/IoStorage.cpp` | 前 64 次 |
  | `probe: Layer.loadImages(<arg>) #N` | `cpp/core/visual/LayerIntf.cpp` | 前 64 次 |
  | `probe: PropGet miss '<member>' (compatFallbacks=N)` | `cpp/core/tjs2/tjsObject.cpp` | 前 32 次 |
  | `probe: AMV header 失败：<tjGetErrorStr2>（size=…，first=XXXX）` | `cpp/core/visual/LoadAMV.cpp` | 无（每帧） |
  | `probe: AMV tjDecompress2 失败：…` | 同上 | 无 |
  | `probe: AMV DQT 注入重试 ok=N dqtSeg=…B jpeg=…B` | 同上 | 无 |
- 触发探针构建：
  ```bash
  gh workflow run "Android 构建" --repo clevebitr/Krkr2Next --ref main \
    -f build_type=debug -f enable_render_probe=true
  ```
  （工作流 `concurrency: cancel-in-progress`：连推/连触发时前一条会显示 cancelled，属预期。）

---

## 1. NEKOPARA 4（`KRKR__官中_NEKOPARA_4`）— 视频无法解码

**状态：根因已定位到数据层；待补"载荷 dump"探针**

证据（`engine-20260921-210738.log`）：
```
AMV: 1280x720, 60 frames, mode=jpeg
probe: AMV header 失败：Could not determine subsampling level of JPEG image（size=7091，first=CEA1）
probe: AMV header 失败：…（size=7292，first=FFD8）
probe: AMV DQT 注入重试 ok=0 dqtSeg=199B jpeg=7091B
AMV: color JPEG decode failed at kaglayer.tjs(1)[(function) loadImages]
画像ロード失敗, AMV: color JPEG decode failed
```

载荷 dump 探针实测（`engine-20260922-065143.log`）：
```
probe: AMV payload first32=CEA1DB873E1CF0CFFC8C9A563FE7EE2FFFD0C57EA94102C49EA7B9AFCADF0CFFC
       payloadLen=74160 extraHdr=12 sizeOfFrame=74172
```
- 载荷**不以 JPEG SOI 开头**；首个 `FF D8` 在偏移 **7091**——`FindSecondSOI` 把它当成了
  “第二个 SOI”，即把 7091 误当成彩色段长度。
- 前 32 字节跨帧高度重复（结构化数据，非加密/压缩）；`firstFFD8` 逐帧变化很大
  （7091 / 1827 / 11127 / 1269 / 40821），而 `payloadLen` 恒定 ≈74 KB
  ⇒ 载荷是 **`[变长前缀][JPEG…]`**；`extraHdr` 与 `sizeOfFrame` 口径自洽。
- 结构探针（全部 SOI 偏移 + 尾部找 `FF D9` + 头部变体字段）已在 `LoadAMV.cpp` 就位。

**下一步**：
1. 按 `SOI_count` / `tail16` 判定前缀边界与是否含 alpha 段，再修 `payloadStart` / `colorSize`。

---

## 2. nainiuniu5krkr（G2）— 启动期 `diffimage2.tjs` 无限递归

**状态：根因已定位并已修（待真机回归）**

完整因果链（`engine-20260922-065121.log`，每步均有日志/反编译证据）：
1. `startup.tjs`(exec#1) 跑完第一遍 KAG 初始化（`diffimage2.tjs` = exec#97）。
2. `mainwindow.tjs saveSystemVariables` 里 `try { … Storages.commitSavedata() }`
   抛 `Member "commitSavedata" does not exist`，进入 `catch` 又调
   `Storages.rollbackSavedata()` → 同样不存在 → **异常从 catch 里抛出**。
3. `StartupProbe: startup.tjs threw(eTJSScriptError) … msg=Member "rollbackSavedata" does not exist`
   → 引擎兜底 `StartupProbe: running FALLBACK system/Initialize.tjs`。
4. 第二遍初始化重跑（`diffimage2.tjs` = exec#229）⇒ `Storages.isExistentStorage` 被**包装两次**。
5. `diffimage2.tjs` 的包装在**调用时**读全局 `diffOrigIsExistentStorage`；二次加载后该全局
   指向第一次的包装 W1 ⇒ W1 调 W1，而该包装**没有** `diffEnterCount` 兜底 ⇒ 无限递归 → 栈耗尽。

关键事实：
- **假设 (A) 排除**：`probe: PropSet miss 'diffEnterCount'` 一条都没有；且
  `tTJSExtendableObject::PropSet` 覆盖了 `tTJSCustomObject::PropSet`，探针位置本就不会触发。
- **回退失效的真正原因**：`TJSCompatIsStartupNoOpFunction` 里**本来就有** `commitSavedata`，
  但 A 块回退只挂在 `tTJSCustomObject::PropGet` 上；而 `Storages.commitSavedata()` 是
  **方法调用**，走 `FuncCall`、不经 `PropGet` ⇒ 回退永远没被问到。

**已修**（`tjsObject.cpp`）：
- 新增 `TJSCompatResolveFuncCallFallback()`，在 `tTJSCustomObject::FuncCall` 的
  `!data` 分支挂上与 `PropGet` 同名单、同顺序的回退链（仅 AetherKiri 层）。
- `rollbackSavedata` 加入 `TJSCompatIsStartupNoOpFunction` 名单。

**背景（保留）**：`tjsInterCodeExec.cpp` 有一段**上游没有**的原生栈保护（256KB 余量），
把原本的 SIGSEGV 转成了脚本异常；它只是兜底，不是修复。

---

## 3. 千恋万花（`KRKR汉化高压_千恋万花`）— 多个独立问题

**状态：图片路径正常；三个独立问题待处理**

证据（`engine-20260921-210834.log`）：
- `probe: Layer.loadImages(psb://quickmenu.pimg/*.tlg)` 一串 ⇒ 图片/E-mote 加载路径正常。
- **卡死**：`*.log.stall` 4 条 `render-thread-stall`：
  - `movie: Close→清理窗口消息/状态`（1.6s）
  - `engine_tick: Application::Run（脚本+合成+绘制）`（1.5s ×3）
- **`Transition handler 'wave' not found, falling back to crossfade`** ⇒ `wave` 转场未实现。
- `convertImage: RL decode failed … raw palette` ⇒ **已知小图标回退**
  （`cpp/plugins/psbfile/PSBMedia.cpp` 有注释：m2logo icon32/icon18 的未压缩调色图被标成 RL），
  非根因。
- 上一次（`engine-20260921-203751.log`）`DrawVideoOverlay` 早期帧是**灰阶**
  （`画后=(253,253,253)`、`(182,182,182)`、`(196,196,196)`，第 120 次才 `(237,222,176)`）——
  若"logo 颜色丢失"指**开场视频**，属视频管线；本次未播视频所以没复现。

**下一步**：
1. `wave` 转场：按 KAGEX 规范补实现（确定的功能缺口，可独立做）。
2. 卡死：`.stall` 只给了阶段名；需要更细的探针（渲染线程当时在做什么），
   或复现时抓 `/data/anr` + `logcat`。
3. "无法渲染 CG"（用户 2026-09-22 明确）：**SDCG 无法正确渲染在 UI 之上**。
   需要定位是图层层级顺序问题，还是 SD 图层的合成路径问题。

---

## 4. 渲染器架构评估（背景，非 issue）

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

## 5. 本地验证命令

```bash
bash scripts/check_static.sh          # JNI 符号 / 移植清单 / 语法
python3 scripts/check_port_drift.py   # 移植漂移
# 探针构建（唯一能拿到 probe: 日志的方式）：
gh workflow run "Android 构建" --repo clevebitr/Krkr2Next --ref main \
  -f build_type=debug -f enable_render_probe=true
```
