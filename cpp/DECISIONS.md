# DECISIONS

> 决策与踩坑日志，只追加，不参与日常上下文。可用 grep 检索。条目按文件顺序编号（第一个 `##` 是 #1）。

## 2026-11-14 — environ 硬骨头：EngineLoop + MainScene 必须同一个 cluster

\## 卡点

`TVPSetPostUpdateEvent` / `TVPDrawSceneOnce` / `TVPGetKeyMouseAsyncState` /
`TVPGetJoyPadAsyncState` / `TVPResetDrawSceneOnceTimingForRestart` 的**定义**在
`cpp/core/environ/EngineLoop.cpp`，**声明**在 `cpp/core/environ/MainScene.h`。

\## 为什么不能只搬 MainScene.h

声明还散落在 4 处全局作用域（AGENTS.md §9.1 的 using/extern 冲突）：

| 文件 | 符号 |
|---|---|
| `cpp/core/base/impl/SystemImpl.cpp:49` | `bool TVPGetKeyMouseAsyncState(tjs_uint, bool);` |
| `cpp/core/environ/stubs/ui_stubs.cpp:47` | `int TVPDrawSceneOnce(int interval);` |
| `cpp/core/visual/impl/DInputMgn.h:184` | `bool TVPGetJoyPadAsyncState(tjs_uint, bool);` |
| `cpp/core/visual/ogl/RenderManager_ogl.cpp:2699` | `void TVPSetPostUpdateEvent(void (*f)());` |

只要 `MainScene.h` 加了 `using krkr::environ::X;`，这四处就会报
`conflicts with a previous declaration`。

\## 额外坑：重复定义

`TVPGetJoyPadAsyncState` 有两份定义：
- `cpp/core/environ/EngineLoop.cpp:59`
- `cpp/core/visual/impl/DInputMgn.cpp:752`

目前靠 `--allow-multiple-definition` 活着。搬 namespace 会让两份变成**不同符号**
（一份 `krkr::environ::`、一份全局），需要显式决定留哪份 / 删哪份。

\## 执行顺序（建议）

1. 先删掉 4 处散落前置声明，改为 `#include "environ/MainScene.h"`；
   或者把它们改成 `namespace krkr::environ { extern ... }` 限定声明。
2. 决定 `TVPGetJoyPadAsyncState` 的重复定义留哪一份（涉及 base ↔ visual 依赖方向）。
3. 再一起把 `MainScene.h/.cpp` + `EngineLoop.h/.cpp` 包进 `krkr::environ`。

\## 为什么跨越 base/visual

这一步必然同时改 `base`、`visual`、`environ` 三个模块，与"一个 commit 只做一个模块"
的约定冲突。要么接受一个跨模块 commit 并单独标注，要么先做一次只改声明归属的
准备 commit（不搬 namespace），再做正式的 namespace commit。

备注：直连的 `TVPDrawSceneOnce` 在 `stubs/ui_stubs.cpp` 里也有 Linux 宿主侧的实现分支，
改之前要确认 `#ifdef` 分区。

## 2026-11-14 — plugin 下一层：NCB 模板层必须在有编译器时做

\## 已完成（可动的前提）

- `2e35fc9` 注册器层：`TVPRegisteredPlugins`、`ncbAutoRegister`、
  `ncbNativeClassAutoRegister`、`ncbModuleAliasAutoRegister`、
  `ncbAttachTJS2ClassAutoRegister`、`ncbRequireClassAutoRegister` → `krkr::plugin`
- `6017d23` `MethodCaller` + `method_cast`（`ncb_invoke.hpp`）→ `krkr::plugin`

两层都没有向插件作用域展开显式特化，所以只靠底部全局 `using` 就够。

\## 剩余：类型转换/注册模板层

要搬的（`ncbind.hpp` 2000 行）：`ncbTypedefs`、`ncbClassInfo`、`ncbInstanceAdaptor`、
`ncbTypeConvertor`（及其 `Conversion` / `SpecialMap` / `SelectConvertorType`）、
`ncbStrictResultConvertor`、`ncbNarrowCharConvertor`、`ncbWideCharConvertor`、
`ncbStringConvertor`、`ncbNativeObjectBoxing`、`ncbDispatchConvertor`、
`ncbPropAccessor`、`ncbArrayAccessor`、`ncbDictionaryAccessor`、`ncbIMethodObject`、
`ncbNativeClassMethodBase`、`ncbNativeClassMethod/Constructor/Factory/Property`、
`ncbRawCallbackMethod`、`ncbRawCallbackProperty(Selector)`、`ncbSubClassItem`、
`ncbRegistClass`、`ncbRegistNativeClass(Base)`、`ncbRegistSubClass`、
`ncbAttachTJS2Class`、`ncbSubClassCheck`、`ncbNativeFunctionAutoRegister(Templ)`、
`ncbCallbackAutoRegister`、`ncbFunctionTag_*`、`ncbNativeClassMethodBase::nativeInstanceGetter`。

\## 为什么不能像前两层那样只加 using

`NCB_*` 宏会在**插件 .cpp 的全局作用域**展开显式特化/显式实例化。模板一旦进了
`krkr::plugin`，未限定名的 `template <> struct X<cls>` 就违反
[temp.expl.spec]（必须写在模板所在命名空间或其外层命名空间——且要用限定名）。

必须逐个改宏体为限定名，涉及这些宏：

| 宏 | 展开出的特化/实例化 |
|---|---|
| `NCB_REGISTER_CLASS_COMMON` (2174) | `template struct ncbClassInfo<cls>`；`template <> void ncbRegistClass<nt<cls>>::Regist()` |
| `NCB_REGISTER_SUBCLASS_DELAY` (2188) | `template <> struct ncbSubClassCheck<cls>`；`template struct ncbClassInfo<cls>`；`template <> void ncbRegistClass<ncbRegistSubClass<cls>>::Regist()` |
| `NCB_REQUIRE_CLASS` / `_SUBCLASS` (2273/2274) | `template struct ncbClassInfo<cls>` |
| `NCB_GET_INSTANCE_HOOK` (2252) / `NCB_ATTACH_CLASS_WITH_HOOK` (2266) | `template <> struct ncbNativeClassMethodBase::nativeInstanceGetter<cls>` |
| `NCB_REGISTER_FUNCTION_COMMON` (2422) | `template <> struct ncbNativeFunctionAutoRegisterTempl<ncbFunctionTag_##tag>` |
| `NCB_TYPECONV_BOXING` (552) → `NCB_SET_CONVERTOR` 链 | `template <> struct ncbTypeConvertor::SelectConvertorType<...>` |
| `NCB_TYPECONV_MAPSET` (373) | `template <> struct ncbTypeConvertor::SpecialMap<...>` |
| `NCB_INNER_CONVERSION_SPECIALIZATION` (258) | `template <> struct ncbTypeConvertor::Conversion<void, void>` |

\## 还需要

