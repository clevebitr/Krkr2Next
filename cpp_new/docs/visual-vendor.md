# 渲染层交接计划（R1：像素与图层语义交给 krkrz 权威实现）

> 结论：**不再自己实现合成。** 把 krkrz 的「像素函数 + 位图 + 图层模型 + 字体」原样 vendor 进来
> （与 `cpp_new/tjs/` 同等待遇），`cpp_new` 只保留「seam 实现 + 平台 + 生命周期 + ABI」。
>
> 依据：`cpp_new/docs/render-spec.md` §3/§14 已列出 29 种混合算子、中性色、`dfMask`/`holdAlpha`、
> 三条采样路径；`cpp_new/visual/Blend.cpp` 当前的语义与官方公式不一致（见 §2）。
> 自写实现要长期追平这 29 个算子的定点舍入，成本高于 vendor。

---

## 1. 权威源与版本事实（已核对）

| 项 | 事实 | 影响 |
|---|---|---|
| 权威源 | `~/AppProjects/krkrz` = **krkrz/krkrz** `1.4.0r2-4-g48c10558` | 只出 Windows 构建（`vcproj/`，无 CMake） |
| TJS 侧对齐 | `cpp_new/tjs/` 与该克隆的 `tjs2/` **仅差 CRLF**（`tjsCommHead.h` 逐行内容相同） | 版本已对齐，无需再 vendor tjs2 |
| 像素函数 | `visual/tvpgl.c`（6,658 行，纯 C，只 include `<memory.h> <math.h> tjsTypes.h tvpgl.h`），自带 `TVPInitTVPGL()` / `TVPUninitTVPGL()` 与 `_c` 函数指针表 | **零修改可编**，一个 init 调用即可用 |
| 图层模型 | `LayerIntf.cpp` 10,291 / `LayerBitmapIntf.cpp` 4,103 / `LayerManager.cpp` 1,100 / `TransIntf.cpp` 1,215 / `ImageFunction.cpp` 997 | 语义权威，全部 vendor |
| 平台切片 | `visual/win32/`：`WindowImpl` 2,383、`VideoOvlImpl` 1,240、`LayerImpl` 1,674、`LayerBitmapImpl`、`BitmapBitsAlloc`（42 行头，`DWORD` + `Alloc/Free` 两个方法）、`GDIFontRasterizer`、`TVPSysFont`、`TVPScreen`、`VSyncTimingThread`、`DInputMgn` | **全部不取**，由我们重写 |
| SIMD | `visual/IA32/`（x86 汇编）、`visual/gl/` + `visual/glgen/`（SSE2/AVX2 生成代码，`tvpps.c`、`ResampleImageSSE2/AVX2`、`gengl.pl`） | ARM 不取，走 `tvpgl.c` 的 `_c` 参考实现 |
| seam | `iTVPDrawDevice` 位于 `visual/win32/DrawDevice.h`（536 行，带 `HWND` 参数） | 我们定义无窗口句柄的子集 |

**不得从本仓库的 `cpp/core/visual` 取文件。** 它是 Kirikiroid2 后裔，已经漂移：

| 文件 | 本仓库 fork | krkrz 权威 |
|---|---:|---:|
| 像素函数 | `tvpgl.cpp` 14,139 行（含 SIMD/生成代码） | `tvpgl.c` 6,658 行（C 参考 + 指针表） |
| `LayerBitmapIntf.cpp` | 4,919 行（与上游 diff 9,025 行） | 4,103 行 |
| `LayerManager.cpp` | 1,133 行（diff 2,236 行） | 1,100 行 |
| GL 渲染 | `ogl/RenderManager_ogl.cpp` 5,278 行 + `krkr_gl3_shim.cpp` | 不存在（Windows 走 D3D） |

## 2. 为什么必须换（自写实现的具体偏差）

`cpp_new/visual/Blend.cpp` 对照官方公式（<https://wamsoft.github.io/krkrz_dev/en/guide/GraphicSystem/>）：

