# core/environ —— 四层，契约头单独一层

`environ` 是启动路径：`Application` 对象、启动序列、配置、平台契约、系统服务。
它曾经是一个 34 文件的扁平目录 + 一个带 `win32/` `android/` `sdl/` `stubs/`
五个子目录的 STATIC target，"哪些是契约、哪些是平台实现、哪些是启动流程"只能靠
读文件回答。现在按依赖层次切成四个目录：

| 目录 | target | 放什么 | 对外依赖 |
|---|---|---|---|
| `intf/` | `core_environ_intf`（INTERFACE） | **契约头**：`Application.h` `SystemControl.h` `SysInitIntf.h` `SysInitImpl.h` `DetectCPU.h` `EngineBootstrap.h` `EngineLoop.h` `MainScene.h` `XP3ArchiveRepack.h` `TVPWindow.h` `EmergencyExit.h` | 只依赖 `tjs2`（+ `utils` 的 `TVPTimer.h`） |
| `sysinit/` | `core_environ_sysinit` | 启动序列、配置、CPU 检测、诊断上报：`SysInitIntf.cpp` `SysInitImpl.cpp` `DetectCPU.cpp` `ConfigManager/*` `DumpSend.cpp` | `intf` + base + visual + io |
| `platform/` | `core_environ_platform` | 平台实现：`android/`（JNI 桥）`sdl/`（`tvpsdl.cpp`）`stubs/`（Linux / UI 桩）+ `SystemControl.cpp` | `intf` + `sysinit` + visual + base |
| `app/` | `core_environ_app` | 顶层：`Application` 与启动流程 `Application.cpp` `EngineBootstrap.cpp` `EngineLoop.cpp` `MainScene.cpp` `XP3ArchiveRepack.cpp` | 上面全部 |

`core_environ_module` 是一个 **INTERFACE 聚合层**，把四层串起来。`base` / `visual` /
`movie` / `sound` / `plugins` 里写的 `core_environ_module` 一个字都没改。

## 为什么契约头能单独成层

因为**实测它们真的是叶子**。被外部 include 次数最多的几个头，自己只 include
`tjsString` / `tjsTypes` / `TVPTimer`：

| 头 | 外部 include 次数 | 自己 include 什么 |
|---|---:|---|
| `SysInitIntf.h` | 38 | 无 |
| `Application.h` | 20 | `tjsVariant.h` `tjsString.h` |
| `SysInitImpl.h` | 6 | `SysInitIntf.h`（同层） |
| `SystemControl.h` | 6 | `TVPTimer.h`（utils） |
| `DetectCPU.h` | 4 | `tjsTypes.h` |
| `TVPWindow.h` | 1 | `tvpinputdefs.h` / `SystemImpl.h` / `ImeControl.h` |

而所有 `.cpp` 都在往上长：`SysInitImpl.cpp` 要 `GraphicsLoaderIntf`/`LayerIntf`
（visual）+ `Application.h`/`SystemControl.h`；`Application.cpp` 要
`ScriptMgnIntf`（base script）+ `PluginImpl`（plugin）+ `FontImpl`（visual）；
`EngineLoop.cpp` 要 `RenderManager` + `WindowImpl` + `tvpinputdefs`。

这就是 base README 那条规矩在模块尺度上的用法：**头放在它服务的那一层，实现放在
需要上层的那一层。** 结果是 `intf/` 里没有任何 `.cpp`，`sysinit/` 里只剩实现。

## 平台目录：为什么 `win32/` 散了

老目录 `win32/` 里有四个文件，性质完全不同：

| 文件 | 现在的去处 | 理由 |
|---|---|---|
| `SystemControl.cpp` | `platform/` | 音量/亮度/剪贴板/文件选择/CPU clock，**Android 上也编译**，只是历史上放在 win32/ |
| `SystemControl.h` | `intf/` | 被 `Application.cpp`、`SysInitImpl.cpp` 等 6 处 include，是契约 |
| `TVPWindow.h` | `intf/` | 被 visual 的 `WindowImpl` 用，是契约 |
| `EmergencyExit.h` | `intf/` | 纯宏，无依赖 |

