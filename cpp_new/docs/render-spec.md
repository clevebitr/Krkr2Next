# 渲染规格（第 8 步需求书）— cpp_new

> 目标：把"KiriKiri 的合成到底要做什么"写成可执行的清单，并确认代码从哪取。
>
> 证据来源（都是实测读过的文件，不是猜的）：
>
> | 主题 | 文件 |
> |---|---|
> | 图层类型枚举（29 种） | `F:/Projetcs/krkrz_gl/visual/drawable.h:21-55` |
> | 图层参数/接口 | `visual/LayerIntf.h`（`SetOpacity` / `SetType` / `SetClip` / `SetFace` …） |
> | 软件合成算子（权威数学） | `visual/tvpgl.c` |
> | GL 渲染器结构（10.4k 行） | `F:/Projetcs/krkrz_gl/visual/opengl/*` |
> | GL Canvas 的默认 shader | `visual/opengl/CanvasIntf.cpp:48-78` |
> | 图层位图 / 遮罩存储 | `visual/LayerBitmapIntf.h`（`tTVPBaseBitmap(w,h,bpp=32)`，`SetPointMask`） |
> | 局部更新区域 | `visual/LayerManager.h:246` `tTVPComplexRect UpdateRegion` |
> | TLG5/TLG6 解码 | `visual/LoadTLG.cpp` |
> | 许可 | `krkrz/LICENSE` = BSD 3-clause 风格（保留版权声明、不得用作者名背书） |

---

## 0. 选型确认（结论）

**不整搬任何引擎的渲染器。三块分开取源：**

| 取什么 | 从哪取 | 条款/方式 |
|---|---|---|
| **合成数学（算子语义）** | `krkrz/visual/tvpgl.c`（BSD-3） | 逐算子对照抄，含定点舍入 |
| **GL 抽象结构** | `krkrz_gl/visual/opengl/*`（Canvas / Texture / ShaderProgram / VertexBuffer / Matrix32 / FBO / Offscreen） | 只抄**接口切分与渲染流程**，实现自己写 |
| **平台接线（EGL / 窗口 / swap）** | **自家** `Krkr2Next/cpp/core/visual/ogl/krkr_egl_context.cpp`（577 行，真机验证过） | 直接搬 |
| **图片解码** | `stb_image.h`（单文件、public domain） + 自写 TLG（`LoadTLG.cpp` 参照） | vendored |

**不采用：**

- `Kirikiroid2`（2024 停更；`cpp/core/visual` 就是它的后裔 —— 同一份代码，零增量）
- `krkrsdl2` 的 SDL2 平台层（Android 上要带 SDL2 + 它的 Java glue；而"零拷贝 SurfaceTexture"SDL 不管）
- `krkrsdl2` 的 Android 平台实现：`visual/opengl/OpenGLPlatformAndroid.cpp` **只有 34 行，是 stub**；同目录 Win32 版 1038 行 —— 说明这块在这个 fork 里没被实现过
- cocos2d-x、`SDL_Renderer`、bgfx

---

## 1. 数据模型

### 1.1 Layer（可见的绘制单元）

| 参数 | 语义 | 备注 |
|---|---|---|
| `Visible` | 是否参与合成 | 关闭 = 整层跳过 |
| `Opacity` | 0..255 全局透明度 | 乘到源 alpha 上 |
| `Type` | `tTVPLayerType`（29 种，见 §3） | 决定混合方程 |
| `Left/Top/Width/Height` | 目标矩形（窗口坐标） | |
| `ImageLeft/ImageTop` | 源位图上的偏移 | 与上者组合出 uv 偏移 |
| `Clip*` | 裁剪矩形 | 层内裁剪，等价 scissor |
| `Matrix`（`t2DAffineMatrix`，6 个 float） | 仿射：缩放/旋转/平移 | 非单位阵时走 affine 采样路径 |
| `Face`（`tTVPDrawFace`） | `dfBoth` / `dfMain` / `dfMask` | 决定画颜色通道还是遮罩通道 |
| `HitType` | `htMask` / `htProvince` | 命中测试，**与渲染无关**，第 9 步（输入）用 |
| `Province` | 命中区域位图 | 同上 |

### 1.2 位图与遮罩