| 算子 | 官方 | 现状 |
|---|---|---|
| `ltOpaque` | `src`，opacity 仍生效 | 忽略 opacity |
| `ltAdditive` | `min(1, dst+src)`，**α 忽略** | `dst + src*α` |
| `ltSubtractive` | `max(0, dst+src-1)`，**α 忽略** | `dst - src*α` |
| `ltMultiplicative` | `dst*src`，**α 忽略** | 按 α 混合 |
| `ltAddAlpha` / `ltDodge` / `ltScreen` / `ltDarken` / `ltLighten` | 各有公式 | 缺失 |
| `ltPsNormal…ltPsExclusion`（16 种） | Photoshop 定义 | 缺失 |
| 中性色（黑/白/50% 灰） | 参与边缘运算 | 没有概念 |
| 舍入 | tvpgl 定点 `(x + (x>>8) + 128) >> 8` | `(v*s+127)/255` |

## 3. 归属与目录

```
cpp_new/
├── vendor/krkrz/                  ← 新，原样拷入（只许 shim/porting 包含）
│   ├── VENDOR.md                 来源 tag、逐文件 sha256、补丁登记
│   ├── msg/                      消息文本与异常（MsgIntf.h + 实现我们将补的少数符号）
│   ├── base/                     StorageIntf / SysInitIntf / EventIntf 接口
│   ├── utils/                    DebugIntf / TickCount 接口
│   ├── visual/                   tvpgl.c/h、drawable.h、LayerBitmapIntf、LayerIntf、
│   │                             LayerManager、TransIntf、ImageFunction、ComplexRect、
│   │                             BitmapIntf、FontSystem/FreeType/PrerenderedFont、
│   │                             GraphicsLoaderIntf、LoadPNG/LoadJPEG/LoadTLG、SaveTLG*
│   └── visual/porting/           ★我们写的实现：BitmapBitsAlloc、SysFont、StorageMedia、
│                                 DrawDevice、TickCount、Event pump、Msg 兜底
├── core/contracts/VisualDrawDevice.h  ★seam：iTVPDrawDevice 的无 HWND 子集
├── shim/classes/                 保留 TJS 侧薄装配（不再建自己的场景树）
├── visual/                       降级为 host 工具的支撑（Surface/PPM/图像断言）
└── host/                         BlendMatrixProbe、SaveLayerImageProbe、diff 工具
```

铁律的调整（写进 `docs/ARCHITECTURE.md`）：

- **「不 include 老树头」的适用范围收窄为「`vendor/` 之外」**。`vendor/krkrz/**` 与
  `visual/porting/**` 可以包含老树头，其余层仍然禁止。
- `check_layer_deps.py` 增加：`vendor/` 只能被 `shim/`（含 `visual/porting`）与 `host/` 包含；
  `core/`、`engine/` 不得出现 `vendor/` 头。
- **同一时刻只有一个组件回答「这个像素应该是什么颜色」**：R1 完成后删除
  `visual/Scene.*`、`visual/Blend.cpp` 的自写数学，以及 `shim/classes/Tjs{Window,Layer}` 的
  自建图层树；不允许保留第二条合成路径。

## 4. 分片计划（每片都能编译、能贴输出、可回滚）

### 阶段 R1-0 — 清单与校验

- 产出 `vendor/krkrz/VENDOR.md`：来源 tag、每个文件的三态（**取 / 不取 / 我们重写**）、sha256。
- 用 `#include` 实测（不是猜）确认 `LayerIntf.cpp` 的编译依赖闭包，登记到本文件。
- 验收：清单里没有「状态未知」的行；`doctor` 脚本能校验 sha256。

### 阶段 R1-1 — 像素函数

- 拷入 `visual/tvpgl.c`、`visual/tvpgl.h`、`visual/drawable.h`（29 值枚举 + `TVPIsTypeUsingAlpha`）。
- `visual/Blend.cpp` 改成薄适配：`BlendPixel()` 用 1 像素 span 调 `TVP*Blend_o(dest, src, 1, opa)`
  与 `_HDA`（hold alpha）变体；`TVPInitTVPGL()` 在首次使用时初始化（无窗口依赖）。
