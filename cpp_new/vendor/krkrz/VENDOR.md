# vendor/krkrz — 上游来源、校验与补丁登记

本目录是 **krkrz 上游代码的原样拷贝**，与 `cpp_new/tjs/` 同等待遇：不承载适配逻辑，
改动必须登记在本文件。我们的适配只允许出现在 `visual/porting/`（README 见 §4）。

## 1. 来源

| 项 | 值 |
|---|---|
| 仓库 | `krkrz/krkrz`（吉里吉里Z，Windows 原生线） |
| 本地克隆 | `~/AppProjects/krkrz` |
| 提交 | `48c10558`（`git describe --tags` = `1.4.0r2-4-g48c10558`） |
| 许可 | BSD 3-clause 风格（保留版权声明，见上游 `LICENSE`） |
| 与 `cpp_new/tjs/` 的关系 | 同一 tag。`tjs2/tjsCommHead.h` 与 `cpp_new/tjs/tjsCommHead.h` 仅换行差异，因此本目录代码直接编在这份 tjs 上 |

## 2. 首批文件（R1-1：像素函数）

| 文件 | 角色 | 补丁处数 | 本地 sha256 |
|---|---|---:|---|
| `visual/tvpgl.c` | 像素表、初始化、非混合类算子（拉伸/变换/TLG/格式转换） | 0 | `eb340b75…fe6074` |
| `visual/tvpgl.h` | 导出声明（函数指针表）+ 后缀约定 | 0 | `62a37361…c02cdd` |
| `visual/drawable.h` | `tTVPLayerType`（29 值）与绘制面/命中类型 | 0 | `a28b9b93…83ce22` |
| `visual/gl/blend_function.cpp` | **29 种混合算子的便携实现**（模板 + 查表），`TVPGL_C_Init()` | **13** | `f6a85099…7651cc` |
| `visual/gl/blend_functor_c.h` | 混合/通道算子的 C 模板实现 | **1** | `d707fa39…a03f7a` |
| `visual/gl/blend_util_func.h` | 定点辅助（`tvpgl` 的舍入风格） | 0 | `a0e3d5bc…ba9f3` |
| `visual/gl/blend_variation.h` | 变体（`d` / `a` / `o` / `HDA`）展开 | 0 | `af7252e3…d76a31b4` |
| `visual/gl/tvpgl_mathutil.h` | 通道解包/打包 | 0 | `f2dcdec4…fb09f` |
| `visual/IA32/tvpgl_ia32_intf.h` | IA32 派发声明（仅被上者 include；本平台不实现） | 0 | `03aadbbd…ecddc6` |
| `visual/GraphicsLoaderIntf.h` | 图片装载器基类（`TJS::tTJSBinaryStream` 前置声明的 MSVC 写法） | **1** | `4c924e13…dcdcd73` |

完整 sha256 见 §5。校验命令：`python3 cpp_new/tools/verify_vendor.py`。

**明确不取**（理由见 `cpp_new/docs/legacy-gap.md` §3）：

- `visual/win32/**`：Windows 平台切片（`WindowImpl` / `BasicDrawDevice` / `TVPSysFont` / `BitmapBitsAlloc` …），
  由 `visual/porting/` 重写。
- `visual/IA32/*.c|asm`、`visual/gl/*sse2.cpp`、`visual/gl/*avx2*`、`visual/gl/x86simdutil*`：
  x86 专用；ARM 走 `tvpgl.c` + `blend_function.cpp` 的便携实现。
- `tjs2/**`：已在 `cpp_new/tjs/`（同 tag）。

## 3. 补丁登记

### 补丁 #1 — `visual/gl/blend_function.cpp`：`##(` → ` (`

- 位置：宏 `DEFINE_CONVERT_BLEND_FUNCTION` / `DEFINE_ALPHA_COPY_FUNCTION` 等 13 处
  （`static void TVP_##FUNC##(...)`）。
- 现象：GCC/Clang 报 `pasting formed 'TVP_remove_const_opacity(', an invalid preprocessing token`。
  `##` 与 `(` 相邻属于 MSVC 的宽松扩展，标准预处理器拒绝。
- 处理：删除 `##`（`TVP_##FUNC (…`），语义不变（`FUNC` 仍是单个标识符）。
- 归属：工具链差异，符合 `docs/ARCHITECTURE.md` §4.2「上游代码在目标工具链下无法编译」的例外条件。

### 补丁 #2 — `visual/gl/blend_functor_c.h`：`screen_blend_func` 补回 `~`

- 位置：`struct screen_blend_func::operator()(d, s, a)`（`_o` 变体，带 opacity 的屏幕合成）。
- 现象：末尾 `return tmp;` 少了取反，结果等于 `255 - screen(...)`（实测把目标色反过来了）。
  同文件的 2 参版本 `screen_blend_functor` 是 `return ~tmp;`，x86 的
  `sse2_screen_blend_o_functor` 末尾也有 `_mm_xor_si128(md, mask_)`，只有这条便携路径漏了。
