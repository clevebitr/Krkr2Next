# 渲染架构：现状与 R1 目标

> 一句话回答「现在还是手写渲染吗」：
> **混合数学（29 种算子的公式与定点舍入）已经交给 krkrz 权威实现，不再手写；
> 「合成结构」（谁画在谁上面、坐标/clip/opacity 怎么累计）还是自写的过渡实现，计划在 R1-3 退场；
> 「呈现与平台接线」永远是自写的 —— 那是 seam，不是像素语义。**
>
> 权威源与补丁登记见 `vendor/krkrz/VENDOR.md`；分片计划与验收见 `docs/visual-vendor.md`。

---

## 1. 分层与归属

```
脚本（TJS）
  │
  ├─ shim/classes/Tjs{Window,Layer}.*        ← 过渡：脚本 → 自建场景树（R1-3 退场）
  ▼
visual/  Scene → DrawList                    ← 过渡：合成结构（R1-3 换 legacy Layer/Manager）
  │
  ▼
pixel/   BlendPixel(op, opacity)             ← 稳定：操作词汇表 + 上游函数映射
  │
  ▼
vendor/krkrz/visual  tvpgl.c + blend_function.cpp   ← 权威：公式、定点舍入、中性色
  │
  ▼
visual/SoftwareBackend | visual/GLBackend → platform/*  ← 自写：采样 + 呈现（seam）
  │
  ▼
core/contracts/IRenderTarget → platform/android|desktop
```

各层职责与依赖方向（以 `docs/ARCHITECTURE.md` 第 3 节为准）：

| 层 | 能引用 | 说明 |
|---|---|---|
| `pixel/` | `vendor/`、`core/log` | 只做「操作 → 上游函数指针」的映射与类型转换 |
| `vendor/` | 只 `tjs/` | 上游原样代码；平台能力由 `porting/` 注入 |
| `visual/` | `pixel/`、`core/`（**不**直接引用 `vendor/`） | 场景树、DrawList、后端 |
| `legacy_support` | `tjs/`、`core/` | 上游 visual 需要、但必须由我们实现的层（消息/存储/时间/线程/内存/字体） |
| `platform/` | `core/contracts` | 只认像素（`IRenderTarget`）与 GL 入口表 |

## 2. 状态矩阵（每一项只有一个权威）

| 能力 | 现在的实现 | 权威归属 | 何时切换 |
|---|---|---|---|
| 29 种混合算子的公式与舍入 | `vendor/krkrz/visual/tvpgl.c` + `gl/blend_function.cpp`（R1-1 ✅） | 上游 | 已切换 |
| 操作词汇表 / 中文名 / 中性色 | `pixel/LayerOps.*` | 我们（词汇表） | 已落地 |
| 类型 → 上游函数映射 | `pixel/Blend.cpp` | 我们（映射） | 已落地 |
| 场景树、坐标/clip/可见性累计 | `visual/Scene.*` | 过渡 | R1-3 换成 legacy `LayerIntf`/`LayerManager` |
| 位图与位图运算（拉伸/模糊/合成到层） | `visual/Surface.*`（RGBA 直通） | 过渡 | R1-2 换成 `LayerBitmapIntf` + `tTVPBaseBitmap` |
| 图片解码 | `visual/ImageDecoder.*`（stb_image） | 过渡 | R1-2b 换成 legacy `LoadPNG`/`LoadTLG`/`LoadJPEG` |
| 字体与文字 | 无 | 待 vendor | R1-2b（FreeType + `PrerenderedFont`） |
| 转场 / effect / filter 层 | 无（跳过并计数） | 待 vendor | R1-3（`TransIntf`、离屏在 8d） |
| 窗口/图层脚本类 | `shim/classes/Tjs{Window,Layer}` | 过渡 | R1-3 换成 legacy `WindowIntf`/`LayerIntf` |
| 呈现（上传+一 quad+脏矩形） | `visual/GLBackend` + `platform/*` | 我们（seam） | R1-4 改为 `VisualDrawDevice` |
| 调试事件 | `debugger/` + `core/log` | 我们 | 已落地 |

## 3. 数据流（一帧）

```
Window 场景树 ──BuildDrawList──► DrawList{纹理表, DrawItem[]}
                                    │  blend=29 值, opacity, clip, uv, dirty
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
        visual/SoftwareBackend              visual/GLBackend
        （像素权威、CI 用）                 （GLES3，真机）
                    │                               │
                    ▼                               ▼
              Surface → PPM                IRenderTarget::Present
```

- 每条绘制项只描述「哪块纹理、什么坐标、什么混合、多少不透明、裁到哪里」；
  后端不允许读图层树、不允许做变换决策（`docs/render-spec.md` §14 第 1 条）。
- `blend` 的语义完全由 `pixel::BlendPixel` 决定 → 上游函数 → 定点结果。
  因此后端只是「采样 + 混合 + 提交」，两条路径的差异可以被逐像素断言。