- `LayerType` 扩到 29 值；`software backend` 增补 `ltAddAlpha/ltDodge/ltDarken/ltLighten/ltScreen`
  与 16 种 `ltPs*`（它们全部由 tvpgl 提供，不再自己写公式）。
- 新增 `host/BlendMatrixProbe.cpp`：**表格驱动** 29 type × `opacity ∈ {0,64,128,192,255}` ×
  `srcAlpha ∈ {0,64,128,192,255}`，golden 表由 tvpgl 生成的 TSV 入库，断言逐项相等。
- 验收：`ctest -R blend_matrix` 通过；`render_spec` 的 §3 表格从「待实现」变成「已对齐」。
- 风险：`_(o)` 变体只接受 `tjs_int opa`，`opacity × 像素α` 的合成要用 `.*` 变体组合；第一天
  用 `tvpgl.h` 的实际签名验证，不要凭文档。

### 阶段 R1-2 — 位图、字体与解码

- vendor `LayerBitmapIntf.*`、`BitmapIntf.*`、`PrerenderedFont`、`FontSystem`、`FreeType*`、
  `GraphicsLoadThread`、`GraphicsLoaderIntf`、`LoadPNG/LoadJPEG/LoadTLG`、`SaveTLG5/6`。
- 重写 `visual/porting/BitmapBitsAlloc.cpp`（malloc + 16B 对齐 + 前后哨兵 + 尺寸记录，~80 行）、
  `visual/porting/SysFont`（FreeType）、`visual/porting/StorageMedia`（接 `krkr::io::Storage`）。
- 新增 zlib（PNG/TLG 需要；termux `pkg install zlib`、NDK 自带；保持"一条命令可编"）
  注意：`puff` 不能解 PNG 容器，只用于 XP3 段，两者不混用。
- 验收：`Layer.loadImages("x.png"/"x.tlg")` → `Layer.saveLayerImage` 与 Windows krkrz 输出
  **逐字节相同**（第一批 oracle）。

### 阶段 R1-3 — 图层模型

- vendor `LayerIntf.*`、`LayerManager.*`、`TransIntf.*`、`ImageFunction.*`、`ComplexRect.*`、
  `LayerTreeOwner*`、`RectItf`、`BitmapLayerTreeOwner`。
- seam 实现（我们写）：
  - `core/contracts/VisualDrawDevice.h` + `platform/*/VisualDrawDevice`：`StartBitmapCompletion /
    NotifyBitmapCompleted / EndBitmapCompletion / Update / Show`；
  - `iTVPWindow` / `iTVPLayerTreeOwner`：由 `core/api/EngineRuntime` 的窗口实现；
  - `iTVPStorageMedia`：接 `krkr::io::Storage`（脚本 `Storages` 与 legacy 共用同一挂载表）；
  - `iTVPSysFont`：接 FreeType；`TickCount`：接 `steady_clock`；`Msg`：接 `core/log`。
- 删除：`visual/Scene.*`、`shim/classes/Tjs{Window,Layer}.*` 的合成/场景职责（改为由 legacy 提供
  `Window`/`Layer`/`Bitmap` 类）。
- 验收：`window_scene` 改为「legacy 合成 → `saveLayerImage` → 与 Windows 逐像素一致」；
  29 type 全矩阵 + `beginTransition` 淡入淡出一致。

### 阶段 R1-4 — Android 呈现

- `VisualDrawDevice` 的 Android 实现：持久纹理 + `glTexSubImage2D`（**`GL_BGRA`**、脏矩形）+
  一个四边形；Y 轴翻转在 present 完成；复用 `AndroidGLContext` / `AndroidSurface`。
- 验收：真机一帧；与软件落盘图 ±1；`render-spec.md` §14 更新。

### 阶段 R1-5 — 收口

