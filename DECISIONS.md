# DECISIONS

> 决策与踩坑日志，只追加，不参与日常上下文。可用 grep 检索。条目按文件顺序编号（第一个 `##` 是 #1）。

## 2026-11-18 — R1：像素语义交给 krkrz，不再自写混合数学

决定：渲染的像素语义（29 种混合算子的公式、定点舍入、中性色、alpha 语义）全部交给 krkrz 权威实现，本仓库不再自写混合数学。

- 权威源：`krkrz/krkrz` tag `1.4.0r2-4-g48c10558`（本地 `~/AppProjects/krkrz`），与 `cpp_new/tjs` 同 tag（仅 CRLF 差异）。
- 已落地（R1-0/R1-1）：`vendor/krkrz/visual/{tvpgl.c,tvpgl.h,drawable.h,gl/blend_function.cpp,gl/blend_functor_c.h,…}`；新增抽象层 `pixel/`（`LayerOp` 词汇表 + `BlendPixel` 映射）；`visual/Blend.*` 删除。
- 补丁 3 条：`##(`（MSVC 扩展）、`screen_blend_func` 缺 `~`（便携路径 bug，Windows 走 SSE2 掩盖）、`class TJS::…` 前置声明。
- 精度：上游用 `(x*a)>>8`，`_o` 先折 opacity 再 `>>8`，故 opacity=128 落在 126；断言容差 3（实测最大偏差 2）。
- 不走 fork（`cpp/core/visual`）：已漂移（`LayerBitmapIntf.cpp` 与上游 diff 9,025 行，另有 5,278 行自研 GL）。

## 2026-11-18 — 老树 cpp/** 零复用，缺的功能按当前架构新写

老树 `cpp/core/**` 决定废弃：从它复用 **0 行代码**，只作行为参考、兼容补丁需求清单、ABI 需求书（`bridge/engine_api/include/engine_api.h`）。

- 实测 seam 预算（19 个 vendor 候选文件扫描）：引用 634 个 `TVP*` 符号 → 108 个来自 `base/utils/msg` → 20 个函数 + 83 条消息常量；消息表由 `msg/MsgIntfInc.h` 的 X-macro 生成。
- 已解决：上游 `MsgIntfInc.h` 全是 NULL，文案在 `msg/text/Messages.xlsx`（构建期 Perl/Excel → Win32 资源 → `LoadString`）。Android 无此路径 → `tools/gen_legacy_messages.py` 直接生成 344 条宽字符串（默认中文，回退 en→ja）。
- 缺的功能按 `ARCHITECTURE` §6 新写：先 `core/contracts` 契约 → `platform` 实现 → `shim` 桥接 → `host` 断言。
- 明确不做：视频（`cpp/core/movie` 22k 行）、动态插件/Susie/ncbind、PhaseVocoder 循环平滑。
- 删除节奏：冻结 `cpp/**`；R1-5 删整树（含草稿 `krkr_ext/krkrMsgImpl.cpp`，技术债 #6），保留 `bridge/engine_api`、`app/`。

## 2026-11-18 — 渲染剩余工作估计：19–30 人日，L1 出图 7–11 人日

渲染剩余工作量（1 人、熟悉本仓库，2025-11 实测盘点）：

| 片 | 内容 | 人日 |
|---|---|---|
| R1-2a | seam 原语：thread_impl/sys_init/detect_cpu/storage_media/bitmap_memory + 事件泵骨架 | 2–3 |
| R1-2b | vendored zlib+libpng+FreeType、LoadPNG/TLG/JPEG、SaveTLG、Windows oracle（saveLayerImage 落盘 + diff_images.py） | 5–8 |
| R1-3 | LayerIntf(10.3k)/LayerManager/TransIntf/ImageFunction/LayerTreeOwner + WindowIntf + iTVP* seam；删自建 Scene | 8–12 |
| R1-4 | Android VisualDrawDevice：BGRA 上传、脏矩形、一 quad、Y 翻转、真机一帧 | 3–5（真机可能 ×2） |
| R1-5 | 收口：删 Scene/stb_image/GLBackend、文档、CI | 1–2 |
| 合计 | | **19–30** |

可用性分级：L1 单帧出图、可与 Windows 逐像素比 = 7–11 人日；L2 脚本能建 Layer/Window（KAG 静态画面）= 15–23；L3 能跑对话（含输入 3–5）= 18–28；L4 日常驱动（含音频 8–12）= 30–45。

编译驱动盘点（上游源码直接 -fsyntax-only）：`ComplexRect`、`FontSystem`、`PrerenderedFont` 已 0 错误；`LayerBitmapIntf`/`LayerBitmapImpl`/`GraphicsLoaderIntf`/`LoadTLG`/`SaveTLG5` 各差 1 个 porting 头；`LoadPNG`/`FreeType` 差第三方库。

## 2026-11-18 — 字体是独立一类问题：GDI vs FreeType，文本一致性需先选 A/B

老树的「字体问题」是独立一类，不在算子和合成里。实测证据：