- 约 30 个全局 `using`：插件直接写名字的有 `ncbInstanceAdaptor`(70)、
  `ncbPropAccessor`(53)、`ncbNativeClassMethodBase`(27)、`ncbTypedefs`(26)、
  `ncbTypeConvertor`(23)、`ncbDictionaryAccessor`(20)、`ncbClassInfo`(20)、
  `ncbRawCallbackMethod`(12)、`ncbRegistClass`(8)、`ncbRegistNativeClass`(7)、
  `ncbArrayAccessor`(7)、`ncbRegistNativeClassBase`(6)、`ncbNativeClassFactory`、
  `ncbNativeClassConstructor`、`ncbIMethodObject`、`ncbSubClassItem`、
  `ncbRegistSubClass`、`ncbSubClassCheck`、`ncbNativeObjectBoxing`、
  `ncbNativeClassProperty`、`ncbRawCallbackProperty(Selector)`、`ncbToValueConvertor`、
  `ncbStringConvertor`、`ncbStrictResultConvertor`、`ncbNarrowCharConvertor`、
  `ncbWideCharConvertor`、`ncbToVariantConvertor`、`ncbVariatToNChar`、
  `ncbDispatchConvertor`、`ncbAttachTJS2Class` …
- `ncbNativeFunctionAutoRegisterTempl` 目前是全局类；搬走后
  `NCB_REGISTER_FUNCTION_COMMON` 里的特化也要限定名。

\## 验收方式

这一层**必须**在有编译环境时做：`./build.sh debug --engine-only` +
至少一个含 `NCB_REGISTER_CLASS` / `NCB_ATTACH_CLASS` / `NCB_ATTACH_FUNCTION` 的插件
能编过。建议一次只改一个宏族（先 `NCB_REGISTER_CLASS_COMMON`，再 `TYPECONV` 族，
最后 `FUNCTION` 族），每族一个 commit，出问题好回滚。

## 2026-11-14 — 硬骨头已拆（f3c64ac）：EngineLoop + MainScene 进 krkr::environ

\## 结论

`f3c64ac` 完成。`EngineLoop` / `TVPMainScene` / `EngineInputEvent(Type)` /
MainScene.h 的 5 个循环控制函数全部进 `krkr::environ`。

\## 更正先前记录的一个错误判断

之前说 `TVPGetJoyPadAsyncState` 有两份定义（`EngineLoop.cpp:59` 与
`DInputMgn.cpp:752`）、靠 `--allow-multiple-definition` 活着——**这个判断是错的**。

`DInputMgn.cpp:752` 那份在 `#if 0` 里：
- 243 行 `#if 0`
- 671 行有一个 `#endif`，但它落在 666–672 的 `/* ... */` 块注释内部，
  翻译阶段先剥注释，所以它不是指令
- 真正配对的是 757 行的 `#endif`

即 243–757 整段都不编译，包含 752 的定义。实际只有 EngineLoop.cpp 一份。

\## 5 处散落声明的最终处理

不引入新的 include 边，一律改成"命名空间限定声明 + 局部 using"：

| 文件 | 处理 |
|---|---|
| `base/impl/SystemImpl.cpp` | `namespace krkr::environ { TVPGetKeyMouseAsyncState(...); }` + using（另加 `TVPGetJoyPadAsyncState` 的 using，声明来自 DInputMgn.h） |
| `environ/stubs/ui_stubs.cpp` | `TVPDrawSceneOnce` 同上 |
| `visual/impl/DInputMgn.h` | `TVPGetJoyPadAsyncState` 同上（头文件里不加 using） |
| `visual/ogl/RenderManager_ogl.cpp` | `TVPSetPostUpdateEvent` 同上 |
| `bridge/engine_api_internal.h` | **删掉**重复声明——同文件第 56 行已 include `environ/MainScene.h`，using 由它提供 |

注意：bridge 里 `::TVPDrawSceneOnce(0)` 这种全局限定调用，经 using 声明后
`::X` 仍能查到（qualified lookup 会找到 namespace 里的 using-declaration）。

\## 教训

`#if 0` 里的 `#endif` 可能落在块注释内，靠 grep `^#endif` 数配对会数错。
判断某段代码是否编译，两边都要看：既要看指令，也要看注释边界。

## 2026-11-14 — environ 停在 28/37：剩下 9 个文件的分类和处理理由

`core/environ` 停在 28/37。剩下的 9 个不是"没干完"，是**分三类有意留下**：

\## A. 搬了没收益（建议永久例外）

| 文件 | 理由 |
|---|---|
| `sdl/tvpsdl.cpp` | 50 行，**一句定义都没有**（只有 include + 一个 static bool）。空 TU |
| `win32/EmergencyExit.h` | `TVPCPUClock` / `TVPCPUClockAccuracy` 全仓库**没有定义**，是死代码 |
| `android/AndroidUtils.h` | 只有 `TVPGetDeviceID()`，除 AndroidUtils.cpp 自己外无调用者 |

\## B. 跨模块契约，必须和别的一起做

| 文件 | 卡在哪 |
|---|---|
| `Platform.h` | 25 个包含者。里面 24 个函数由 `android/AndroidUtils.cpp`、`stubs/platform_linux.cpp`、`Application.cpp` **以及 `core/io/IoStorageLocal.cpp`** 分别实现。搬它等于同时改 io（`TVP_stat`/`TVP_utime`/`TVPWriteDataToFile` 都在 io 里），应按"平台契约"单独一轮，和 io 的遗留项一起 |
| `win32/TVPWindow.h` | 482 行，含 3 个匿名/命名 enum（枚举值要逐个 using）+ `tTVPWindow` + `iWindowLayer`。被 `visual/impl/WindowImpl.h` 依赖，必须和 visual 的 Window 体系一起做 |

\## C. 类型/宏头（建议永久例外）

`combase.h`、`typedefine.h`、`vkdefine.h`、`cpu_types.h` —— 全是 typedef / enum / #define，
被大量文件包含但本身没有"属于哪个模块"的语义。搬它们只是把 #include 的读者换个前缀，
收益接近 0，风险是枚举值/宏作用域。

\## 结论

environ 的**代码层**已经收完。剩下的都是契约层或类型层，应该按"契约"而不是"模块"来切。

## 2026-11-14 — KAGParser 登记为例外：它属于 TJS 边界，不迁 krkr::base

\## 决定：登记为例外，不迁 namespace

`KAGParser.h` / `KAGParser.cpp` 保持现状（`KAGParser.cpp` 整体在 `namespace TJS`，
`KAGParser.h` 第 28 行还有 `using namespace TJS;`）。

\## 理由

1. **它本质是 TJS 类的实现，不是 base 服务。** `tTJSNI_KAGParser` 是
   `tTJSNativeInstance` 派生，`TVPCreateNativeClass_KAGParser()` 返回
   `iTJSDispatch2*`——和 `tTJSNC_Scripts` / `tTJSNC_System` 不同，后两者的宿主侧
   状态（脚本引擎、System 属性）确实是 base 的职责，而 KAGParser 的状态全在
   TJS 对象里。
2. **`.cpp` 已经整体在 `TJS` 命名空间（3264 行）。** 要搬进 `krkr::base` 就得
   从 `TJS` 里把它拆出来，等于重写这个文件的命名空间结构，违背"小步、行为零变化"。