- 删 `visual/ImageDecoder.cpp` + `third_party/stb_image`（与 `LoadPNG` 双解码 = 双 alpha 约定）、
  `visual/GLBackend.*`（或移入 `host/` 作对照工具）、`visual/DrawList.h`/`SoftwareBackend` 移入
  `host/`（仅作 oracle 落盘）。
- `check_layer_deps.py` 新规则，以及 `tech-debt.md`、`ARCHITECTURE.md`、`render-spec.md` 同步更新，
  `tools/run_probes.sh` 覆盖新探针。

## 5. 验收矩阵（oracle）

**oracle 由 krkrz 自己生成，不用截图比截图。** Windows 侧脚本：

```tjs
// oracle/scene_matrix.tjs —— 在 Windows krkrz 上跑
var w = new Window(); w.width = 256; w.height = 256;
for(var t = 0; t <= 28; t++) {
  var base = new Layer(w, null); base.type = ltOpaque; base.fillRect(0,0,256,256,0x4080C0);
  var src  = new Layer(w, base);  src.type  = t;
  src.loadImages("oracle/gradient.png"); src.opacity = 128; src.left = 32; src.top = 32;
  base.saveLayerImage("out/type_" + t + ".png");
}
```

`cpp_new` 侧跑同一份脚本（`Layer.saveLayerImage` 走同一个 legacy 实现），`tools/diff_images.py`
逐像素比较，CTest 挂 `golden_type_*`。容差：**同一像素路径 0，跨架构（x86 SSE2 vs ARM `_c`）允许 ±1**，
阈值写进探针参数，不用"看起来差不多"。

## 6. 不做的事

- 不从 `cpp/core/visual` 取任何文件（漂移 + 自研 GL）。
- 不 vendor `visual/win32/`、`visual/IA32/`、`visual/gl/`、`visual/glgen/`、`tjs2/`（已对齐）。
- 不在 `glBlendFunc` 里表达 krkr 语义；GPU 只做上传 + 一个 quad。
- 不保留第二条合成路径（`Scene`/自写 `Blend` 必须退场）。
- 不为了"能跑"去改 vendor 源码；确需修改的登记 `vendor/krkrz/VENDOR.md`。

## 7. 风险与回滚点

| 风险 | 应对 | 回滚 |
|---|---|---|
| 依赖闭包比预期大（`EventIntf` 366 行、`StorageIntf` 302 行） | 用链接驱动：只实现被引用到的符号，登记成清单 | R1-3 之前的每个阶段都是增量改动，回滚 = 不链接新目标 |
| `LayerIntf` 与 `WindowIntf` 双向依赖（`layerTreeOwnerInterface`） | 实现该接口，不改 legacy | 同上 |
| 字体度量不一致 | 在 R1-2b 接入 FreeType 路径，oracle 覆盖文本 | oracle 用例独立，可单独标记 |
| CPU 合成上传带宽 | R1-4 强制脏矩形 + 一帧一次上传 | 保留软件落盘路径做对照 |
| 与 `tjs/` 的 ABI 差异 | 已核对：同 tag，仅 CRLF | — |

## 7.1 进度记录

### R1-0（完成）清单与校验

- `vendor/krkrz/VENDOR.md`：来源 tag、9 个上游文件、三态归属、逐文件 sha256。
- `tools/verify_vendor.py`：sha256 + 补丁面校验（`##(` 与 `screen_blend_func` 的 `~`）。
- 实测：`visual/tvpgl.c` + `visual/gl/blend_function.cpp` 在 Termux/Clang（C 与 C++）下直接编译通过，
  零源码修改（除登记的两个补丁）。

### R1-1（完成）像素函数接入

| 位置 | 内容 |
|---|---|
| `vendor/krkrz/visual/porting/toolchain_compat.h` | 为上游头补齐非 Windows 缺失的宏（不改上游源码） |
| `vendor/krkrz/visual/porting/pixel_routines.h` | 上游像素函数表的唯一入口（先 `tjsTypes.h`，再 compat，再 `tvpgl.h`） |
| `pixel/LayerOps.*` | 29 种操作的词汇表 + 名字/是否读源 alpha/中性色 |
| `pixel/Blend.*` | 单像素混合：`op → 上游函数指针`（plain / `_o`），`ltOpaque` 走 `bmCopy` 分支 |
| `visual/LayerTypes.h` | 过渡期别名（词汇表归 `pixel`，合成层只是消费者） |
| `host/BlendMatrixProbe.cpp` | 29 × 5 alpha × opacity 的不变量 + 官方公式比对（300 项断言） |

