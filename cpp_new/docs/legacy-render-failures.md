# 老树渲染问题复盘：立绘 / 特殊 CG / Q 版动画 / 字体

> 结论：这三类问题**不是算子或合成算错**，而是各自走了不同的渲染路径，而这些路径
> 都不在「核心探针」的覆盖范围内。探针加得再多也不会变绿 —— 因为探针测的是
> 「`tTVPBaseBitmap` + 29 种 blend」，失败点却在「插件 → `RenderManager_ogl` → GL」这条旁路。
>
> 证据全部来自本仓库的 `cpp/`（老树，已冻结见 `cpp/FROZEN.md`）与 `~/AppProjects/krkrz`（上游），行号可查。
> 老树里已有的探针清单与未解决条目在 `docs/compat/recon/render-issues.md`（本次分析与之互补：
> 那份记录"哪些探针已埋"，本文说明"为什么它们照不到这些路径"）。

---

## 1. 渲染路径地图（老树真实存在的五条）

```
                       ┌─ ① 核心：Layer/Bitmap + 29 blend（探针覆盖这里）
脚本 ── Layer/Window ──┼─ ② 插件：motionplayer/psbfile（E-mote）→ krkrgles.cpp 自开 EGL+GLES2
                       ├─ ③ 插件：cubism（Live2D）→ krkrlive2d.cpp
                       ├─ ④ 插件：layerEx*（透视/长曝光/光栅/拖影）→ RenderManager "命名渲染方法"
                       └─ ⑤ 文本：InternalDrawText 与 textrender.cpp（第三方 TextRender，自走 EGL）
```

核心探针（`blend_matrix`、`render_surface`、`window_scene`）**只走①**。②③④⑤ 各自
直接碰 GL、纹理格式、字体光栅器，任一条有偏差都会表现为「立绘/特殊 CG 不对」，
而①全绿。

## 2. 逐条证据

### 2.1 Q 版动画（E-mote）：插件自开 EGL，绕过渲染管理器

`cpp/plugins/krkrgles.cpp` 顶部直接：

```cpp
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
...
#include "motionplayer/Player.h"   // E-mote
```

注释原文：「Motion.Player 的自动渲染要判断原生实例……直接包含 motionplayer 的 Player 头」。
`motionplayer`（7,615 行）+ `psbfile`（6,351 行）是 E-mote 的播放器与 PSB 解析，**自己管
纹理、自己管 alpha**。它既不走 `RenderManager_ogl`，也不受核心探针约束。

### 2.2 Live2D：构建期被静默跳过

`cpp/plugins/CMakeLists.txt`：`cubism/{Framework,Core/lib}` 被 gitignore（proprietary SDK），
找不到 `libLive2DCubismCore.a` 时输出

```
message(STATUS "Live2D: Android ${ANDROID_ABI} 下没找到 libLive2DCubismCore.a，跳过 Live2D（krkrlive2d.cpp 不参与编译）")
```

也就是说「Q 版动画图层不显示」有时**不是渲染错，而是子系统根本没编进来**；真机上只看到
`krkrlive2d.dll Failed` 一行日志。

### 2.3 特殊 CG（透视/长曝光）：要「命名渲染方法」，GL 与软件是两份数学

`cpp/plugins/layerExPerspective.cpp`：

```cpp
TVPGetRenderManager()->GetRenderMethod("PerspectiveAlphaBlend_a");   // 按名字取
TVPGetRenderManager()->OperatePerspective(...);
```

`layerExLongExposure`（长曝光）、`layerExRaster`、`layerex_draw` 同类。这些方法由
`ogl/RenderManager_ogl.cpp`（5,278 行，fork 自研）实现，软件侧 `RenderManager.cpp` 是**另一份**。
一旦某个方法名缺失或近似实现，只在 GL 路径上出错；而 CI 跑的是软件路径 → 探针看不到。

### 2.4 立绘：纹理格式与 alpha

`LoadPVRv3.cpp`（fork 独有解码器）+ `RenderManager_ogl.cpp:1873` 的 `glCompressedTexImage2D`，
并且 `GL_ETC1_RGB8_OES` 出现在代码里（`:3246` 的注释与 `:4659` 的格式映射）。
**ETC1 没有 alpha 通道**：带透明的立绘一旦走 ETC1，边缘要么黑边要么不透明 —— 这是典型的
「立绘边缘不对、只在高通设备明显」。上游没有这条路径。

### 2.5 立绘：部分更新与纹理缓存

立绘/动画每帧只改一小块区域。老树的更新链是
`Layer.update → LayerManager::UpdateToDrawDevice → RenderManager_ogl` 的纹理缓存 + 脏矩形。
探针做的是**全帧重绘**，缓存不失效、只更新矩形大小算错这类 bug **永远照不到**；
表现就是「拖影、残帧、切场景后闪一下旧图」。

### 2.6 字体：老树有两条文本路径，而不是一条

| 路径 | 代码 | 特点 |
|---|---|---|
| 核心 | `LayerBitmapImpl::InternalDrawText` / `DrawGlyph` + `tTVPCharacterData` + `PrerenderedFont` + 65 级 alpha | 上游同款，Windows 默认 **GDI** 光栅器 |
| 插件 | `cpp/plugins/textrender.cpp`（1,259 行，源自 3c1u/TextRender） + `FontBaseline.h` + `krkr_egl_context.h` | 自己光栅化（FreeType）、自己上传 GL、自己算基线 |