- 图层位图固定 **32bpp**：`tTVPBaseBitmap(w, h, bpp = 32)`（`LayerBitmapIntf.h:149`）。
- **遮罩不是独立通道**：`SetPointMask(x, y, mask)` 与 `SetPointMain(x, y, color)` 都在同一个 32bpp 位图上 —— 遮罩就是 alpha 通道的一种解释。
- 因此"画遮罩"= 用 `dfMask` 面把 alpha 当灰度值写/读。

### 1.3 树与窗口

- 图层树 `LayerTreeOwner`，顶层挂到 `Window`。
- 只有**顶层**才需要真正提交到屏幕；中间层可以渲染到 FBO/离屏位图（`Offscreen`）。

---

## 2. 变换与采样

三种路径，按图层参数自动选择（不是三套 API）：

| 路径 | 触发条件 | 采样 |
|---|---|---|
| 直接拷贝 | 无缩放、无旋转、整像素对齐 | 1:1，无插值 |
| `Stretch` | 目标尺寸 ≠ 源尺寸，无旋转 | 最近邻 / 双线性（`InterpStretch*`） |
| `Affine` / `LinTrans` | 有旋转/斜切（Matrix 非单位阵） | 双线性（`InterpLinTrans*`） |
| `UnivTrans` | 通用变换（`tTVPUnivTrans`） | 预留，真实游戏少见 |

**规格要点**：三种路径的混合方程**完全相同**，只是采样不同。实现上应该"同一份 blend 代码 + 三种采样器"，而不是三套渲染通路 —— 这是 krkrz `xStretchxBlend` / `xLinTransxBlend` 命名法的由来。

---

## 3. 混合算子（29 种图层类型）

`tTVPLayerType`（`visual/drawable.h`）按值分组：

| 值 | 类型 | 语义 | GL 可行性 |
|---:|---|---|---|
| 0 | `ltBinder` | 不画自己，只画子层 | 直接支持（跳过本层） |
| 1 | `ltCoverRect` / `ltOpaque` | 不透明覆盖 | `glBlendFunc(ONE, ZERO)` |
| 2 | `ltTransparent` / `ltAlpha` | 标准 alpha 混合 | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` |
| 3 | `ltAdditive` | 加法 | `SRC_ALPHA, ONE` |
| 4 | `ltSubtractive` | 减法 | 需 shader 或 `glBlendEquation(GL_FUNC_REVERSE_SUBTRACT)` |
| 5 | `ltMultiplicative` | 乘法 | `DST_COLOR, ZERO`（预乘近似） |
| 6 | `ltEffect` | 效果层（子层合成后整体作用） | 需 FBO |
| 7 | `ltFilter` | 滤镜层（同上，可带滤镜参数） | 需 FBO + shader |
| 8 | `ltDodge` | 颜色减淡（非 PS 版） | shader |
| 9 | `ltDarken` | 变暗 | shader（`GL_MIN` 可近似） |
| 10 | `ltLighten` | 变亮 | shader（`GL_MAX` 可近似） |
| 11 | `ltScreen` | 滤色 | `ONE_MINUS_DST_COLOR, ONE` |
| 12 | `ltAddAlpha` | 加法 alpha（alpha 也相加） | `ONE, ONE` |
| 13–28 | `ltPsNormal` … `ltPsExclusion` | Photoshop 家族 16 种 | **GL 无固定管线，必须 shader 或软件兜底** |

**结论（关键）**：krkrz 的 GL 路径只实现了 **5 种**（`tTVPBlendMode`：`bmDisable/ bmOpaque / bmAlpha / bmAdd / bmAddWithAlpha`，见 `CanvasIntf.h:18`），Photoshop 家族只在软件路径（`tvpgl.c` 的 `TVPPs*Blend`）里存在。

→ cpp_new 必须**两层都做**：GL 后端覆盖常用 5~12 种；遇到 `ltPs*` 必须能**回落到软件合成**，否则某些游戏的光效/立绘会画错。

### 3.1 数学定义（8-bit，直通 alpha）

以 `src`（源，可带逐像素 alpha 与图层 opacity）、`dst`（目标）为像素，`As` = 源 alpha × opacity：

```
Opaque      : out = src
Alpha       : out.rgb = src.rgb*As + dst.rgb*(1-As)
              out.a   = As + dst.a*(1-As)