验收输出：

```
blend_matrix: dst=0xFF4080C0 src=0xFFC86432
covered: 125 documented comparison(s), 5 skipped (no usable formula)
largest deviation from the float model: 2 channel(s) at PsColorDodge: alpha 255
ALL PASS (0 failure(s) / 300 check(s))
ctest: 22/22 通过
```

四个实测结论（写下来避免以后重新踩）：

1. **上游便携路径有一个真 bug**：`screen_blend_func`（带 opacity 的屏幕合成）少了最后的 `~`，
   结果是 `255 - screen(...)`；Windows 走 SSE2 变体（有 `~`），所以只在非 x86 暴露。
   已登记补丁 #2，修复方向是**向 Windows 靠拢**。
2. **`ltOpaque` 不是"直接复制"**：上游 `bmCopy` 在 `opa==255` 走 `TVPCopyColor`，否则走
   `TVPConstAlphaBlend`（常量比例，**忽略源 alpha**）。这解释了为什么 opaque 层的不透明度
   行为与 `ltAlpha` 不同。
3. **定点舍入会产生 ±1～±2 的偏差**：`alpha_blend` 用 `(x*a)>>8`，`_o` 变体还先把 opacity 折进
   源 alpha 再 `>>8` 一次。所以 128 不透明度落在 126，白色乘 255 得 254。测试的容差因此是 3，
   而不是 0；逐像素 0 容差要靠 R1-2 的 Windows oracle（同一条代码路径）才有意义。
4. **上游的非 `_d`/`_a` 变体不维护目标 alpha**（有的会清零）。本层合成的是不透明窗口表面，
   所以在 `pixel::BlendPixel` 里把 alpha 规范回 `0xFF`；图层间 alpha 语义留给 R1-3 的 legacy 位图。

### R1-2 的实测盘点（编译驱动，不是估计）

用上游源码 + 现有 vendor/porting 逐个 `-fsyntax-only`（`-fdelayed-template-parsing`，
与 tjs 目标同一套垫层参数），得到的**剩余阻塞项**：

| 文件 | 行数 | 状态 | 剩余阻塞 |
|---|---:|---|---|
| `ComplexRect.cpp` | 1,224 | ✅ 0 错误 | — |
| `FontSystem.cpp`、`PrerenderedFont.cpp` | 264 | ✅ 0 错误 | — |
| `LayerBitmapIntf.cpp` | 4,103 | 差 1 个头 | `ThreadImpl.h`（我们写） |
| `LayerBitmapImpl.cpp` | 1,674 | 差 1 个头 | `SysInitImpl.h` + `BitmapBitsAlloc`（我们写） |
| `GraphicsLoaderIntf.cpp`、`LoadTLG.cpp`、`SaveTLG5.cpp` | 3,968 | 差 1 个头 | `DetectCPU.h`（我们写） |
| `LoadPNG.cpp` | 677 | 差 libpng | `png.h`/`pngstruct.h`/`pnginfo.h`（vendored libpng） |
| `FreeType*.cpp` | 889 | 差 FreeType | `ft2build.h`（vendored FreeType） |

已经落地的兼容项（都在 `visual/porting/toolchain_compat.h`，上游零修改）：
`TVP_GL_FUNC_*`/`TVP_GL_IA32_FUNC_*` 宏、`BITMAPINFO`/`BITMAPINFOHEADER`/`RGBQUAD`、`HRGN`、
`MAX_PATH`、`OutputDebugString` 两个重载；外加 3 个 VENDOR 补丁（`##(`、`screen_blend`、`TJS::` 前置声明）。

### 字体：不是算子/合成问题（须先决策）