- 上游 Windows 默认 **GDI** 光栅器（`visual/LayerBitmapImpl.cpp:66` `TVPCurrentFontRasterizers = FONT_RASTER_GDI`），FreeType 是备选。
- 老树 fork 把 GDI 整个注释掉、默认 `FONT_RASTER_FREE_TYPE`（`cpp/core/visual/impl/LayerBitmapImpl.cpp:63-69`），并把 `FontSystem.cpp` 从 97 行重写到 240 行，新增 `FontImpl.cpp`（Android 从 external storage / `default.ttf` / `fonts` 找字体）与 `FontBaseline.h`（逐字回退时对齐基线，否则相邻字符垂直跳动）。
- 文本绘制走 `InternalDrawText`/`DrawGlyph` + `tTVPCharacterData` + `PrerenderedFont` 图集 + 65 级 alpha，和 29 种 layer blend 是两套代码。

结论与待拍板项：R1 能 vendor **代码路径**（FreeType 光栅器、PrerenderedFont、InternalDrawText），但带不走光栅器 → 文本不会自动与 Windows 逐像素一致。二选一：
A) Windows oracle 也切 `FONT_RASTER_FREE_TYPE` 并锁定 FreeType 版本 + hinting/LCD_FILTER/gamma，做逐像素；
B) 接受字形差异，文本只断言度量（advance、行高、baseline、换行），像素比对限定在非文本图层。
未拍板前不要写文本 oracle（否则把 A/B 的选择混进断言）。

## 2026-11-18 — 立绘/特殊CG/Q版动画：问题在插件旁路，探针结构性盲区

老树「立绘 / 特殊 CG / Q 版动画」问题不在算子与合成，而在五条渲染路径里只有核心①被探针覆盖。证据（`cpp/` 与上游克隆逐条核对，详见 `cpp_new/docs/legacy-render-failures.md`）：

- ② **E-mote**：`cpp/plugins/krkrgles.cpp` 直接 `#include <EGL/egl.h> <GLES2/gl2.h>` 并引 `motionplayer/Player.h`，自开 EGL 自管纹理；`motionplayer`(7,615)+`psbfile`(6,351) 不经过 `RenderManager_ogl`。
- ③ **Live2D**：`cubism/{Framework,Core/lib}` 被 gitignore，CMake 找不到 `libLive2DCubismCore.a` 时 **message(STATUS …跳过 Live2D)** → 子系统根本没编进来，真机只看到 `krkrlive2d.dll Failed`。
- ④ **layerEx\***：`layerExPerspective.cpp` 用 `TVPGetRenderManager()->GetRenderMethod("PerspectiveAlphaBlend_a")` + `OperatePerspective`；GL 实现缺失/近似只在 GL 路径错，CI 跑软件路径看不到。
- ⑤ **纹理格式**：`LoadPVRv3`（fork 独有）+ `glCompressedTexImage2D`，`GL_ETC1_RGB8_OES` 无 alpha → 带透明立绘走 ETC1 必然黑边/不透明。
- 部分更新：立绘只改小矩形，探针全帧重绘 → 纹理缓存/脏矩形 bug 永远照不到。
- 字体：老树有两条文本路径（`InternalDrawText` + `PrerenderedFont` vs 第三方 `textrender.cpp` + `FontBaseline.h` + 自走 EGL），度量/基线不同 → 「有的文本正常、有的错位」。

结论：加探针无效是**结构性的**（探针与失败点不在同一条路径）。修法顺序：路径地图 → 每条路径一个可观测点（走了哪条 seam、什么纹理格式、上传几次）→ 再上像素断言。

R1 覆盖：`dfMask`/`holdAlpha`/`ltAddAlpha` 语义、TLG/PNG 解码、`tTVPComplexRect` 更新骨架；不覆盖：ETC1/PVR、`GetRenderMethod` 命名契约、E-mote/Live2D、两条字体路径（均需单独立项，且插件不得再自开 GL）。

## 2026-11-18 — 字体：接口用上游、策略注入、度量先行、单一路径、可回退

字体策略建议（回应「老树部分重写方向对但兼容性不如原版」）：

老树重写的问题不是方向，而是**侵入式**：改在 `LayerBitmapImpl`/`FontSystem` 里，导致与上游行为分叉，并多出第二条文本路径。建议改成「接口用上游、策略注入、默认=上游」：

1. `iTVPSysFont`（枚举/选择）、`iTVPFontRasterizer` 保持上游签名；Android 字体发现放 `platform/`；回退链与基线对齐做成显式 `FontFallbackPolicy`，**默认关闭**（=上游行为），由配置/命令行开启。
2. 只留一条文本绘制路径：把 `textrender` 的能力并入 legacy `InternalDrawText`，或明确它仅服务 Canvas；否则永远两套度量。
3. 度量先于字形：`font_metrics` oracle —— Windows 上游打印 `getTextWidth/getTextHeight/getEscWidthX/getEscWidthY/getEscHeightX/getEscHeightY/getGlyphDrawRect`，逐项断言（与光栅器无关，KAG 排版依赖它）。
4. 锁字体来源与光栅参数：`default.ttf` + 显式 fallback 列表（不扫 external storage）；FreeType 版本/hinting/LCD_FILTER/gamma 记录并与 oracle 一致。
5. 改写必须可回退：编译开关 `KRKR_LEGACY_FONT_RASTERIZER=upstream|fork`，两边跑同一套度量断言，差异登记 VENDOR.md。
6. 承认像素差异：GDI vs FreeType 不可能逐像素相同；0 容差文本 oracle 仅在「Windows 也切 FreeType + 同参数」时成立。

