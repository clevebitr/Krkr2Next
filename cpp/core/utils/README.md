# core/utils —— 四层：C 接口 / 纯工具 / TJS 接口 / 平台实现

`utils` 是引擎最底层，但它以前是 43 个文件的扁平目录 + 一个 `win32/` 子目录，
"哪些是不可再分的 C 代码、哪些是纯 C++ 工具、哪些是要 TJS 类型的接口"只能靠读文件。
现在按依赖层次切成四个目录：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `c_api/` | `core_utils_c_api` | C 接口与生成表：`md5.{c,h}`、`iconv/{iconv.h,utf8.h}`、`encoding/{gbk2unicode,jis2unicode}.c` | 无（不 include 任何 C++ 头） |
| `misc/` | `core_utils_misc` | 纯 C++ 小工具：`StringUtil` `LogUtil` `StallWatchdog` `DeferredDestroyer` `ObjectList` `MiscUtility` `Random` `VelocityTracker` `MathAlgorithms` `RealFFT` `Exception` `FilePathUtil` `Debugger` | STL + `tjs2` + `c_api` |
| `intf/` | `core_utils_intf` | TJS 接口契约：`ThreadIntf` `TimerIntf` `PadIntf` `ClipboardIntf` `DebugIntf` `TickCount.h` `AsyncTriggerMode.h` | `misc` + `impl`（已知的环，见下） |
| `impl/` | `core_utils_impl` | 平台实现（**原 `win32/`**）：`ThreadImpl` `TimerImpl` `PadImpl` `ClipboardImpl` `DebugImpl` `TVPTimer` | `intf` + `tjs2` |

`core_utils_module` 是一个 **INTERFACE 聚合层**，`base` / `environ` / `plugin` /
`visual` / `sound` 里的调用点零改动。

## `win32/` → `impl/`

那 9 个文件里 **DebugImpl / PadImpl / TVPTimer 都在 CMake 的源文件列表里**，
也就是 Android 构建照样编（只有 `PadImpl.cpp` 里 `PadFormUnit.h` 那段在 `#if 0` 里，
那是上游文本编辑器的遗物，文件在本仓库根本不存在）。

`sound/win32 → sound/impl`、`environ/win32` 解散，是同一批"命名误导目录"清理
（TODO.md P1 第 7 条）。这一批之后，全仓再无"目录名说 Windows、实际编 Android"
的地方。

## 一条半解：头依赖为零，链接依赖不为零

`utils` 的头**不 include 任何其它模块的头**（只有 `tjsCommHead.h` / `tjsNative.h`），
所以它看起来是完全的叶子。但链接期它不是：

```
impl/TVPTimer.cpp:59,112  TVPGetRoughTickCount32()
                          ↑ 声明在 intf/TickCount.h（本模块），
                            实现在 base/runtime/TickCount.cpp（DECISIONS.md #12 搬过去的）
```

`TickCount.h` 是"头在下层、实现在上层"的又一个例子，也是 `core_utils_module`
仍 PRIVATE 链 `core_base_module` 的唯一确定原因（`core_environ_module` 那条目前
找不到使用者，没有链接验证前不动）。

## 已知的环：intf ↔ impl（与 sound 同型）

三个接口头在**文件中段**include 它的实现头：

| 位置 | 语句 | 原因 |
|---|---|---|
| `intf/ThreadIntf.h` | `#include "ThreadImpl.h"` | `tTJSNC_Thread` 以 `tTJSNI_Thread` 为基类 |
| `intf/TimerIntf.h` | `#include "TimerImpl.h"` | 同上（`tTJSNI_Timer`） |
| `intf/PadIntf.h` | `#include "PadImpl.h"` | 同上（`tTJSNI_Pad`） |

反向边：`impl/ThreadImpl.h` → `ThreadIntf.h`、`impl/TimerImpl.h` → `TimerIntf.h`、
`impl/PadImpl.h` → `PadIntf.h`、`impl/DebugImpl.h` → `DebugIntf.h`、
`impl/ClipboardImpl.h` → `ClipboardIntf.h`。

**搬目录不消这条边**，拆法（`tTJSNI_*` 声明留 intf，`CreateNew` 走注入口）
与 `sound/README.md`、`DECISIONS.md` #9 是同一个模板，需要编译器。

## 例外（登记过，别顺手改）

| 文件 | 为什么不迁 / 不动 |
|---|---|
| `c_api/md5.h` + `md5.c` | 第三方 C API；`AndroidUtils.cpp` / `PluginImpl.cpp` 直接 include `md5.h` |
| `c_api/iconv/` | 生成表 / 兼容适配头，无 C++ |
| `c_api/encoding/*.c` | 从字符集数据生成的转换表 |
| `misc/MathAlgorithms*` | 与 `sound/common/MathAlgorithms.h` 内容重复；**定义只有 utils 这一份**（sound 侧的 `.cpp` 不在构建里） |
| `misc/Debugger.h` | 没有任何文件 include 它（死头，清理属于独立一轮） |
| `intf/TickCount.h` | 只有声明，实现已经不在本模块 |

## 加一个新文件放哪

1. C 接口或生成的表 → `c_api/`
2. 纯 C++ 工具，只 include STL / `tjs2` → `misc/`
3. 要 `tjsNative.h`、要注册 TJS 类 → `intf/`（如果是纯契约头，实现照样放 `impl/`）
4. 有平台分支或碰系统能力（线程、定时器、剪贴板、手柄）→ `impl/`
5. **不要**再建 `win32/` 这种名字：先问"它在 Android 上也编译吗"

## 验收（有编译器之后）

```bash
scripts/check_tu.sh --module utils
scripts/check_cmake_graph.sh utils
llvm-nm -C --defined-only libengine_api.so | grep 'krkr::utils::'
```

注意两个已知的行为面：`TVPTimer`（定时器分辨率依赖 `TVPGetRoughTickCount32`）、
`TVPAddLog` 的日志级别过滤（`DebugIntf`）。搬文件不改任何一条；
`core_utils_c_api` 是唯一一个只有 `.c` 源的簇，所以根 `project()` 必须开 `C` 语言
（已经是这样，`check_cmake_graph.sh` 也补了这一条）。