`android/`、`sdl/`、`stubs/` 整体搬进 `platform/` 并保留子目录名（`platform/android`
这种写法在 README 和注释里更好引用，也保住了 `#include "../../utils/..."` 这种
相对路径的深度）。

## 例外：`ConfigManager/` 不拆

`ConfigManager/{Global,Individual,Locale}ConfigManager.{h,cpp}` 整簇留在 `sysinit/`。
它的头靠 `#include "ConfigManager/LocaleConfigManager.h"` 这条**带子目录的裸路径**
被 10 处调用点引用；把 `.h` 和 `.cpp` 拆到两个目录只会制造两个同名目录，
换不来任何可读性。和 base 的 `KAGParser` 一样，属于登记过的例外。

## 已知的环（不在本轮解决）

- **`base ↔ environ`**（12 / 56）：`base` 侧的 `Application.h`(7) 与
  `SystemControl.h`(5)。解法是抽 `iTVPApplication`（7 虚函数）+ `iTVPSystemControl`，
  见 `docs/HANDOVER.md` §7 路 A。抽完这两条边就只剩 `script ↔ environ` 一条。
- **`environ ↔ visual`**（28 / 11）：environ → visual 是合法的（启动要渲染、
  要加载画面）；要处理的是 **visual 侧那 11 处**用了 environ 的 11 个不同头。
- **`environ ↔ plugin`**：`Application.cpp` 要 `PluginImpl.h`
  （`tvpLoadPlugins` / `TVPRegisteredPlugins`），plugin 的 loader 又要
  `SysInitImpl.h`。这一对的拆法是"插件注册表反转"，和 TJS 类注册表反转
  （`DECISIONS.md` #9）是同一件事，必须一起做。
- **`script ↔ environ`**：`SysInitIntf.cpp` 要跑脚本（`ScriptMgnIntf` / `KAGParser`），
  而 `ScriptMgnIntf.cpp` 又要 `Application.h`。

`environ` **内部**是 DAG（`intf → sysinit → platform → app`，没有反向 include），
四组环全部跨模块。

## 加一个新文件放哪

1. 只是声明、被别的模块 include → `intf/`
2. 属于启动序列 / 配置 / CPU 检测 / 诊断 → `sysinit/`
3. 有平台分支（JNI、SDL、Linux 桩）或直接碰系统能力（音量、剪贴板、时钟）→ `platform/`
4. 是 `Application` / 启动流程 / 主循环的一部分 → `app/`
5. **不要**再建 `win32/` 这种名字：先问"它在 Android 上也编译吗"

## 技术债（本轮没动）

- **公共头仍是扁平面**：`#include "Application.h"` 这种裸文件名靠六条 `-I` 解析。
  `bridge/engine_api/src/engine_api_internal.h` 用的是显式路径
  （`environ/intf/Application.h`），改目录时必须一起改 —— 本轮已同步。
- **`bridge/engine_api` 的 include 面收窄了**：它原本把 `cpp/core/environ` 当
  include 根来吃 `android/KrkrJniHelper.h`，现在改为 `cpp/core` +
  `platform/android/...`。
- **`$<BOOL:${MACOS}>` / `$<BOOL:${IOS}>` 指向的 `apple/` 目录在本仓库不存在**：
  历史上是从上游 TVP 带过来的生成表达式，Android/Linux 构建永远求值为空。
  没有编译验证前原样保留（删它属于"清理死代码"的独立一轮）。
- **`SystemControl.cpp` 是 `platform/` 里最脏的文件**：它要 `Application`、
  `SysInitImpl`、`XP3Archive`、`RenderManager`、`LayerIntf`，是
  `environ ↔ visual` 的主要来源之一。

## 验收（有编译器之后）

```bash
scripts/check_tu.sh --module environ
scripts/check_cmake_graph.sh environ
bash scripts/check_static.sh          # 含 check_jni_symbols.py（已同步 AndroidUtils 新路径）
```

行为对齐点：启动序列（`TVPInitialize` 一族）的调用顺序、`Application` 的
`Terminate`/`ProcessMessages` 时序、配置文件读取与 locale 选择、
CPU 检测结果（`TVPGetCPUType`）、JNI 桥的符号（`check_jni_symbols.py`）。
搬迁不改变任何一条，出现差异说明是 include 路径问题。