3. **header 有 `using namespace TJS;`。** 这是 AGENTS §2.3 明令禁止的写法，但它是
   既有的、和 tjs2 边界没切开的证据。硬搬会把这个 using 带进 `krkr::base`，
   污染整个 base 命名空间。
4. `TJS` 命名空间本身不在迁移范围（AGENTS §2.1）。KAGParser 深度绑定它，
   强行纳入只会制造一个"半个 TJS 类在 base 里"的中间态。

\## 代价

`KAGParser.h` 的 3 个符号仍是全局：`tTJSNI_KAGParser`（及其 2 个辅助类
`tTVPCharHolder` / `tTVPScenarioCacheItem`）、`TVPCreateNativeClass_KAGParser()`、
`TVPClearScnearioCache()`、`TVPRegisterCompiledScenarioLabelResolver()`。
其中 `TVPCreateNativeClass_KAGParser` 被 `plugins/KAGParser/` 和兼容层调用。

\## 如果以后要收拾它

前提是先把 tjs2 的 `TJS` 命名空间边界定下来（或者把 KAGParser 的 TJS 依赖收进
一个薄适配层）。那是独立的设计任务，不应混在 namespace 迁移里。

## 2026-11-14 — 阶段计划：base 收口 → 文件结构重设计 → visual/movie/plugins 日后做

\## 决定的顺序

base 收口（本阶段）→ **文件结构重设计**（下一步，独立任务）→ visual / movie / plugins（日后再做）。

\## 为什么这个顺序

1. **base 是其他一切的前置。** `MsgIntf.h`（87 个包含者）、`EventIntf.h`（50）、
   `ScriptMgnIntf.h`（25）是所有模块的公共依赖。base 没定，visual/movie 的
   命名空间落点就没法定（它们大量 include base 头）。
2. **文件结构重设计和 namespace 迁移是两个正交任务，不要混。**
   前者动目录/文件名/CMake target，后者动符号归属。混在一起时
   `git diff -w` 的"只有 namespace/using/include"这条验收标准就失效了。
3. **visual / movie / plugins 的体量决定了它们必须单独成阶段。**
   visual 104k 行 / 157 文件、movie 22k / 86、plugins 100k / 302（含 cubism
   第三方 179 文件 / 44k 行）。而且 visual 现在有 4 种命名落点并存
   （`krkr2` / `krkr` / `krkr::font` / `krkr::alphamovie`），**必须先定契约**，
   否则每搬一批都在加深不一致。

\## visual / movie / plugins 阶段的前置条件（日后再做时必须先满足）

### visual
- 决定 `simd/` 的 21 处 `namespace krkr2` 往哪收：`krkr::visual::simd`？
- 决定 `krkr::font` / `krkr::alphamovie` 保留还是并入 `krkr::visual`。
- 决定 `ogl/` 是否按目标地图用 `krkr::visual::gl`。
- 决定 11 处裸 `namespace krkr` 的归属。
- 顺序建议：`simd/`（自包含）→ `gl/` → `impl/` → 根目录巨型文件
  （`LayerIntf.cpp` 12864 行、`tvpgl.cpp` 14139 行、`RenderManager_ogl.cpp` 5278 行）。

### movie
- `motionplayer/` 与 `ffmpeg/` 是两个独立世界，分开做。
- `TVPMainThreadID` 在 movie 的 3 个文件里有各自的全局 `extern` 声明，
  要搬到 `krkr::environ` 必须先处理这 3 处。

### plugins
- **前置：先定插件契约**——插件能不能 include 核心头、能调哪些符号。
- `cubism/`（179 文件 / 44k 行）是第三方 SDK，**不迁**。
- 目标是 `krkr::ext::xxx`，不是把 100k 行都塞进 `krkr::plugin`。

\## 不在本阶段范围的

- 390 条 `using krkr::` 桥接的清理（要等调用点迁移，属独立任务）。
- `tjs2` 的 `TJS` 命名空间（设计上排除）。
- `KAGParser`（`DECISIONS.md` #5）。

## 2026-11-14 — MsgIntf 扫描结论：消息目录是生成文件，永久留全局

\## 扫描结论

`MsgIntf` 那一族文件按"谁生成"分成两半，只有手写那半该迁。

| 文件 | 行数 | 内容 | 判定 |
|---|---:|---|---|
| `MsgIntf.h` | 86 | 手写：宏 `TVP_MSG_DECL*`、`WIDEN`、15 个工具函数/版本变量 | ✅ 已迁 |
| `MsgIntf.cpp` | 203 | 覆盖 `TVP_MSG_DECL*` 生成 **343 个消息定义** + 函数体 | ✅ 函数体已迁，消息定义留全局 |
| `MsgIntfInc.h` | 229 | **生成文件**（`gentext.pl Messages.xlsx`），151 条 `TVP_MSG_DEFINE` | ⛔ 例外 |
| `impl/MsgImpl.h` | 398 | **生成文件**，192 条 `TVP_MSG_DEFINE` | ⛔ 例外 |

\## 为什么不迁消息目录

1. **两个文件都是生成文件。** 首行就写着 `// generated from gentext.pl Messages.xlsx`。
   手改 namespace 会在下次重新生成时被覆盖。生成器不在仓库里（只有产物）。
2. **343 个符号。** 全部搬进 `krkr::base` 就要 343 条全局 `using`（每个消息平均被
   2 个文件引用）。收益/噪音比极差。
3. 它们是**数据目录**，不是"属于某个模块的 API"——和 `cpu_types.h` 的枚举、
   `tar.h` 的结构体同类。

\## 宏覆盖的机关（迁移时必须理解）

`MsgIntf.cpp` 顶部先把 `TVP_MSG_DECL` / `TVP_MSG_DECL_CONST` / `TVP_MSG_DECL_NULL`
重定义成**定义**形式，再 `#include "MsgIntf.h"`；`MsgIntf.h` 里又是
`#ifndef TVP_MSG_DECL` 的兜底，然后 include `MsgImpl.h` 与 `MsgIntfInc.h`。
所以 343 个消息符号的定义都"长"在那一个 `#include` 的位置。

→ **namespace 必须开在 `#include "MsgIntf.h"` 之后**，否则消息定义会被包进
namespace，与全局声明撕裂。这是本次唯一需要小心的地方。

\## 顺带发现的两个潜在坑（暂不处理）

- `MsgImpl.h` 的兜底宏 `#define TVP_MSG_DECL_CONST(name)` 只有 1 个形参，
  而调用方传 2 个实参。当前永远不会触发（`MsgIntf.h` 总是先定义 2 参版本），
  但一旦有人单独 include `MsgImpl.h` 且没先 include `MsgIntf.h`，会直接预处理报错。
- `MsgImpl.h` 与 `MsgIntf.h` 是循环 include，靠 include guard + `#ifndef TVP_MSG_DECL`
  兜底才成立。生成器若改了顺序会炸。

## 2026-11-14 — 文件结构重设计：5 条建议 + 实测依赖图

\## 实测依赖图（core/ 下 12 模块，头引用次数）