Add         : out.rgb = min(dst.rgb + src.rgb*As, 255)
Sub         : out.rgb = max(dst.rgb - src.rgb*As, 0)
Mul         : out.rgb = dst.rgb * (src.rgb*As + (1-As)) / 255
Screen      : out.rgb = 255 - (255-dst.rgb)*(255-src.rgb*As)/255
AddAlpha    : out.rgb = min(dst.rgb + src.rgb*As, 255); out.a = min(dst.a + As, 255)
Ps*         : 按 Photoshop 定义（tvpgl.c 里 TVPPs*Blend 逐个实现）
```

⚠️ **定点舍入以 `vendor/krkrz` 为准，不自写**（R1-1 起）。上游用 `(x*a)>>8` 这类 ÷256 的快速近似，
且 `_o` 变体先把 opacity 折进源 alpha 再 `>>8` 一次，因此与"标准公式除 255"相差 1～2，
`opacity=128` 实际落在 126。语义断言（`host/BlendMatrixProbe.cpp`）与浮点模型比对时容差取 3；
逐像素 0 容差只在同一条代码路径（R1-2 的 Windows 落盘 oracle）上才成立。

实现位置：`pixel/`（`krkr::pixel::BlendPixel`）。29 种操作到上游函数指针的映射在
`pixel/Blend.cpp` 里，`visual/` 只消费，不再持有混合数学。

---

## 4. 遮罩 / Clip / Face

| 机制 | 语义 | 实现 |
|---|---|---|
| `Clip` | 矩形裁剪 | `glScissor`（软件后端 = 逐行跳过） |
| `Mask`（`dfMask`） | 用 alpha 通道当灰度蒙版 | 每个像素 `src.a *= mask`；GL 用第二张贴图 + shader 乘法 |
| `Face` | 只画 main / 只画 mask / 都画 | 决定写哪个通道 |
| `ltEffect` / `ltFilter` | 先合成子层到离屏，再整体作用 | 需要 FBO + 一次额外 pass |

---

## 5. 局部更新（脏矩形）

- `tTVPComplexRect UpdateRegion`（`LayerManager.h:246`）维护"本帧哪些矩形变了"。
- 规格：**没有变化的区域不应该重画**。真机上这条直接决定续航。
- 对 cpp_new 的约束：draw list 里每一项都带自己的脏矩形；后端可以只清/画这些矩形（`glScissor`）。
- 第一阶段（8a/8b）**允许整屏重画**，但 draw list 的数据结构必须能表达脏矩形，否则以后要重构。

---

## 6. 资源与纹理

| 格式 | 谁在用 | 解码难度 |
|---|---|---|
| PNG | `LoadPNG.cpp` | `stb_image` 直接搞定 |
| JPEG | `LoadJPEG.cpp` | 同上 |
| **TLG5** | KiriKiri 主格式 | zlib 块 + 逐行预测（`TVPTLG5DecompressSlide`）。**我们已经有 puff，可直接用** |
| **TLG6** | KiriKiri 主格式 | 分块 + 自定义编码，比 TLG5 复杂得多，先不做 |
| 压缩纹理（ETC/PVR） | 老树 `RenderManager_ogl` 里有 | 可选，不做也能跑 |

纹理上传规格：一律转 **RGBA8**、**预乘与否必须统一**（建议 GL 侧用非预乘 + `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`，与软件后端一致，便于逐像素比对）。

---

## 7. 字体与文字

- `PrerenderedFont`：把字形渲染进一张位图（atlas），之后当普通纹理贴。
- 规格：文字 = **一堆带 uv 的四边形**，与图片走同一条 draw list。所以渲染器不需要"懂文字"。
- 字体光栅化本身（FreeType）是第 7 步之后的事，第 8 步只需能吃现成的字形位图。

---

## 8. 视频叠加

- `VideoOvlIntf`：视频帧作为一张纹理贴到某个矩形上，混合模式 `ltOpaque`。
- 对第 8 步的要求：**只要支持"外部提供的 RGBA/YUV 纹理上传"即可**，不需要解码。

---

## 9. GL 后端的最小形态（来自 krkrz Canvas）

`CanvasIntf.cpp:48-78` 的默认 shader 就是最小可用管线：

```glsl
// vertex
attribute vec2 a_pos; attribute vec2 a_texCoord;
uniform mat4 a_modelMat4; uniform vec2 a_size;
void main(){
  mat4 ortho = mat4(vec4(2.0/a_size.x,0,0,0), vec4(0,-2.0/a_size.y,0,0),
                    vec4(0,0,-1,0), vec4(-1,1,0,1));
  gl_Position = ortho * a_modelMat4 * vec4(a_pos, 0.0, 1.0);
  v_texCoord = a_texCoord;
}
// fragment
precision mediump float; varying vec2 v_texCoord;
uniform sampler2D s_tex0; uniform float a_opacity;
void main(){ vec4 c = texture2D(s_tex0, v_texCoord); c.a *= a_opacity; gl_FragColor = c; }
```

即：**一个四边形批 + 一个 shader + `glBlendFunc` 切模式 + `glScissor` 切 clip**。KiriKiri 的复杂度全在"合成策略"和"算子正确性"，不在 GL 本身 —— 这就是为什么它可以用 200 行 GL 代码 + 一套状态机表达。

krkrz 的结构（`visual/opengl/`，10.4k 行）值得照抄的部分：

| 接口 | 职责 |
|---|---|
| `CanvasIntf` | 状态栈（矩阵/clip/culling）、draw call、混合模式 |
| `TextureIntf` | 纹理创建/上传/虚拟纹理（大图分块） |
| `ShaderProgramIntf` | 编译/链接/参数绑定 |
| `VertexBufferIntf` + `VertexBinderIntf` | 顶点数据与属性绑定 |
| `Matrix32Intf` / `Matrix44Intf` | 2D/3D 矩阵，暴露给 TJS（`Matrix32` 类） |
| `GLFrameBufferObject` | FBO（离屏渲染 / 遮罩 / effect 层） |
| `OffscreenIntf` | 离屏表面（渲染到纹理） |
| `OpenGLScreen` | 帧缓冲区、swap、scissor、vsync |

---

### 9.1 dst-dependent 算子（Ps* 家族）的难点（旧树实测）

Photoshop 家族（Overlay / HardLight / SoftLight / ColorDodge / …）需要**读取目标像素**才能算。
旧树 `RenderManager_ogl.cpp` 的做法（实测）：

- 探测 `GL_EXT_shader_framebuffer_fetch` / `ARM_` / `NV_` 扩展（第 258–261 行）；
- 能用则在 fragment shader 里直接读 framebuffer（`TEST_SHADER_BLTO(PsOverlayBlend)` 等，第 4127–4136 行）；
- 不能用（大量 Android 设备）就得另一条路：渲染到 FBO / ping-pong 纹理。

**两条路径 + 设备差异 + 无对照测试 = “平台层一团糟”的主要来源。**

cpp_new 的规避方式：

1. 软件后端先把这些算子按 `tvpgl.c` 实现（权威、可断言）；
2. GL 后端**只走一条路**：dst-dependent 算子一律渲染到离屏 FBO（不探测扩展、不做 shader 变体分叉）。
   代价是多一次离屏 pass，收益是行为可预测；
3. 将来真要提速，再拿实测数据决定要不要加 `framebuffer_fetch` 快速路径。

---

## 10. 平台接线（用自家的）

| 平台 | 来源 | 状态 |
|---|---|---|
| Android | **自家** `krkr_egl_context.cpp`（577 行） | 真机验证过，支持 `eglMakeCurrent(EGL_NO_SURFACE)` + 换 surface（SurfaceTexture 零拷贝需要） |
| krkrz 的 Android | `OpenGLPlatformAndroid.cpp` | **34 行 stub，不可用** |
| Windows（仅测试台） | krkrz `OpenGLPlatformWin32.cpp`（1038 行）或 SDL2 | 只在开发机上用，不进产品 |

---

## 11. cpp_new 的落地设计

```
引擎（Layer/合成策略）  ──►  DrawList（数据，无 GL）  ──┬──► SoftwareBackend  （像素权威，可断言）
                                                        └──► GLBackend        （GLES3，手机上跑）