## 2026-11-18 — 仓库清理：冻结 cpp/，删除过期文档与未引用脚本

仓库清理与老树冻结（2025-11）：

- `cpp/` **冻结并标记**（`cpp/FROZEN.md`）：根 `CMakeLists.txt` 仍从 `cpp/{external,core,plugins}` 构建 `libengine_api.so`，`app/` 与 `bridge/engine_api` 都挂在它上面；删除会让 Android APK 在第 11 步之前完全不可构建。删除作为第 11 步收尾。
- 删除过期文档：根 `TODO.md`（老树问题分析）、`docs/{HANDOVER,PROJECT_REPORT,STRUCTURE_DESIGN,NAMING}.md`（`docs/` 目录随之移除）。
- 删除未引用脚本：`scripts/{build_shell_local.sh,install_to_emulator.ps1,check_cmake_graph.sh,check_tu.sh}`；老树命名机器（`check_naming.py`、`rename_symbols.py`、`scripts/{naming,rename}/`）一并移除，`check_static.sh` 去掉对应步骤。
- 命名规则保留：并入 `cpp_new/docs/ARCHITECTURE.md` §7 第 5 条（抽象层用抽象层命名、新东西不用 `TVP`/`tTVP`/`iTVP` 前缀、文件名与职责一致）。
- `check_port_drift.py`：清单 `compat/upstream/aetherkiri_ports.json` 已在提交 `8578e07` 删除，改为「缺失即跳过（rc=2）」，`check_static.sh` 显式报告跳过项。
- CI 仍在用的脚本一律保留（check_static/check_jni_symbols/check_port_drift/check_syntax/check_qualification/check_gl_symbols/verify_engine_so/restore_cubism_sdk/build_engine_android/patch_cubism_sdk/gen_embedded_shaders）。
- 未跟踪的 `cpp/core/extension/krkrMsgImpl.cpp`（老树消息系统草稿，技术债 #6）**未纳入提交**，留待确认后删除。

## 2026-11-18 — bridge/app 拆为 submodule，删除 krkrMsgImpl 草稿

bridge/ 与 app/ 拆为独立仓库（submodule），并删除废弃草稿。

- 删除 `cpp/core/extension/krkrMsgImpl.cpp`（老树消息系统搬运草稿，技术债 #6；已被 `vendor/krkrz/msg` 的生成消息表取代，从未接入构建）。
- 拆分方式：`git subtree split -P bridge` / `-P app` 保留各自路径历史（87 / 71 个提交），各加仓库级 `.gitignore` 后推到新仓库：
  - `https://github.com/clevebitr/Krkr2Next-bridge`（`engine_api`：C ABI + JNI 薄壳）
  - `https://github.com/clevebitr/Krkr2Next-app`（Kotlin/Compose 宿主、Gradle 工程）
- 父仓库改 gitlink（`.gitmodules`），并要求：子仓库改动先在子仓库提交，再回父仓库提交指针。
- CI：两处 `actions/checkout@v4` 加 `submodules: recursive`（两个子仓库均为 public，无需 token）。
- `build.sh`：启动前检查 `bridge/.git`、`app/.git`，缺失即提示 `git submodule update --init --recursive` 并退出 2。
- 已知耦合（暂不解决）：`bridge/engine_api` 仍 `#include` 老树头（`../../cpp/...`），因此子仓库不能脱离父仓库布局单独构建；第 11 步切到 `cpp_new` 后这条耦合自然消失。
- 验证：`git clone --recurse-submodules` 落地正常（`app/gradlew`、`bridge/engine_api/include/engine_api.h` 存在）；父仓库已推送到 `origin/main`（`00b44dd`）。

## 2026-11-18 — CI：暂停自动构建，仅保留手动触发

- 原因：引擎正在换实现（cpp_new 的 R1 路线），自动构建没有可验收产物。最近 6 次 `push` 构建全部
  failure，属于纯噪声；且 `bridge/engine_api` 仍指向老树（冻结中），构建结果不代表新引擎状态。
- 做法：`.github/workflows/android_build.yml` 删除 `push`/`pull_request` 触发（注释保留在原位），
  只保留 `workflow_dispatch`（全部 inputs 不变，可在 Actions 页面手动 Run workflow）。
- 已取消当时在跑的 push 构建（run 44301886702），推送本次 YAML 改动后没有再产生新运行。
- 恢复自动构建：把 `push: branches: [main]` 与 `pull_request:` 两行取消注释即可。
- 静态检查不受影响：`scripts/check_static.sh` 仍可本地跑（它不依赖 CI 触发）。