```
utils    -> tjs2(30) base(16) environ(2)
base     -> tjs2(50) utils(27) visual(26) environ(23) sound(4) plugin(2) extension(1) io(1)
io       -> base(27) tjs2(11) utils(8) environ(4) visual(1)
compat   -> io(3) tjs2(2) base(1)
environ  -> base(42) visual(28) utils(23) tjs2(19) plugin(2) sound(2) io(2)
extension-> tjs2(2)
plugin   -> tjs2(10) base(9) utils(4) sound(2) compat(1) io(1) visual(1) environ(1)
movie    -> visual(9) utils(9) base(5) sound(3) environ(3) tjs2(2)
sound    -> tjs2(29) base(28) utils(15) environ(6) movie(1)
visual   -> base(127) tjs2(121) utils(44) environ(32) sound(2) plugin(2) movie(1)
```

**14 组双向依赖**，几乎完全图。注意 `tjsCommHead.h`（54 行，164 个包含者，
内容只是 `tjs.h` + 几个 std 头）严重虚高 `→ tjs2` 的数字，
所以真实边要把它剔除后再算。

\## 目标分层（由 namespace 地图隐含）

```
tjs2 → utils → base → {io, sound, movie, visual} → environ → plugin / compat
```

违规边 11 条；其中 **10 条是"廉价边"（1–4 次引用）**，4 条是结构边（20+）。

\## 5 个建议

### A. 先拆环，再改目录

目录是结果不是手段。现成的 14 组环不切断，新目录只会原样复刻这张网。
切环手段用**依赖反转**（注册回调），`IoPolicy` / `ModuleGate` 已经是这个模式。

### B. 目录只放三层，靠 CMake 强制

```
core/<module>/{include, src, platform/{android,linux,windows}}
```

- `include/` 进 `target_include_directories(PUBLIC)`
- `src/`、`platform/` 只进 PRIVATE
- **内部头不在公共 include path 上** → 谁 include 内部头就是编译错误

顺带修掉三个误导性目录：`core/sound/win32/`、`core/utils/win32/`、
`core/environ/win32/` 在 Android 上全部参与编译，应改名 `platform/windows`
并补 `platform/posix`。

### C. 子模块 = CMake target，不是目录

`utils/string/`、`utils/thread/` 如果还是同一个 `core_utils_module`，
只有心理收益（编译时间不变、依赖检查不变）。要么一个子目录一个 target，
要么不切。

### D. 不做"单一总头"，做"每模块门面头 + 显式子头"

反例就在仓库里：`tjsCommHead.h` 被 164 个文件包含，作用只是让所有人依赖 tjs2。
单头的四个反作用：编译依赖全连通、god header 化、隐藏真实使用面、
挡住前置声明（`MsgIntf.h` 87 个包含者里大部分只要一个 throw 声明）。

替代：

```
core/utils/include/krkr/utils.h          ← 门面：只放跨模块必需 + 尽量前置声明
core/utils/include/krkr/utils/string.h   ← 子模块头，显式 include
```

三条规则：门面头不得 include `src/` 实现头；门面头的 include 数单调不增（CI ratchet）；
跨模块只能 include 门面头。

### E. 用 CI 把结构锁住

- 每子模块一个 target
- include-graph 扫描脚本进 CI，层间反向 include 直接 fail
- 去掉 `--allow-multiple-definition`（先变 warning 清单）

\## 对"utils 再细分"的具体结论

utils 45 个文件里，**7 个不是叶子**：
`DebugIntf.cpp`、`TickCount.cpp`、`TimerIntf.h/.cpp`、
`win32/{ThreadImpl,TimerImpl,ClipboardImpl}.cpp`
—— 它们需要 `MsgIntf`(4) / `SysInitIntf`(4) / `EventIntf`(3) / `SystemControl`(1)。

**它们本质是"引擎服务"，不是工具。** 所以正确的切法是**按依赖层切，不是按主题切**：

- `utils`（叶子，只依赖 tjs2）：StringUtil / FilePathUtil / ObjectList / Random /
  RealFFT / md5 / MathAlgorithms / Exception / MiscUtility / encoding / VelocityTracker
- 线程 / 定时器 / Pad / 剪贴板 / 调试输出 → 归 `base`（或新建 `core/runtime`）

\## 执行顺序

1. 剔除 `tjsCommHead.h` 后重算依赖图（纯测量，不改代码）
2. 切 10 条廉价违规边（依赖反转）
3. `base` 拆成 3–4 个子 target（它依赖 7 个其他模块，是最大违规者）
4. 再动目录结构（`include/src/platform` + 门面头）
5. CI ratchet + 去 `--allow-multiple-definition`

## 2026-11-14 — 拆环：io→visual 已切；base 注册表需真反转且需编译器

\## 拆环第一刀（已完成）

`c60623c`：`core/io/IoStorageLocal.cpp` 删掉 `#include "WindowImpl.h"`。
`TVPSelectFile` 的声明在 `FileSelector.h`（base），那个头本来就已经在该文件里。
`core/io → core/visual` 从 1 条降到 **0 条**。同时修掉 `AGENTS.md §9.7` 记了很久的边界泄漏。

\## 最大的一块：base 的 TJS 类注册表（未做，需要编译器）

`base → visual(26) / sound(4) / plugin(2) / extension(1)` 的**根因只有一个文件**：
`core/base/ScriptMgnIntf.cpp` 的 `TVPInitScriptEngine()` 里内联了一张 21 个类的注册表。

涉及符号（22 个工厂 + 3 个直接实例化）：

```
TVPCreateNativeClass_{Window,Layer,Font,Bitmap,BitmapLayerTreeOwner,ImageFunction,
  VideoOverlay,MenuItem,Rect,CDDASoundBuffer,MIDISoundBuffer,WaveSoundBuffer,
  Timer,Pad,Clipboard,Debug,Plugins,AsyncTrigger,System,Scripts,Storages,KAGParser}
tTJSNC_PhaseVocoder        ← new，需要完整定义（sound/PhaseVocoderFilter.h）
tTJSNC_BasicDrawDevice     ← new，需要完整定义（visual/BasicDrawDevice.h）
WindowMenuProperty         ← new，需要完整定义（visual/MenuItemIntf.h）
TVPCauseAtInstallExtensionClass(global)   ← extension/Extension.h
```

### 为什么"集中声明头"救不了

我本来想新建 `base/impl/NativeClassFactories.h` 只放 21 个工厂的原型，让 base 不再
include 各模块的头。但注册表里还有 **3 处 `new`**，那需要**完整类定义**，
光有声明编不过。所以必须做真正的反转。

### 正确的做法（需要编译验证）

把整块注册表从 `base/ScriptMgnIntf.cpp` 搬到 `environ`，base 只留一个注入口：

```cpp
// base：注册点
using TVPNativeClassInstaller = void (*)(iTJSDispatch2 *global);
void TVPSetNativeClassInstaller(TVPNativeClassInstaller f);
// TVPInitScriptEngine 里： if(auto f = ...) f(global);  替代原来那 60 行
```