```

### 11.1 DrawList（稳定接口，后续不改）

```cpp
struct DrawItem {
  std::uint32_t textureId;        // 0 = 纯色填充
  std::uint32_t maskTextureId;    // 0 = 无遮罩
  float vertices[4][2];           // 目标坐标（已含仿射）
  float uvs[4][2];
  std::uint32_t blend;            // tTVPLayerType 的原值
  std::uint32_t opacity;          // 0..255
  Rect clip;                      // 裁剪
  Rect dirty;                     // 脏矩形（第一阶段可等于 clip）
};
struct DrawList { std::vector<DrawItem> items; std::uint32_t clearColor; };
```

### 11.2 为什么这么切

- "合成策略"（谁画在谁上面、矩阵怎么来）是**业务逻辑**，放引擎侧，可以在没有 GL 的机器上测。
- "算子正确性"用软件后端做**像素断言**（权威 = `tvpgl.c`）。
- GL 后端只做"翻译"，出错面小；调试时可以把同一帧的软件输出与 GL 输出**逐像素 diff**，一眼看出是算子错还是 GL 状态错。

### 11.3 断言策略

| 断言 | 判据 |
|---|---|
| 尺寸 / 坐标 | 精确相等 |
| 背景纯色 | 精确相等 |
| 不透明贴图 | 精确相等（无插值路径） |
| alpha 混合 / 加法 / 乘法 | 与 tvpgl.c 照抄的软件后端精确相等；GL 允许 ±1 |
| 缩放 / 旋转 | 允许 ±2（双线性实现有差异） |
| Ps* 家族 | 只与软件后端比，GL 要求回落 |

---

## 12. 分片与验收

| 片 | 内容 | 验收 | 需要手机？ |
|---|---|---|---|
| 8a | DrawList + `SoftwareBackend` + PPM 输出 | 用 `Storage` 读真包里的 `title_bg.png`（3.1 MB，已验证可读），输出 PPM，尺寸与指定像素断言通过 | 否 |
| 8b | `GLBackend`（GLES3）：纹理上传、四边形批、5~12 种 blend、scissor | 同一 DrawList，GL 输出与软件后端差 ≤1/像素 | 桌面 GL 即可 |
| 8c | EGL + SurfaceTexture（真机一帧） | 手机上出现 8b 那张图；旋转/改变尺寸不崩 | 是 |
| 8d | 遮罩（dfMask）+ Clip + effect/filter 层（FBO） | 遮罩与 effect 各一条像素断言 | 否（软件后端） |
| 8e | 脏矩形 + 差分更新 | 只改动 1 个矩形时，绘制项数/像素数下降（计数断言） | 否 |

---

## 13. 不做 / 已知差距（写下来防止以后返工）

| 项 | 为什么现在不做 |
|---|---|
| TLG6 | 解码复杂度高；真机游戏里 TLG5/PNG 足够先跑起来 |
| ETC/PVR 压缩纹理 | 内存优化，不是功能；等纹理内存吃紧再做 |
| `UnivTrans` 通用变换 | 真实游戏罕见 |
| Photoshop 16 种在 GL 里原生实现 | 先用软件兜底，正确性优先 |
| 多线程合成 / 光栅化 | 先单线程跑对，再谈性能 |
| `Province` 命中区域 | 属于输入/命中测试（第 9 步） |
| 字体光栅化 | 第 7 步以后；第 8 步只吃现成字形位图 |

---

## 14. 实现约定（8a 起的行为准则）

1. **DrawList 是唯一接缝**。引擎侧算好顶点与混合参数，后端只做"采样 + 混合 + 提交"。
   后端不得读图层树、不得做变换决策——否则软件/GL 两条路径必然分叉。
2. **软件后端是像素权威**。GL 后端的一切差异都以软件输出为参照；断言以"逐像素 ±1"
   为单位，而不是整帧截图对比（整帧对比定位不到错在哪个算子）。
3. **混合数学只有一份**：`visual/Blend.cpp`。`Surface::Composite` 与 `SoftwareBackend`
   共用它；新增算子只改这一处并补断言。
4. **采样路径可降级，但不能画错**。8a 只实现轴对齐 + 最近邻；顶点非轴对齐的项被跳过并
   计入 `RenderStats::itemsSkipped`，等 8b/8d 的双线性与仿射路径。宁可少画一帧，
   不可用错误采样污染像素断言。
5. **脏矩形从现在就在接口里**（`DrawItem::dirty`），即使 8a 整屏重画；否则 8e 要改结构。
6. **GL 入口点表放在 `core/contracts`**：平台装载、visual 消费，两边都不需要 include GL 头，
   也不需要互相引用（`platform` 不能引用 `visual`，反之亦然）。
7. **平台只认像素**：`core/contracts/IRenderTarget.h` 的 `Present(pixels, w, h, stride)`
   是 visual 与 platform 的唯一交界。`platform/` 不引用 `visual/`，`visual/` 不引用
   `platform/`，两者互不可见。
8. **呈现失败必须显式**：Android 在 8c 之前 `Present` 返回 false 并记录一次原因；
   不允许返回成功让上面以为画面已经出去。
9. **GL 只用函数指针装载**（8b）：`visual` 不 include 任何 GL 头，由 platform 把
   `glGenTextures/glDrawArrays/...` 注入后端。这样 CI 不需要 GL，Android 与 Windows 共用
   同一份后端代码，SDL 之后只是"又一个 loader + target"。
10. **呈现与合成单线程**：visual 不持有锁、不做线程同步；跨线程只在 engine 侧排队。
11. **SDL 的位置**：如果以后接 SDL，它是 `IRenderTarget` 的另一个实现（+ GL context provider），
    不改渲染核心；不要为了 SDL 把它的类型引进 `visual/`。

### 14.4 场景合成（7g-1）

`Layer` / `Window` 落地后，"谁画在谁上面"第一次有了真实数据源。接缝仍然只有一条：
引擎侧把图层树合成成 DrawList，后端只做采样与混合（§14.1 第 1 条）。

```
Window（脚本） ──► visual::Scene（窗口根 + 图层树）
                        │  BuildDrawList
                        ▼
                   DrawList ──► SoftwareBackend / GLBackend
