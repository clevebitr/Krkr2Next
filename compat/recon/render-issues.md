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

判读（已核实的事实）：
- `LoadAMV.cpp` 假设"帧载荷第 0 字节就是彩色 JPEG 的 SOI（`FF D8`）"，实测是 **`CE A1`**
  ⇒ 载荷前面有前缀，或这份 AMV 是变体。
- DQT 注入（前置 `FF D8`+DQT）后前缀对了，TurboJPEG 仍报"无法确定子采样等级"
  ⇒ 后续字节也不是合法 JPEG 体。
- `LoadAMV.cpp` 与上游 AetherKiri whitespace-ignoring diff **语义逐字一致**
  ⇒ 不是移植偏差。
- `vol4video.xp3` 是 **Cx 保护包**（`Protected XP3 payload is already decoded`），
  本机 xp3 解析器取不出内容。

**下一步**：
1. 加探针：dump 帧载荷前 32 字节 + 第一个 `FF D8` 的偏移（`TVPLoadAMV` 读完 `payload` 处）。
2. 按前缀长度修正 `payloadStart` / `colorSize`；若确认为变体（彩色/alpha 顺序不同、
   有额外 per-frame 头），按实测结构处理。
3. 顺带确认 `extraHdr = sizeof(AMVJpegFrameHeader) - 8` 与 `size_of_frame` 的口径是否一致。

---

## 2. nainiuniu5krkr（G2）— 启动期 `diffimage2.tjs` 无限递归

**状态：范围已收窄到两种可能；待补 TJS 层探针**

证据（`engine-20260921-210714.log`）：
```
StorageExec: exec#246 name=exroll.tjs
Script call stack exhausted (recursive call?) at diffimage2.tjs(1)[(function expression) (anonymous)]
（trace 只有 diffimage2.tjs 的 anonymous 重复）
```
探针结果：
- `probe: Layer.loadImages` **整局 0 条** ⇒ 原生 `Layer.loadImages` 从未被调用。
- `probe: Storages.isExistentStorage` 正好 64 条（全是启动期 `*.xp3`，**无 `.dref`**），
  被 64 条限额截断。
⇒ **递归期间没有任何原生方法被调用**，递归完全发生在 TJS 包装函数内部。

`diffimage2.tjs`（已从 `data.xp3` 解包 + 反编译，源存于会话机 `~/diffimage2.src.tjs`）：
- `global.Layer.loadImages = wrapper`（先存 `diffOrigLoadImages`），内部
  `global.Layer.loadImages(local5)` **按名字自递归**，兜底
  `if (global.Layer.diffEnterCount > 100) throw new Exception(...)`。
- `global.Storages.isExistentStorage = wrapper`（先存 `diffOrigIsExistentStorage`），
  内部 `local2(...)` 调保存的原函数，**无兜底**。
- 游戏里确实有大量 `.dref`（`evimage.xp3` 646 个、`patch_append1.xp3` 5 个），
  内容是 `psb://…/*.tlg` 的引用列表。

两种可能（**二选一即可定位**）：
- **(A)** 脚本的兜底 `Layer.diffEnterCount` 没生效 —— 若原生类对象**不接受新增静态成员**，
  `global.Layer.diffEnterCount = 0` 会失败，`undefined > 100` 恒假 ⇒ 无限递归。
- **(B)** 是 `Storages.isExistentStorage` 那个**无兜底**的包装在自递归
  （保存的 `diffOrigIsExistentStorage` 解析回了包装自身）。

**下一步**：
1. 加探针：在栈保护触发点（`tjsInterCodeExec.cpp` 的 `TVPIsTJSStackNearlyExhausted`）
   打印顶层 TJS 帧的 `start_ip`（两个包装入口偏移不同），或加"原生类新增成员是否成功"探针。
2. 背景：本仓库 `tjsInterCodeExec.cpp` 有一段**上游没有**的原生栈保护（256KB 余量），
   把原本的 SIGSEGV 转成了脚本异常；上一个会话就是为这个游戏加的（注释里记着
   "libsigchain 打出 512 帧仍未见底"）。**它只是兜底，不是修复**。

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
3. "无法渲染 CG"：**需要具体场景**（哪个界面 / 哪张 CG / 黑屏还是不出现）。
   本次日志里没有 `evimage*.xp3` 的 CG 被加载，无法定位。

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