```cpp
// environ/EngineNativeClasses.cpp：原样搬过来的那 60 行
void TVPInstallEngineNativeClasses(iTJSDispatch2 *global) { ... }
```

`environ → visual/sound/plugin/extension` 本来就是合法方向（environ 已有 28 条到 visual），
所以这是**合法方向的移动**，不是新增违规。

### 为什么必须等编译器

1. **注册顺序**决定 TJS 类的成员覆盖行为（`TJS_MEMBERENSURE`）。搬动时若顺序变了，
   同名类可能被注册两次或漏注册——这类 bug 只在运行时表现为脚本报错。
2. 注入口必须在 `TVPInitScriptEngine()` 之前设好。设晚了一步就是**所有 TJS 类消失**
   （引擎直接废掉，但是失败得很响）。
3. 60 行里夹着 `CreateShortCutKeyCodeTable()`、`TVPAddCompactEventHook()` 等副作用，
   要逐条确认归属。

**结论：这一块不在无编译器状态下做。** 但它一旦做完，base 的 33 条跨模块边
（visual 26 + sound 4 + plugin 2 + extension 1）一次性降到个位数。

\## 其余已确认可安全切的边

| 边 | 做法 | 风险 |
|---|---|---|
| `base → io`（IoPolicy.h x1） | 把 XP3 段缓存 override 的**状态**移到 base（`SysInitIntf.h`），io 只读它。方向从 base→io 翻成 io→base（已有 27 条） | 低，行为等价 |
| `io → environ`（Application.h） | `ExePath()` 就是 `return TVPNativeProjectDir;`，io 可直接用 base 的全局 | 低 |
| `plugin → compat`（ModuleGate.h） | 见下 | 中 |
| `utils → base/environ`（7 个文件） | 结构性问题：DebugIntf/TickCount/Timer/Thread/Clipboard 本质是引擎服务，应按 `DECISIONS.md` #8 的建议搬到 base | 大（动目录） |

## 2026-11-14 — 拆环结果：utils 变真叶子，双向依赖 14 → 10

\## 结果

`612ff56`。`utils` 现在是真叶子：**只依赖 tjs2**。
模块间双向依赖 **12 → 10 组**。

\## 做法

### 1) 7 个"不是工具的实现"搬到 `base/impl/`

```
utils/DebugIntf.cpp            -> base/impl/DebugIntf.cpp
utils/TickCount.cpp            -> base/impl/TickCount.cpp
utils/TimerIntf.cpp            -> base/impl/TimerIntf.cpp
utils/MiscUtility.cpp          -> base/impl/MiscUtility.cpp
utils/win32/TimerImpl.cpp      -> base/impl/TimerImpl.cpp
utils/win32/ThreadImpl.cpp     -> base/impl/ThreadImpl.cpp
utils/win32/ClipboardImpl.cpp  -> base/impl/ClipboardImpl.cpp
```

**只搬 .cpp，接口头留在 utils。** 这形成了"头在下层、实现在上层"的分居：
- 头在 `core/utils/`（DebugIntf.h / TickCount.h / TimerIntf.h / MiscUtility.h …）
  被 visual(21) / io(8) / sound(15) 广泛包含 → 必须留在叶子层
- 实现在 `core/base/impl/` → 可以自由用 base/environ 的消息、事件、初始化、Console

### 2) `tTVPAsyncTriggerMode` 下移到 `utils/AsyncTriggerMode.h`

被 utils 的 Timer 和 base 的 AsyncTrigger 共用。留在 base 会让 utils 的**公共头**
`TimerIntf.h` 反向 include `base/EventIntf.h`（50 个包含者）。放下层后是 base → utils。

三个枚举值单独 using（AGENTS §9.3）。

\## 一个走错又退回的岔路

我一开始顺手把 `CharacterSet` 从 base 挪到 utils（"UTF-8 转换看起来是工具"）。
查完发现 `CharacterSet.cpp` 自己要用 base 的 `TVPThrowExceptionMessage`——
挪下去就制造一条新的 `utils → base` 边。**已回退**，`CharacterSet` 留在 base。

教训：判"是不是工具"不能看名字，要看它**依赖谁**。

\## 头依赖清零 ≠ 链接依赖清零

`utils/win32/TVPTimer.cpp` 用的 `TVPGetRoughTickCount32`，实现现在在
`base/impl/TickCount.cpp`，所以 `core_utils_module` 仍 PRIVATE 链接 `core_base_module`。
静态库下无害，等收 CMake 依赖图时再处理（那时也可以顺手去掉
`utils -> core_environ_module`）。

\## 现在的 10 组环

```
base <-> visual        26 / 126   ← class registry 真反转（需编译器）
base <-> environ       25 / 42    ← iTVPApplication 接口
base <-> sound          4 / 25    ← class registry 的一部分
base <-> plugin         2 / 9
io <-> environ          3 / 2     ← Platform.h 契约
environ <-> plugin      2 / 1
environ <-> sound       2 / 6
environ <-> visual     28 / 32
movie <-> sound         3 / 1     ← sound 侧 1 文件（TVPInitLibAVCodec）
movie <-> visual        9 / 1     ← visual 侧 1 文件（DeferredDestroyer）
```

## 2026-11-14 — 拆环收尾：双向依赖 14 → 5，剩余卡在注册表与接口抽取

\## 总结果

模块间双向依赖 **14 组 → 5 组**，8 个提交。

```
utils   -> tjs2                                      ✅ 叶子
io      -> base, tjs2, utils                         ✅ 干净三层
movie   -> visual, sound, base, utils, tjs2, environ  ✅ 干净顶层，无人反向依赖
extension -> tjs2
compat  -> io, tjs2, base
```

\## 剩余 5 组环与处理路径

| 环 | 权重 | 卡在哪 | 路径 |
|---|---|---|---|
| `base ↔ visual` | 26 / 147 | TJS 类注册表：`base/ScriptMgnIntf.cpp` 里 21 个 `TVPCreateNativeClass_*` + 3 处 `new`（`tTJSNC_PhaseVocoder` / `tTJSNC_BasicDrawDevice` / `WindowMenuProperty`） | **必须等编译器**（`DECISIONS.md` #9）。做完三组环同时消失 |
| `base ↔ sound` | 4 / 29 | 同上（注册表里的 4 个 sound 工厂） | 同上 |
| `base ↔ plugin` | 2 / 9 | 同上（`TVPCreateNativeClass_Plugins`） | 同上 |
| `base ↔ environ` | 12 / 56 | `Application.h`(7) + `SystemControl.h`(5) | 抽 `iTVPApplication`（7 虚函数）+ `iTVPSystemControl` |
| `environ ↔ visual` | 28 / 11 | environ 需要 visual 的渲染/窗口（合法）；visual 侧 11 处用了 environ 的 11 个不同头 | 逐个判断能否下移或接口化 |

\## 已确认可下移但还没动的（按价值）

1. **`iTVPApplication`**（`base/impl/ApplicationIntf.h`）：`Terminate` /
   `ProcessMessages` / `PostUserMessage` / `FilterUserMessage` / `GetActivating` /
   `GetTitle` / `SetTitle`。同时解掉 `base → environ`(7) 和 `sound → environ`(1)。