上游 Windows 默认 **GDI 光栅器**（`LayerBitmapImpl.cpp:66`），FreeType 是备选；Android 只能用
FreeType，老树还为此重写了字体枚举与回退（`FontImpl.cpp`、`FontBaseline.h`）。
所以 vendor 只能带走**代码路径**，带不走光栅器 → 文本要逐像素一致，必须让 oracle 也切 FreeType
并锁定 hinting/gamma 参数；否则只能断言度量。详见 `docs/rendering-architecture.md` §4.1。

### 剩余工作量（1 人、熟悉本仓库，单位：人日）

| 片 | 内容 | 估计 |
|---|---|---:|
| R1-2a | seam 原语：`ThreadImpl.h`、`SysInitImpl.h`、`DetectCPU.h`、`storage_media`（`iTVPStorageMedia` + `tTJSBinaryStream` 接 `krkr::io::Storage`）、`bitmap_alloc`、事件泵骨架、tick | 2–3 |
| R1-2b | vendored zlib/libpng/FreeType + `LoadPNG/TLG/JPEG`、`SaveTLG5/6`、`Bitmap.save`；Windows oracle（`saveLayerImage` 落盘 + `diff_images.py` + CTest） | 5–8 |
| R1-3 | 图层模型：`LayerIntf`(10.3k)、`LayerManager`、`TransIntf`、`ImageFunction`、`LayerTreeOwner*`、`WindowIntf`(1.9k) + `iTVPWindow`/`iTVPLayerTreeOwner`/`iTVPDrawDevice` seam；删掉自建 `Scene` | 8–12 |
| R1-4 | Android `VisualDrawDevice`：BGRA 上传、脏矩形、一 quad、Y 翻转、真机一帧 | 3–5（真机调试可能 ×2） |
| R1-5 | 收口：删 `Scene`/`stb_image`/`GLBackend`、文档、CI | 1–2 |
| **合计** | | **19–30** |

“可用”的三级定义（回答“还需要多少工作可用”这个问题）：

| 级别 | 判定 | 累计 |
|---|---|---:|
| **L1 单帧出图、可与 Windows 逐像素比** | R1-2 完成：能解 PNG/TLG、能存图；用探针而不是脚本驱动 | 7–11 人日 |
| **L2 脚本能建 Layer/Window，KAG 静态画面正确** | L1 + R1-3（含 29 类型、clip、遮罩、转场基线） | 15–23 人日 |
| **L3 能跑对话（输入 + 字体排版）** | L2 + 第 9 步输入（约 3–5 人日）+ 字体度量对齐 | 18–28 人日 |
| **L4 日常驱动（声音/视频/存档/性能）** | L3 + 第 10 步音频（8–12）、存档配置、性能 | 30–45 人日 |

风险项（各可能 +1–3 天）：转场/`ltEffect`/`ltFilter` 的离屏路径、TLG6、字体度量与抗锯齿、
真机 GPU/色彩空间差异、以及上游便携路径与 SSE2 路径的其它差异（已发现 1 个）。

## 8. 下一步

R1-0 与 R1-1 已完成。下一步是 **R1-2（位图 + 字体 + 解码）**：

1. vendor `LayerBitmapIntf.*`、`BitmapIntf.*`、`FontSystem`/`FreeType*`/`PrerenderedFont`、
   `GraphicsLoaderIntf`、`LoadPNG/LoadJPEG/LoadTLG`、`SaveTLG5/6`；
2. 写 `vendor/krkrz/visual/porting/bitmap_memory.*`（替代 `win32/BitmapBitsAlloc`：
   malloc + 对齐 + 哨兵）、`system_font.*`（FreeType）、`storage_media.*`（接 `krkr::io::Storage`）；
3. 引入 zlib（PNG/TLG 需要；`puff` 只用于 XP3 段）；
4. 验收：`Layer.loadImages` → `Layer.saveLayerImage` 与 Windows krkrz 落盘逐字节一致，
   并把 `tools/diff_images.py` 接进 CTest。