```

语义（都对齐参照实现，逐条有依据）：

| 项 | 取值 | 说明 |
|---|---|---|
| 坐标 | `left/top` 相对父层原点 | 子层原点 = 父层的绝对原点，不是父层位图左上角 |
| `clip` | 只作用于本层四边形 | 不下传给子层（`SetClip` 是图层自身属性） |
| `visible` | 为假则本层与子层都不画 | 对应参照实现的 `nodeVisible` |
| `opacity` | 只作用于本层位图 | 不向子层累积；KAG 的分组淡入靠逐层设置 |
| 混合算子 | 只支持 `ltOpaque/Alpha/Additive/Subtractive/Multiplicative` | 其余 24 种跳过自身、子层照画，并计入 `SceneBuildStats::nodesUnsupported` |
| 默认值 | `type = ltAlpha`、`visible = false`、32×32 | 来自 `tTJSNI_BaseLayer` 构造函数 |

`SceneBuildStats` 是给断言用的：`nodesVisited / itemsEmitted / nodesHidden /
nodesUnsupported / itemsClippedOut`。没有它，"没画"与"算错"在测试里无法区分。

混合数学的归属在 R1-1 之后变了：`Scene` / `SoftwareBackend` 只决定"画什么、画在哪"，
算子本身来自 `pixel::BlendPixel`（上游 `tvpgl`）。29 种类型全部可画，只有 `ltEffect`/`ltFilter`
仍需离屏（8d）。

尚未做：遮罩（`dfMask`）、effect/filter 离屏层、脏矩形差分、仿射（矩阵）图层、
`ltPs*` 家族。这些都在 8d/8e 的分片里，`Scene` 的结构已经能容纳（节点带 `clip`/`dirty`）。

### 14.3 图像解码（8a 验收路径）

| 决定 | 内容 |
|---|---|
| 位置 | `visual/Image`（纯数据，ARGB8888）+ `visual/ImageDecoder`；**不依赖 Storage**，只吃字节 |
| 格式判定 | 按 magic，不看扩展名（包内扩展名与内容不一致真实存在）；TLG5/TLG6 显式报未实现 |
| 依赖 | `visual/third_party/stb_image.h`（v2.32，public domain，仅启用 PNG/JPEG/BMP） |
| 像素约定 | 解码结果统一成 `0xAARRGGBB`，与 Surface/Texture 一致，避免上层再转一次 |
| 验证 | 真包 `title_bg.png`（3.1 MB / 1920×1080）：本解码器的 FNV-1a 与 Pillow 对同一文件的解码结果**逐像素一致**（`0xC2B8E3C192CEA9EC`） |

用法：

```bash
# 断言：Storage → 解码 → 软件合成 → PPM（没有游戏包时退出码 77，CTest 记 skip）
build/cmake/debug/bin/image_probe build/real_root.txt title_bg.png build/title_bg.ppm
# Windows 运行：解码 → 合成 → 窗口呈现（半透明条确认混合路径）
build/cmake/debug/bin/image_window build/real_root.txt title_bg.png 120
```

### 14.2 8c 的边界

- 代码侧完成：EGL 上下文、surface 生命周期接口、两个平台共用一份 GL 入口点清单
  （`KRKR_GL_FUNCTION_LIST`，避免桌面/Android 漏项）。
- **未完成：JNI 接线与真机验证**。`bridge/engine_api` 目前仍是老实现，`surfaceCreated /
  surfaceChanged / surfaceDestroyed` 需要调到 `krkr_android_surface_*`；
  在那之前 8c 只能保证"NDK 下编译通过、符号存在"。

### 14.1 当前落地（8a 第一刀）

| 组成 | 位置 | 说明 |
|---|---|---|
| 表面与算子 | `visual/Surface.*`、`visual/Blend.*` | RGBA8888；Opaque/Alpha/Additive/Subtractive/Multiplicative |
| 绘制表 | `visual/DrawList.h` | 纹理表 + `DrawItem`（顶点/uv/混合/opacity/clip/dirty） |
| 软件后端 | `visual/SoftwareBackend.*` | 轴对齐 + 最近邻；返回 `RenderStats`（画了多少、跳过多少） |
| GL 后端 | `visual/GLBackend.*` | GLES3 风格（shader + VAO + 缓冲区）；5 种算子 |
| GL 入口点表 | `core/contracts/GLApi.h` | 平台装载、visual 消费；放在 core 是因为 visual 与 platform 互不引用 |
| GL 上下文契约 | `core/contracts/IGLContext.h` | `IsValid / MakeCurrent / SwapBuffers / LoadApi`；桌面与 Android 各自实现 |
| 桌面 GL | `platform/desktop/gl/GLContext.*` | WGL 3.3 core + 隐藏窗口 + 函数装载 |
| Android GL | `platform/android/AndroidGLContext.*` | EGL(ES3) + `ANativeWindow`；`Resize` 重建 surface |
| Android 表面桥 | `platform/android/AndroidSurface.*` | `krkr_android_surface_attach/resize/detach/valid` + `GetAndroidGLContext()`，供 JNI 调用 |
| GL 验收 | `host/RenderGlProbe.cpp` | 同一 DrawList 走软件/GL 两条路，逐像素比较；无 GL 环境退出码 77 → CTest skip |
| 呈现目标 | `platform/desktop/PpmTarget.*` | 离屏 PPM，CI 用 |
| 窗口目标 | `platform/desktop/win32/Win32WindowTarget.*` | Win32 + GDI，`host/RenderWindow` 手工验证 |
| Android 像素目标 | `platform/android/AndroidTarget.*` | 软件路径的占位：`Present` 显式失败（Android 走 GL 路径） |
| 断言 | `host/RenderProbe.cpp` | 原语 + DrawList 两条通道，手算期望值 |
| 混合数学 | `pixel/Blend.*`、`pixel/LayerOps.*` | 上游 `tvpgl` 的映射层（R1-1） |
| 上游像素代码 | `vendor/krkrz/visual/*` | 原样 vendored，补丁见 `vendor/krkrz/VENDOR.md` |
| 场景树 | `visual/Scene.*` | 窗口 → 图层树 → DrawList（见 §14.4） |
| 窗口/图层桥接 | `shim/classes/Tjs{Window,Layer}.*` | 脚本参数写进 `SceneNode`；不参与合成 |
| 场景断言 | `host/WindowProbe.cpp` | 脚本建场景 → 软件后端 → 手算像素 + PPM |