## 4. 代码地图

| 文件 | 职责 | 归属 |
|---|---|---|
| `vendor/krkrz/visual/tvpgl.c`（6.6k） | 表、初始化、非混合类算子 | 上游（零修改） |
| `vendor/krkrz/visual/gl/blend_function.cpp` | 29 种混合算子的便携实现 | 上游（补丁 #1） |
| `vendor/krkrz/visual/gl/blend_functor_c.h` | 算子模板 + 表 | 上游（补丁 #2） |
| `vendor/krkrz/msg/MsgIntf.*`、`msg/generated/messages.inc` | 消息文案（344 条，中文优先） | 我们（理由见 VENDOR.md §4） |
| `vendor/krkrz/visual/porting/toolchain_compat.h` | 非 Windows 缺失的宏与 Win32 POD 类型 | 我们 |
| `vendor/krkrz/visual/porting/pixel_routines.h` | 上游像素函数表唯一入口 | 我们 |
| `pixel/LayerOps.*` | 29 值枚举、名字、是否读 alpha、中性色 | 我们 |
| `pixel/Blend.*` | 单像素混合映射（`ltOpaque` 走 `bmCopy` 两支） | 我们 |
| `visual/LayerTypes.h` | 过渡别名（词汇表在 `pixel/`，不复制常量） | 我们 |
| `visual/Surface.*`、`Blend` 已删 | 32bpp 直通 alpha 表面 | 过渡 |
| `visual/Scene.*` | 图层树 → DrawList（含 `SceneBuildStats`） | 过渡 |
| `visual/GLBackend.*`、`platform/*/GLContext` | GLES3 绘制 + 上下文 | 我们（seam） |
| `host/BlendMatrixProbe.cpp` | 29×5 alpha×5 opacity 语义断言（300 项） | 测试 |
| `host/RenderProbe.cpp`、`host/RenderGlProbe.cpp` | 软件/GL 两条通道的像素断言 | 测试 |
| `host/WindowProbe.cpp` | 脚本建场景 → 像素断言 | 测试 |
| `tools/verify_vendor.py` | 上游完整性 + 补丁面校验 | 工具 |

## 4.1 字体是**独立的一类**问题，不在「算子 + 合成」里

老树在字体上的问题不是混合数学的子集，而是另一条代码路径 + 另一个外部依赖。实测证据：

| 事实 | 上游（Windows） | 老树 fork（Android） |
|---|---|---|
| 光栅器默认值 | `visual/LayerBitmapImpl.cpp:66` → `TVPCurrentFontRasterizers = FONT_RASTER_GDI`（FreeType 存在，但旧默认被注释掉） | `cpp/core/visual/impl/LayerBitmapImpl.cpp:63-69` → 把 `FONT_RASTER_GDI` 整个注释掉，默认 `FONT_RASTER_FREE_TYPE` |
| 字体枚举 | `FontSystem.cpp` 97 行，走 Win32 字体枚举 | 重写成 240 行，新增 `TVPGetAllFontList` / `TVPGetDefaultFontName` + `FontImpl.cpp`（`Android_GetExternalStoragePath()`、`/default.ttf`、`/fonts` 目录） |
| 回退排版 | 无 | 新增 `FontBaseline.h`：**逐字回退时把回退字形挪回主字面基线**（否则相邻字符垂直跳动） |
| FreeType 光栅化 | `FreeTypeFontRasterizer.cpp` 184 行 | 561 行（fork 重写） |

也就是说字体问题分四类，各自独立：

1. **字形位图**：GDI 与 FreeType 的 hinting、gamma、抗锯齿覆盖率不同 → 同字号同字体，像素不同。
2. **度量与取整**：advance/line height/baseline、`getTextWidth`/`getTextHeight`、`drawText` 的坐标取整。
3. **文本绘制路径**：走 `LayerBitmapImpl::InternalDrawText` / `DrawGlyph` + `tTVPCharacterData`（逐字形 `OriginX/Y`、`IncX/Y`、`AA`、像素数据）+ `PrerenderedFont` 图集 + 65 级 alpha（`TVPApplyColorMap65`），与 29 种 layer blend 是两套代码。
4. **字体选择与回退**：`Face` 解析、缺字回退链、`mapPrerenderedFont`、Android 上的字体发现路径。

**对 R1 的含义（必须先拍板的事）**：vendor 能带走的是**代码路径**（FreeType 光栅器 + `PrerenderedFont` + `InternalDrawText`），
但**带不走光栅器本身** —— Windows oracle 默认是 GDI，所以文本**不会**因为「同一份代码」就逐像素一致。两个选项：

- **A（真一致）**：Windows oracle 也切 `FONT_RASTER_FREE_TYPE`（上游代码里已有这条路径），并锁定
  FreeType 版本 + hinting/`LCD_FILTER`/gamma 参数（写进 oracle 脚本与文档），再逐像素比。