2. **`iTVPSystemControl`**：解掉 `base → environ`(5) 和 `sound → environ`(1)。
   但 `SystemControl.cpp` 自身依赖 visual，所以只能抽接口，不能搬文件。
3. **`environ ↔ visual` 的 visual 侧 11 处**：逐个看，可能有的能像
   `Platform.h` 一样把纯声明下移。

\## 方法沉淀（本轮踩过的坑）

- **死 include 的两类误判**：
  ① 变量名不在符号表里（`Application->X`）→ 会误判成"没用"；
  ② 符号长在被 include 的文件里（`MsgIntfInc.h` 的消息、`Application.h` 的 <vector>）
  → 会误判成"没用"，删了会连带丢传递依赖。
- **判"属于哪一层"不能看名字，要看依赖谁**：`CharacterSet` 名字像工具，
  但实现要用 base 的 `TVPThrowExceptionMessage`，所以留在 base。
- **同名 include guard 的重复头必须一起处理**：`environ/win32/DetectCPU.h` 与
  `environ/DetectCPU.h` 同 guard，实现搬走后留着副本会导致链接失败。
- **头依赖清零 ≠ 链接依赖清零**：`sound → movie` 的位置声明只是消了头依赖。

## 2026-11-14 — base 拆 4 target + 4 目录（e1f80ea / 7edaf4e）：cluster 不能按名字切

\## 结论

`core_base_module` 拆成 `core_base_{core,runtime,script,sysinit}` 四个 STATIC，
文件搬进同名目录 `core/ runtime/ script/ sysinit/`，`impl/` 消失。
`core_base_module` 变成 INTERFACE 聚合层，调用方零改动。

\## 与 STRUCTURE_DESIGN §3.1 的偏差（重要）

§3.1 把 `EventIntf.cpp`、`impl/EventImpl.cpp`、`impl/NativeEventQueue.cpp`、
`impl/FileSelector.cpp` 划给了 `core`。**实测不成立**：

| 文件 | 实际要的头 | 结果 |
|---|---|---|
| `EventIntf.cpp` | `ScriptMgnIntf.h`(script) `SystemImpl.h`(script) `WindowIntf.h`(visual) | 只能进 runtime |
| `impl/EventImpl.cpp` | `Application.h`/`SystemControl.h`(environ) `WindowImpl.h`(visual) | 只能进 runtime |
| `impl/NativeEventQueue.cpp` | `Application.h`(environ) | 只能进 runtime |
| `impl/FileSelector.cpp` | `WindowImpl.h`/`TVPScreen.h`(visual) | 只能进 runtime |

硬按 §3.1 放，`core` 会同时依赖 script/visual/environ/plugin，变成四层里最脏的一层，
和"core 是干净叶子"的目标正好相反。

\## 期间的另一个发现

`impl/MsgImpl.cpp` include 了 `PluginImpl.h` 但**一个符号都没用**（只定义
`TVPGetVersion` / `TVPReadAboutStringFromResource`，用的是 `MsgIntf.h` 的
`TVPVersionMajor` 与 `CharacterSet.h` 的 `TVPUtf8ToWideCharString`）。
删掉后 `core` 才不需要链接 plugin。`std::vector` 来自 `tjsCommHead.h`，不会因为
删这个 include 而失去。

\## 判 cluster 的方法（可复用）

不要看文件名/注释里的职责描述，**看它 include 谁**。一个 `.h` 和它的 `.cpp`
可以、而且应该分在不同层：

- `core/EventIntf.h` + `runtime/EventIntf.cpp`
- `core/FileSelector.h` + `runtime/FileSelector.cpp`
- `core/EventImpl.h`（只有一句 include）+ `runtime/EventImpl.cpp`

这和本仓库既有的"`core/utils` 放接口头、`core/base` 放实现"是同一套分工。

\## 没有解决的

- `script ↔ sysinit` 两条环仍然在，只是显形了（以前藏在 60 个文件里）。
- 公共 include 路径仍是四条 `-I` 指向四目录 = base 整体还是一张扁平面。
  收紧成 `#include "base/core/MsgIntf.h"` 是独立的调用点迁移任务。
- 第三方库仍集中在 `core_base_core`（`unrar`/`LibXml2`/`SDL2`/`zstd`/`minizip`
  base 自己不用，但符号被全引擎共享，例如 SDL2 被 `sound/win32/WaveMixer.cpp` 用）。
  没有编译验证前不动。
- **未编译验证**：本地无 NDK / cmake。第一步应该跑
  `scripts/check_tu.sh --module base`（先 export ANDROID_NDK_HOME）。

## 2026-11-14 — sysinit 整簇并入 environ（125b37c）

结论：`cpp/core/base/sysinit/` → `cpp/core/environ/sysinit/`（git mv 整目录），target `core_base_sysinit` 删掉，源码并进 `core_environ_module`。base 只剩三簇：core/ runtime/ script/。

为什么是「整簇」而不是「只搬 SysInit*.cpp」：include 图显示 `SysInitImpl.cpp` 要 `Application.h`/`SystemControl.h`（environ 自己）+ `GraphicsLoaderIntf.h`/`LayerIntf.h`（visual）；`SysInitIntf.cpp` 要 `ScriptMgnIntf.h`/`KAGParser.h`（base_script）。这一簇每个文件都在往上长，留在 base 里只能靠 base→environ 的反向 include 维持。

关键点：`ConfigManager/` 的目录名不能动。10 处调用点写的是 `#include "ConfigManager/LocaleConfigManager.h"`，所以它作为二级目录整体搬进 `environ/sysinit/`，再把 `${ENVIRON_PATH}/sysinit` 加进 `core_environ_module` 的 PUBLIC include，调用点零改动。顺序放在 `${ENVIRON_PATH}/win32` 之前（同 basename 的 `DetectCPU.h` 历史上踩过「两个同名 guard 副本」的坑）。

连带变化：
- `core_base_runtime` / `core_base_script` 删掉 `core_base_sysinit` 链接项（本来就 PRIVATE 链 `core_environ_module`）。
- `tinyxml2` 的 find_package 随 ConfigManager 落到 environ。
- intra-base 的环从 `script ↔ sysinit` 变成 `script ↔ environ`，环没消。消它要注册表反转（DECISIONS #9）+ `iTVPApplication` 接口抽取，两者都必须有编译器。

验证：未编译。WSL + Linux 版 NDK（F:/runtime/android-ndk/r27d 是 Windows 版，WSL 里跑不了）。最短路径 `scripts/check_tu.sh --module environ` 与 `--module base`。

顺带 fe53f2e：`.gitattributes` 把 `*.sh` 钉成 eol=lf 并把工作区脚本就地转 LF —— core.autocrlf=true 会让 WSL 里的 build.sh 报 `$'\r': command not found`。

## 2026-11-15 — io / sound 分四层：先定"链接行放哪"的规则，再动目录

\## 结论