两条路径的度量与基线算法不同 → 「同一个游戏，有的文本正常、有的文本错位/跳动」；
`FontBaseline.h`（fork 新增）就是为这条路径打的补丁。这也是「不如原版」的来源：
**原版只有一条文本路径。**

## 3. 为什么「反复加探针」修不好

| 探针位置 | 能覆盖 | 覆盖不到 |
|---|---|---|
| 核心（`tTVPBaseBitmap` + 29 blend） | ① 的算子与舍入 | ②③④⑤ 全部 |
| 全帧合成 | 静态画面 | 部分更新、纹理缓存、脏矩形 |
| 软件路径（CI） | 与 GL 的对照 | GL 独有：ETC1/PVRTC、`GetRenderMethod`、EGL 上下文、`onDraw` 时序 |

结构性原因：**失败点和探针不在同一条路径上**。要修，顺序应该是
「先画路径地图 → 每条路径一个可观测点（哪条方法被调用、用了什么纹理格式、上传了几次）
→ 再对具体路径加断言」，而不是继续在①上加用例。

## 4. R1 能带走什么、带不走什么

| 问题 | R1 是否解决 |
|---|---|
| 立绘的 `dfMask`/`holdAlpha`/`ltAddAlpha` 语义 | ✅ R1-3（legacy `LayerBitmapIntf`） |
| 立绘解码（TLG/PNG/JPEG、独立 `_m` 遮罩） | ✅ R1-2b |
| 立绘部分更新与缓存 | ⚠️ R1 带来 legacy 的 `tTVPComplexRect` + 上游 draw device 契约，但**缓存/脏矩形实现仍是我们的 seam** |
| ETC1/PVRTC 有损压缩 | ❌ 不 vendor（`LoadPVRv3` 是 fork 独有）。方针：**带 alpha 的立绘禁止走 ETC1**；要压缩就用 ETC2/PVRTC RGBA 并做像素断言 |
| `layerEx*` 的命名渲染方法 | ❌ 属插件（第 8d 之后单独立项）。它们必须改成走我们的 `iTVPDrawDevice`/离屏接口，不能再自开 GL |
| E-mote（`motionplayer`+`psbfile`+`krkrgles`） | ❌ 插件。同样的要求：只保留解码/动画逻辑，绘制走统一 seam |
| Live2D | ❌ 插件 + proprietary SDK；要么把 SDK 纳入私有构建并做 par条对照，要么明确不支持并**显式报错**（不要静默跳过） |
| 字体两条路径 | ❌ 属插件与本仓库都要处理；建议见 §5 |

## 5. 字体：方向对，但要做成「可回退的策略层」

老树的重写（FreeType 优先、Android 字体发现、回退基线对齐）方向是对的，问题出在**侵入式**：
它改在 `LayerBitmapImpl`/`FontSystem` 里，于是和上游行为分叉，且多出第二条文本路径
（`textrender`）。建议：

1. **接口用上游，策略注入**：`iTVPSysFont`（枚举/选择）、`iTVPFontRasterizer` 保持上游签名；
   Android 的字体发现放 `platform/`，回退链与基线对齐做成显式策略 `FontFallbackPolicy`，
   **默认关闭 = 上游行为**，需要时由配置/命令行打开（不写死在代码里）。
2. **只保留一条文本绘制路径**：把 `textrender` 要的能力（自定义字形位图/描边）并进 legacy 的
   `InternalDrawText`，或明确它只服务于 Canvas 绘制；否则永远是「两套度量」。
3. **度量先于字形**：先建 `font_metrics` oracle —— Windows 上游打印每个
   `(face, size, text)` 的 `getTextWidth/getTextHeight/getEscWidthX/getEscWidthY/getEscHeightX/
   getEscHeightY/getGlyphDrawRect`，逐项断言。这是**与光栅器无关**的硬指标，KAG 排版全靠它。
4. **锁定字体来源与光栅参数**：`default.ttf` + 显式 fallback 列表（不要扫 external storage，
   不可复现）；FreeType 版本、hinting、`LCD_FILTER`、gamma 全部记录，oracle 侧也用同一份。
5. **改写可回退**：光栅器若必须改，用编译开关
   `KRKR_LEGACY_FONT_RASTERIZER=upstream|fork`，两边都跑同一套度量断言；差异进 `VENDOR.md`。
6. **像素层面承认差异**：GDI 与 FreeType 不可能逐像素相同（见
   `docs/rendering-architecture.md` §4.1），逐像素 0 容差的文本 oracle 只有在
   「Windows 也切 FreeType + 同参数」时才成立。

## 6. 建议的下一步顺序

1. 补一张**路径地图**进 `docs/rendering-architecture.md`（本文 §1），此后每个渲染 bug 先归路径。
2. `layerEx*` / E-mote / Live2D 的验收方式改成「调用次数 + 像素」两段：先断言走了哪条 seam，
   再断言像素；只断言像素会重演「加的探针看不见」。
3. R1-2b/R1-3 落地时，把 `GetRenderMethod` 这类**字符串契约**列成清单（谁提供、缺失时如何报错）。
4. 纹理格式白名单：带 alpha 的资源禁止 ETC1；压缩路径必须有像素断言。
5. 字体按 §5 拆成 `font_metrics`（先做）+ `FontFallbackPolicy`（后置、默认关）+ 单一路径（收口）。