- **B（度量一致）**：接受字形差异，文本只断言**度量**（advance、行高、baseline、换行位置）与
  “不裁字/不重叠”，像素比对限定在非文本图层（图片、纯色、光效）。

未拍板前，R1-2b 的文本 oracle 不要开工（否则会把 A/B 的选择混进断言里）。

## 4.2 老树的立绘/特殊 CG/Q 版动画问题在哪

不是算子/合成，而是五条渲染路径里**只有①被探针覆盖**。详见
`docs/legacy-render-failures.md`：E-mote 走插件自开的 EGL（`krkrgles.cpp`）、Live2D 的
proprietary SDK 在构建期被静默跳过、`layerEx*` 通过 `GetRenderMethod("...")` 要命名渲染方法、
`LoadPVRv3`+ETC1（无 alpha）用于带透明立绘、以及字体存在两条文本路径（`InternalDrawText`
与 `textrender.cpp`）。

## 5. 精度约定（实测，不是估计）

1. 上游用 `(x*a)>>8` 这类 ÷256 定点；`_o` 变体先把 opacity 折进源 alpha 再 `>>8` 一次。
   因此 `opacity=128` 落在 **126**、白色乘 255 得 **254**，与「公式除 255」差 1～2。
2. 语义断言（`blend_matrix`）与浮点模型比对时容差 **3**；实测最大偏差 **2**。
3. 逐像素 **0 容差**只在同一条代码路径上成立 —— 这要等 R1-2b 的 Windows oracle
   （krkrz 自己 `Layer.saveLayerImage` 落盘）才谈得上。
4. `pixel::BlendPixel` 把结果 alpha 规范成 `0xFF`：合成目标是不透明窗口表面。
   图层间 alpha（`ltAddAlpha` 的累积语义）在 R1-3 交给 legacy 位图。

## 6. 测试与 oracle

| 测试 | 覆盖 | 状态 |
|---|---|---|
| `blend_matrix` | 29 种操作的不变量 + 官方公式（300 项） | ✅ |
| `legacy_message` | 消息表非空、`%1` 替换、异常带文本 | ✅ |
| `render_surface` | 软件后端 + DrawList（手算期望值，已按定点更新） | ✅ |
| `render_gl`（Windows） | GL 与软件逐像素 ±1 | ✅（无 GL 环境 skip） |
| `window_scene` | 脚本建 Window/Layer → 合成 → 像素 | ✅（R1-3 后改为 legacy 落盘） |
| `golden_*`（待建） | Windows krkrz 落盘 vs 本仓库输出 | R1-2b |

## 7. 待做与断点

完整分片见 `docs/visual-vendor.md` §7.1；下次从这里继续：

```
R1-2a  seam 原语（2–3 人日）
  1. porting/thread_impl.h      → 让 LayerBitmapIntf.cpp 从「差 1 个头」到 0 错误
  2. porting/sys_init.*         → SysInitImpl.h（路径、本地名）
  3. porting/detect_cpu.h       → DetectCPU.h（CPU 特性查询，返回通用值）
  4. porting/storage_media.*    → iTVPStorageMedia + tTJSBinaryStream 接 krkr::io::Storage
  5. porting/bitmap_memory.*    → 替代 win32/BitmapBitsAlloc（malloc + 对齐 + 哨兵）
  6. 探针：storage_media_probe（夹具 PNG 经 Storage → tTJSBinaryStream 逐字节一致）
```

估计：R1-2a 2–3｜R1-2b 5–8｜R1-3 8–12｜R1-4 3–5｜R1-5 1–2，合计 **19–30 人日**；
「单帧出图、可与 Windows 逐像素比」= **7–11 人日**。

## 8. 已知偏差与风险

| 项 | 说明 |
|---|---|
| 上游便携路径 vs SSE2 | 已发现 `screen_blend_func` 缺 `~`（Windows 走 SSE2 掩盖）；同类问题靠 oracle 兜底 |
| 定点舍入 | 见 §5，测试容差 3，不要用「手算 ÷255」的值做断言 |
| 自建 `Scene` 与新 `Layer` 并存 | R1-3 必须删掉 `Scene`，不允许两条合成路径（`ARCHITECTURE.md` §3 第 7 条） |
| 双解码 | R1-2b 之后删 `ImageDecoder`（stb），只保留 legacy 的 `Load*` |
| 第三方库 | zlib / libpng / FreeType 以 vendored 引入（`vendor/third_party/`），不链系统库 |
| 真机 GPU | 色彩空间、预乘 alpha、`GL_DITHER`、`highp` 精度（`render-spec.md` §14.2） |
| 字体一致性 | 上游 Windows 默认 GDI、Android 只能 FreeType；文本要逐像素一致必须让 oracle 也切 FreeType 并锁参数，见 §4.1 |
| 字体枚举与回退 | 老树在 Android 上重写了枚举/回退（`FontImpl.cpp`、`FontBaseline.h`）；R1 不带这套，需要重新确认“字体从哪来、缺字怎么退” |