以 `base` 为范本，把 `io`（20 文件）和 `sound`（41 文件）各切成四个目录 /
四个 STATIC target + 一个 INTERFACE 聚合层。聚合层名字不变
（`core_io_module` / `core_sound_module`），所以 `base` / `environ` / `visual` /
`movie` / `plugins` / `tools/xp3` 里一个字都没改。设计说明分别写在
`cpp/core/io/README.md`、`cpp/core/sound/README.md`，模块约定写在
`cpp/core/README.md`。

\## 决策一：链接行放簇上还是放聚合层，取决于簇之间的边是不是 DAG

- `base`：簇之间有 `script ↔ runtime`、`script ↔ environ` 的声明式环，
  链接行只能写在各簇上（写不下去的部分反而显形了）。
- `io` / `sound`：簇之间的边是 DAG（sound 的环只在 include 方向，见决策三），
  于是**簇只 `PUBLIC tjs2`，真正的依赖全部挂在聚合层**，逐条对应搬迁前的
  PUBLIC/PRIVATE 写法。

写成规则：**簇之间的边是 DAG → 链接行集中（好对照、不新增 target 环）；
有真环 → 链接行分散到簇上（让环显形）**。两种都是"不改最终链接行"的手段，
不是审美选择。

验证方式（本轮新增）：`scripts/check_cmake_graph.sh io sound` 打印聚合层的
`INTERFACE_LINK_LIBRARIES`，与搬迁前的 target_link_libraries 逐条对照即可确认
没有丢依赖、没有多依赖。

\## 决策二：`sound/win32/` 改名 `impl/`，但 `utils/win32`、`environ/win32` 不动

TODO.md 的"命名误导目录"一条：`sound/win32/` 里 6 个源文件在 Android 上全部参与编译
（`WaveMixer.cpp` 就是 Android 的 oboe 后端），目录名与事实相反。
`impl/` 表达的是"接口背后的实现"，与平台无关。

不一起改另外两个，理由是**判据不同**：`utils/win32/`（`ClipboardImpl` /
`DebugImpl` / `PadImpl`）与 `environ/win32/`（`SystemControl` / `TVPWindow` /
`EmergencyExit`）里多数文件是真的 Windows 语义，改名会把"哪些是平台无关"这个
未回答的问题藏起来。STRUCTURE_DESIGN §7 已经登记为"逐个确认后才改"。

\## 决策三：搬目录 ≠ 拆环，sound 的 intf ↔ impl 必须在另一轮解决

`sound` 的环不是目录问题，是头文件问题：四个接口头在**文件中段**include 实现头，

```
intf/WaveIntf.h:222            #include "WaveImpl.h"            // tTJSNI_WaveSoundBuffer 是 tTJSNC_WaveSoundBuffer 的基类
intf/SoundBufferBaseIntf.h:101 #include "SoundBufferBaseImpl.h"
intf/CDDAIntf.h:37             #include "CDDAImpl.h"
intf/MIDIIntf.h:43             #include "MIDIImpl.h"
```

所以 `intf → impl` 的依赖被锁死，任何 include `WaveIntf.h` 的文件都会把 `impl/`
拖进来。**换目录不会改变这条边**，只有"声明留 intf、`CreateNew` 走注入口"
（`tTVPSoundObjectFactory`，与 #9 的 TJS 类注册表反转同一个模板）能拆。

结论：这一轮把目录与 target 拆开，是为了**让这条边只出现在两个目录之间**
（以前它藏在 27 个文件的扁平目录里）；拆它本身排在有编译器之后，和 #9、#10 一起做。

\## 踩坑（下一个搬模块的人直接用）

- **注释里的旧路径比 include 更容易漏**：`grep -rn "io/IoPolicy\|sound/win32"`
  命中 8 处注释（`StorageIntf.h`、`SysInitImpl.cpp`、`StoragePolicy.h`、
  `XP3Archive.cpp`、`IoStorageLocal.cpp`×4）。过期注释比没有注释更糟，
  因为它们会被当成事实引用。
- **显式路径调用点只有 4 处**（`compat/AetherKiriCompanions.cpp`、
  `compat/CompatLayer.{h,cpp}`、`plugin/PluginImpl.cpp`），改起来很便宜；
  但 `cpp/plugins/CMakeLists.txt` 里还有一份**手写的 include 目录清单**
  （`${KRKR2CORE_PATH}/sound/win32`），它不随目标 include 面自动更新。
- **`git mv` 在 Android 上会偶发 `index.lock: File exists`**（瞬时，不是残留锁）。
  批量 `git mv` 要写重试，别去看是不是有别的 git 进程。
- **Termux 里的 cmake 会认为 `ANDROID` 为真**（Termux 本身就是 Android），
  所以在本机跑 `check_cmake_graph.sh` 会走进 `if(ANDROID)` 的 oboe 分支，
  必须提供一个 `liboboe.a` 桩才能过。这条对 CI 不成立（CI 的 Linux 宿主 job
  走 `else` 分支），两边行为不同，别据此判断平台分支写错了。
- **`--allow-multiple-definition` 与 `check_tu.sh` 都不受影响**：搬目录不改符号、
  不改 TU 数，`check_tu.sh --module io` 应该和搬之前同数量级。

\## 没有解决 / 没有验证

- 仍然**未编译验证**（本机无 NDK 交叉编译条件，按计划最后统一验证）。
- `io ↔ base` 声明级环、`StoragePolicy` 半接线、`IO_LEGACY_INCLUDE_DIRS`
  的 15 条历史 include 面：README 里写明，未动。
- `sound` 的 `MathAlgorithms` 重复声明、`xmmlib.cpp` / `WaveFormatConverter_SSE.cpp`
  不在构建里、`sound → movie` 的链接期半解：README 里写明，未动。

## 2026-11-15 — environ / plugin 分四层 / 三层：整模块"契约头单独一层"什么时候成立

\## 结论

- `environ` → `intf/`(INTERFACE，只有契约头) + `sysinit/` + `platform/` + `app/`
- `plugin` → `host/` + `ncb/` + `loader/`
- 两个聚合层名字不变，`base`/`visual`/`movie`/`plugins` 调用点零改动。

\## 决策一：一个模块要不要"纯头层"，看这些头是不是叶子

`environ` 的头确实是叶子（`Application.h` 只 include `tjsVariant`/`tjsString`，
`SysInitIntf.h` 谁都不 include），而所有 `.cpp` 都往上长 → 纯头层成立，
于是 `environ` 内部变成 DAG。

反例是 `sound`：它的 `intf/*.h` 在文件中段 include `impl/*.h`（`tTJSNC_*` 以
`tTJSNI_*` 为基类），头不是叶子，所以 `sound` 没有"纯头层"，只能在 README 里把
环写清楚、等下一轮拆。

判断方法仍然是**读 include**，不是读名字：`Application.h` 听起来像"上层实现"，
实际是叶子；`SysInitImpl.h` 名字带 Impl，实际也是叶子。

\## 决策二：`environ/win32/` 解散，但不改 `utils/win32`、`environ` 里其它平台目录