- 为什么长期没被发现：Windows 构建由 `TVPGL_IA32_Init()` 换成 SSE2 变体，便携 C 路径只在
  非 x86 平台生效。
- 处理：`return tmp;` → `return ~tmp;`。这是**向 Windows 行为靠拢**（不是偏离上游）。
- 证据与回归：`host/BlendMatrixProbe.cpp` 的 "opacity 0 keeps the target" 断言在补丁前会报
  `Screen: 0xBE7E3E vs 0x4080C0`，补丁后通过。

### 补丁 #3 — `visual/GraphicsLoaderIntf.h`：`class TJS::tTJSBinaryStream;` → `namespace TJS { class ... }`

- 现象：标准 C++ 不允许带嵌套名字说明符的前置声明，Clang/GCC 报
  `forward declaration of class cannot have a nested name specifier`。
- 影响面：`GraphicsLoaderIntf.cpp`、`LoadPNG.cpp`、`LoadTLG.cpp`、`SaveTLG5.cpp` 都包含它。
- 处理：改成 `namespace TJS { class tTJSBinaryStream; }`，语义完全等价。

### 已知但暂未打补丁的差异（非 Windows 下的宏补全）

`visual/tvpgl.h` 与 `visual/IA32/tvpgl_ia32_intf.h` 里的 `TVP_GL_FUNC_*` / `TVP_GL_IA32_FUNC_*`
宏只在 `#ifdef _WIN32` 下定义（例如 `rettype (__cdecl * funcname) arg`）。非 Windows 编不过。

**处理方式：不改上游源码**，由 `visual/porting/toolchain_compat.h` 在包含前补齐同名宏
（去掉 `__cdecl`）。这样上游文件保持零修改，补丁面最小。

## 4. 我们写的部分（不是上游代码）

`vendor/krkrz/` 下有两类文件，`VENDOR.md` §2 只列上游；下面这些**是本仓库实现**：

| 位置 | 为什么必须由我们写 |
|---|---|
| `msg/MsgIntf.{h,cpp}` | 上游把 152 条消息留成 `NULL`，文案在 `Messages.xlsx` 里，构建期由 Perl/Excel 生成 Win32 字符串资源、运行时 `LoadString` 读入。Android 没有这条路径 → 我们保留同名符号与签名，文案由 `tools/gen_legacy_messages.py` 直接生成为宽字符串字面量（`msg/generated/messages.inc`，344 条，含中/日/英，默认中文） |
| `visual/porting/toolchain_compat.h` | 补非 Windows 缺失的宏与 Win32 POD 类型（`BITMAPINFO` 家族、`HRGN`、`MAX_PATH`）以及调试输出声明 |
| `visual/porting/pixel_routines.h` | 上游像素函数表的唯一入口（先 `tjsTypes.h`，再 compat，再 `tvpgl.h`） |
| `visual/porting/debug_output.{h,cpp}` | `OutputDebugString` → `core/log` |

### 4.1 第三方依赖（R1-2 起）

上游的图片装载与字体走 `external/`：`LoadPNG.cpp` 直接 include `png.h`/`pngstruct.h`/`pnginfo.h`，
`FreeType*.cpp` 需要 `ft2build.h`。因此 R1-2 会把 **zlib / libpng / FreeType** 作为 vendored 第三方
（放 `vendor/third_party/`，与上游同版本线），而不是链系统库 —— 理由与 `puff` 的选择一致：
Windows 开发机没装就编不过，而 vendored 源码在哪都一样。

## 5. `visual/porting/` 是什么

`visual/porting/` 不是上游代码，是我们为「让上游在非 Windows 工具链上编译」与
「实现上游依赖的平台原语」而写的适配层。命名不使用上游的 `TVP`/`tTVP` 前缀（除引用上游宏/符号处）。

| 文件 | 职责 |
|---|---|
| `toolchain_compat.h` | 为上游头补齐非 Windows 分支缺失的宏（`TVP_GL_FUNC_*`、`TVP_GL_IA32_FUNC_*`）、Win32 POD 类型与常量 |
| `pixel_routines.h` | 上游像素函数表的唯一入口 |
| `debug_output.*` | `OutputDebugString` → `core/log` |
| （R1-2a）`thread_impl.h` | `ThreadImpl.h` 的可移植版本（`std::mutex` / `std::thread`） |
| （R1-2a）`bitmap_alloc.*` | 位图内存分配（malloc + 对齐 + 哨兵），替代 `win32/BitmapBitsAlloc` |
| （R1-2a）`storage_media.*` | `iTVPStorageMedia` + `tTJSBinaryStream` 适配 `krkr::io::Storage` |
| （R1-2a）`sys_init.*`、`detect_cpu.h` | `SysInitImpl.h` / `DetectCPU.h` 的最小实现（路径、CPU 特性查询） |

## 6. 完整校验表

见 `tools/verify_vendor.py` 的常量表；改动或新增上游文件时必须同时更新该表与本文件 §2。