`win32/` 四个文件性质完全不同（`SystemControl.cpp` 在 Android 上也编译；
`SystemControl.h` / `TVPWindow.h` 是被别处 include 的契约；`EmergencyExit.h` 是纯宏），
一个"平台名"目录混着三种东西。这轮按"实现 → platform/、契约 → intf/"解散。

`utils/win32/` 与 `plugins/` 里的平台目录**不动**：那里的文件多数确实带平台语义，
改名需要先回答"哪些真的平台无关"，属于 TODO.md「命名误导」的后续条目。

\## 决策三：plugin 这一轮只搬文件，不动 NCB 宏

`ncbind.hpp` 被 50 个插件源文件 include，`NCB_*` 宏在插件作用域展开
`template <>` 显式特化（宏体必须写限定名）。把它收成 `krkr::plugin::ncb`
是 11 个宏族的同步改造 —— 与目录重组混做，失败时无法判断是"路径错"还是"特化错"。
所以这一轮的收益是**把宏层的风险隔离在一个 target / 一个目录里**，而不是收口。

\## 踩坑

- **`bridge/engine_api/src/engine_api_internal.h` 是本仓库唯一大量使用显式路径的地方**
  （`environ/Application.h` 这种）。模块一搬，8 条路径全废。更要紧的是里面有一条
  `#include "environ/Platform.h"` —— `Platform.h` 早在 `a176393` 就下移到
  `base/core/` 了，这条从那时起就是失效路径，只是没人编译到。**显式路径的代价是
  它不会自己跟着文件走**；搬家时务必 `grep '"<模块>/'` 一遍。
- **`scripts/check_jni_symbols.py` 里有硬编码路径**（`cpp/core/environ/android/AndroidUtils.cpp`）。
  它读不到文件时**不会报错而是少查一个前缀**，于是 `check_static.sh` 依旧绿。
  这是"假信心"的典型：静态检查工具自身的路径也要跟着搬。
- **generator expression 会让"源文件存在性"检查假失败**：`$<$<BOOL:${MACOS}>:…>`
  在桩工程里求值为空，但 `SOURCES` 属性里仍是字面 genex 字符串。
  `check_cmake_graph.sh` 现在跳过含 `$<` 的条目。
- **`apple/macos`、`apple/ios` 的源文件在仓库里根本不存在**（上游遗留的 genex）。
  没有编译验证前不删，但它意味着 `if(APPLE)` 分支从来没有任何人跑过。
- **纯头层用 INTERFACE 库而不是空 STATIC**：空 STATIC 在部分生成器上
  会产生一个空 `.a`，链接行里多一个无意义条目；INTERFACE 库还能顺带承载
  `target_include_directories`。

\## 没有解决 / 没有验证

- 仍然**未编译验证**。
- `environ` 的四组跨模块环、`plugin` 的三组边、NCB 收口、插件契约：全部未动，
  理由与拆法写在两个 README 的"已知的环"里。
- `plugins/`（119 个源文件）仍无契约，仍是最后一块。

## 2026-11-15 — utils / compat / extension：簇数按"有几个可独立理解的实现群"定

\## 结论

- `utils` → `c_api/`(C 接口与生成表) + `misc/`(纯 C++ 工具) + `intf/`(TJS 接口) +
  `impl/`(原 `win32/`)，4 个 STATIC + 聚合层。
- `compat` → `layer/`(策略与门控) + `companions/`(层专属内容)，2 个 STATIC + 聚合层。
- `extension`（`Extension.h` + 1 个 `.cpp`）**不拆**，理由写进它自己的 README。

至此 `core/` 只剩 `visual`(156 文件) 与 `movie`(85 文件) 未按层重组。

\## 决策一：不追求"每个模块一样多簇"

现在的分布：base 3 / io 4 / sound 4 / environ 4 / plugin 3 / utils 4 / compat 2 /
extension 1。簇数是**用 include 图数出来的实现群数量**，不是规则推导的结果。
判据：一个集群里至少要有两个可独立理解的实现文件，
否则只是把空壳 target 加到链接行里（`extension` 就是反例，所以没拆）。

\## 决策二：`utils/win32 → impl` 之后，全仓没有"名字说 Windows、实际编 Android"的目录

这一列的清理是分三批做完的：`sound/win32 → sound/impl`、
`environ/win32` 解散（实现进 `platform/`、契约进 `intf/`）、`utils/win32 → utils/impl`。
判据都是同一句：**先问"它在 Android 上也编译吗"**，再决定是改名还是解散。
现在只剩 `cpp/plugins/` 里的平台目录没动（那是插件侧，等插件契约）。

\## 决策三：`compat` 拆层的价值是"策略不认识内容"能在 CMake 里验证

拆之前 `CompatLayer.cpp` 与 `AetherKiriCompanions.cpp` 在同一个 target 里，
所以"策略不 include 存储实现"这句话只能靠 review 保证。拆之后 `layer/` 的
include 集合里不再有 `base/core/StorageIntf.h` 与 `io/storage/IoVirtualFile.h` ——
**约束从文档变成了构建图**。这是把"半接线抽象"往可验证方向推的最小一步
（接线本身仍然没做）。

\## 踩坑

- **`compat` 是全仓显式路径最多的模块（7 处）**：核心里 1 处
  （`plugin/ncb/ncbind.cpp`）、插件里 5 处（`krkrgles` / `krkrlive2d` /
  `aetherkiri/legacy_*` 三个）、bridge 1 处。横切型模块的目录一改，
  波及面按"引用它的模块数"放大，不是按"它自己的文件数"。
- **只有 `.c` 源的簇会让 CMake 报 "can not determine linker language"**：
  `core_utils_c_api` 只含 `md5.c` / `encoding/*.c`，需要根 `project()` 开启 `C` 语言。
  真实构建里根 `project(${APP_NAME})` 默认开 C+CXX 所以没事，但
  `check_cmake_graph.sh` 的桩工程原来只声明 `LANGUAGES CXX` → 假失败。
  已改为 `LANGUAGES C CXX`。
- **`PadImpl.cpp` 里有 `#include "PadFormUnit.h"`，那个文件在本仓库不存在** ——
  整段在 `#if 0` 里，是上游文本编辑器遗物。它解释了 TODO.md 的"298 处 #if 0"
  为什么危险：读 include 图时会把不存在的头算成一条依赖（清理 #if 0 是独立一轮，
  本轮只登记）。
- **`utils` 的 `core_environ_module` PRIVATE 链接找不到使用者**：
  真实的跨模块链接需求只有一条（`TVPGetRoughTickCount32`，在 base）。
  没有链接验证前不动 —— 少了它可能只是少一次 `--start-group` 的机会，
  也可能是某个模板实例的来源。

\## 没有解决 / 没有验证

- 仍然**未编译验证**。
- `utils` 的 intf ↔ impl 环与 `sound` 同型，拆法同一批做（`tTJSNI_*` 声明 +
  `CreateNew` 注入口）。
- `compat` 的半接线（策略 → 存储实现）没动；`ModuleGate` 仍默认放行。
- `visual` / `movie` 未开始；`plugins/`（119 个源文件）仍无契约。
