# 对照证据：KAG 脚本层（KiriNext vs AetherKiri）

> 证据文件，非规范。`OURS/` = `KiriNext/cpp`，`REF/` = `AetherKiri/cpp`。2026-09-18 全量对照结论。
> 溯源：`OURS/cpp/core/base/KAGParser.cpp`（2931 行）与 PocketKrKr/KrKr2-Next 逐字节同源（仅 3 处
> clang-format 重排，无语义差）⇒ **OURS = 上游基线**；`REF`（3647 行）在同一基线上增加 724 行，
> 来自 REF 独有提交（`717430c7` 编译场景校验、`185e716d` KAG 渲染兼容、`daaa1bc8` 翻译接入）。
> KAGParser 层：**REF 是严格超集，OURS 没有 REF 缺失的解析器级修复**。

## 1. 核心 KAGParser 的 REF 增量（逐项）

| # | 能力 | REF 位置 | 说明 | OURS |
|---|---|---|---|---|
| 1 | `copyTag(name, source?)` 原生方法 | `KAGParser.cpp:3298-3315`（实现 `:3131-3211`）、`KAGParser.h:389` | 克隆标签字典；内部 `CloneTag(:3054)` 也用 | 缺 |
| 2 | `taglist` 隐藏元数据 | `:113-149` 定义、`:150-245` 助手、调用点 `:1571/1855/1896/2005/2019/2289/2629` | 每个返回的标签字典带 `taglist=[tagname, 属性名…]`（`TJS_HIDDENMEMBER`，`EnumMembers` 看不到）；`PushMacroArgs` 因 `Dictionary.assign` 跳过隐藏成员而显式重设（`:1567-1571`）；`[tag *]` 用 `taglist` 重建 `parsed_attributes`（`:2812-2833`） | 缺 |
| 3 | 明文行翻译 | `:1917-1969` | 整段 `TVPTransformText("kirikiri", run)` 后回写 `LineBuffer` + `PrefetchTextLookahead` | 缺 |
| 4 | `GetNextTag` 文本段聚合 + `TextTagQueue` | `:3062-3128`，助手 `:2968-3060`，成员 `KAGParser.h:272-275` | 连续 `ch` 标签（上限 2048）合并为一个文本段翻译后按字符重切、按索引克隆源标签模板、边界标签最后入队；`Interrupted` 清队列 | 缺（OURS 直接 `return _GetNextTag()`） |
| 5 | `.scn` 编译场景容错 | `:415-425` | 读失败但同名 `.scn` 存在时 `Buffer="*\n"` 而不抛 | 缺 |
| 6 | 编译场景标签解析回调 | `KAGParser.h:31-35`、`:320-360`、`GoToLabel` `:1394-1399` | `TVPRegisterCompiledScenarioLabelResolver()`；找不到标签时先问解析器再抛 `TVPKAGLabelNotFound` | 缺（全树无该符号） |
| 7 | 上条的消费者 | `REF/cpp/plugins/psbfile/main.cpp:181-190/220/224` | 扫 PSB 场景树里的 `*label`（`CollectScenarioLabels` `:128` 等） | 缺 |
| 8 | 标签通知 → 环境/世界复位修复 | `KAGParser.cpp:2290` + `ScriptMgnIntf.cpp:3890-3991` + `EngineLoop.cpp:149` | 每个 `tag_other` 进入有界观察窗；`envclear` 置 8192 个标签的窗口，其余 120 帧修复 | 缺 |
| 9 | `[endtrans]` → 无 trans 等待修复 | `KAGParser.cpp:2291-2292` + `ScriptMgnIntf.cpp:3759-3819` + `EngineLoop.cpp:148` | 卡在无 trans 的 `envupdate` 上时强制 `kag.conductor.trigger("trans")` | 缺 |
| 10 | 环境变量门控诊断 | `:27-270`、`:786-791`、`:2148-2186`、`:3171-3210` | `AETHERKIRI_SAVE_TRACE` / `KAG_TAG_TRACE*` / `KAG_QUEUE_TRACE` | 缺 |
| 11 | 队列失效点 | `:702`、`:1330` | `operator=` / `ClearBuffer` 时清 `TextTagQueue` | 缺 |
| 12 | `TVPClearScnearioCache()`（原拼写） | REF 定义 `:555`，声明只在 `bridge/engine_api/src/engine_api.cpp:140` | 跨重启清场景缓存 | OURS 头文件有声明（`KAGParser.h:374-375`） |

**行为不等价点（即使关掉翻译）**：`GetNextTag` 会连续取到文本段末尾并把边界标签入队 ⇒
`getNextTag()` 之后的 `kag.curLine/curPos/curLineStr` 指向文本段之后；期间 `kag.store()` 存的是更靠后的位置；
`interrupt()` 只在下次进入时清队列，`kag.restore()` 不会丢弃已克隆标签；翻译变长时尾随标签继承最后一个源标签参数。

**逐节核对为"相同"的部分**：词法（`[[ ]]`/`\` 续行/引号/`@` 形式/反引号）、21 条特殊标签表与顺序、
`iscript/endscript`（含 `[eval exp='…']` 重发技巧）、`*label` 记账（除 `.scn` 回落）、
`macro/endmacro/macropop/erasemacro/pmacro` 逻辑、`call/jump/return`、`while/endwhile/break/continue`、
错误消息集合（41 处、常量名一致）。

## 2. `ScriptMgnIntf.cpp` 的 KAG 运行时补丁层（REF 独有，最大缺口）

REF 4945 行 / OURS 2029 行；`ScriptMgnIntf.h:86-125` 导出的 19 个函数里 **OURS 只有 2 个**。

REF 独有：`TVPRegisterPostStartupScriptHook`、KAG 加载契约守卫（`TVPGetKagLoadContractGuardScript` +
`Scripts.getStorageExecutionSerial`/`execStorageNative`）、patch 运行时注册表合并
（`TVPGetPatchRuntimeRegistryExpression`、`TVPGetPatchRuntimeInstanceRecoveryScript`、
`TVPMergeObjectMembers`/`TVPMergeMissingObjectMembers`）、`TVPExecuteTextScriptWithRecovery`、
`TVPPatchWorldRestoreFaceVisibility`、`TVPPatchAffineSourceMotionStorageFallback`（`dx_`/`dxlow_` 回落）、
`TVPRepairShiftedNumberedMovieMappings`、无 trans 等待修复、环境/世界复位修复、
**27 个按存储名匹配的脚本文本补丁**（`TVPApplyScriptCompatibilityPatches`，`ScriptMgnIntf.cpp:1071-2559`；
覆盖 `envinit.tjs`/`kagenvimage.tjs`/`kagenvironment.tjs`/`messagelayer.tjs`/`custom.tjs`/`mainwindow.tjs`/
`standaffinesourcelayer.tjs`/`d3d.tjs`/`affinesourcemotion.tjs`/`motionaffinesourcelayer.tjs`(~400 行)/
`world.tjs`/`d3daffinesourcemotion.tjs`/`standinformation.tjs`/`standimage.tjs`/`psdlayer.tjs`/`standlayer.tjs` 等）、
**11 个执行后类包装补丁**（`TVPApplyPostScriptCompatibilityPatches`，`:2785-3079`；保存原实现到
`global.__aetherKiri*` 后重新包一层）、3 个每帧 KAG 修复、伴生脚本替换（见 §4）。
另有 `kag_runtime_defaults` 在 `AfterStartup.tjs` 之后再补一次（REF `:4210`，OURS 只补一次 `:1194`）、
`-debugwin=no` 种子（REF `impl/SysInitImpl.cpp:562-575`）、引擎卸载时 `TVPUnloadInternalPlugins()`。

**OURS 领先 REF 的两处（保留）**：
1. `TVPGetPatchWindowPrerequisitesScript` 用 `typeof global.KAGWindow`（OURS `ScriptMgnIntf.cpp:956-980`）；
   REF 用裸标识符，TJS2 里对不存在标识符 `typeof` 会抛异常 ⇒ REF 每局多刷一条异常（おっぱいスパイ学園 实证）。
2. `kirikiriz` 返回整数 `0`（OURS `SystemImpl.cpp:879-893`，有 SIGSEGV 记录的取舍）；REF 返回 mock 对象，合并会带回崩溃。

**OURS 独有资产**：`plugins/zcompat/k2compat_scripts.cpp`（2359 行 Krkr2Compat TJS：`k2compat.tjs`、
`win32dialog.tjs`、`k2compat_modeless.tjs`、`k2compat_padcommon.tjs`、`k2compat_pad.tjs`、
`k2compat_console.tjs`、`k2compat_inputstring.tjs`、`k2compat_fontselect.tjs`）——
**没被任何 target 编译、`TVPInstallK2CompatScripts()` 零调用**（`zcompat_plugin.cpp:54-56` 把 `k2compat.dll`
映射成空桩；`:48-53` 记录了当年在 `LoadAllModules` 里执行导致 3 款游戏黑屏的回退历史）。
REF 完全没有这套脚本。接线位置与时机会是独立议题。

## 3. TJS2 内核层兼容（A 块）

| 机制 | REF 位置 | 作用 | OURS |
|---|---|---|---|
| 全局名回退 | `core/tjs2/tjsObject.cpp:249-275` + 调用点 `:1807`（`tTJSCustomObject::PropGet` `:1772`） | 成员找不到时按白名单回退到同名全局：`LayerClass→Layer`，以及 `System/Storages/Scripts/Dictionary/Debug/Math/Plugins/Window/Layer/inSystemMenuStorages/kagHookEntries/afterInitCallback/COMMAND_SYNC|ASYNC|WAIT/kirikiriz/kirikiriz_generic/AffineSource*/clNone/ltBinder/ltOpaque/ltAlpha/ltAdditive/ltSubtractive/omAlpha/omAuto/debugWindowEnabled`（34 个）；有 `thread_local resolving` 重入保护 | 缺（未定义成员一律抛 `Member "x" does not exist`） |
| 启动名回退 | `:171-247`、`TJSCompatIsStartupNoOpFunction` `:64-76`、调用点 `:1806` | 11 个名字当 no-op 函数（返回 1）、`ShortCutInitialPadKeyMap`/`…GamePadKeyMap→[]`、`CompoundStorageMedia→字典类`、`archiveUniqueKey`、`kirikiriz/inXP3archivePacked→1`、`llsDllLoadDir…` Win32 常量、`kirikiriz_generic/debugWindowEnabled/developMode→0` | 缺 |
| `kag.*` 运行时回退 | `:310-366`、调用点 `:1803` | 全局 `kag` 上读 `autoMode/skipMode/autoModePageWait/autoModeLineWait/userChSpeed/autoModeWaitVoice` 返回 0 | 等价（OURS 用 `kag_runtime_defaults.tjs` 注入） |
| `TextRender.renderCount` 合成 | `:368-397`、调用点 `:1810` | 读该属性不抛 | 缺 |
| `touchImage` 合成 | `:41-62`、调用点 `:1654/:1797` | — | 缺 |
| 启动期可写白名单 | `core/tjs2/tjsObjectExtendable.cpp:9-19` + 守卫 `:96-105` | 8 个名字（`debugWindowEnabled/inXP3archivePacked/convertMode/drawDevice/gpuDrawDevice/nativeDrawDevice/OGLDrawDevice/GLESAdaptor`）在 `PropSet` 返回 `ACCESSDENYED/MEMBERNOTFOUND` 时用 `TJS_MEMBERENSURE|TJS_IGNOREPROP` 重试 | 缺（OURS 全靠 C++ `krkrgles.cpp:3254-3256` 写入绕开） |

两处都是**按名字表白名单**的纯增量，不会掩盖任意拼写错误；合并时唯一风险是行为变化
（`typeof <未定义标识符>`、`this.<框架全局>` 从"抛"变"有值"），建议**按层开关**。

## 4. 存储层脚本替换（REF 独有机制）

`TVPRegisterStorageResolver`/`Unregister`（`StorageIntf.h:239-244`、`:330-365`）+ 伴生脚本谓词
（`:368-404`：`gfxEffect`/`gpu`/D3DEmote/LogWindow）+ 打开器（`:406-438`，消费点 `:1302-1305/2023-2026/2178-2189`）
+ 内嵌 `LogWindow` KAGEX 类（`:46-…`）+ 生成的 D3DEmote TJS blob（`core/base/resources/D3DEmote_tjs.cpp.in`
→ `kAetherKiriD3DEmoteTjs`）。OURS 全部缺失；OURS 的等价物是 `krkrgles.cpp:3263-3289` 的
`KrkrOglKagScript()`（由引擎选项 `ogldrawdevice_compat` 门控 + 首帧一次性钩子安装），以及
`GpuCompatScript.h`（26 行）在 REF 里由"打开 GPU 伴生脚本且真实文件不存在"触发。

## 5. 其它 REF 独有模块

| 项 | REF | OURS |
|---|---|---|
| `MDKParser.dll`（4470 行，MIT，vendored） | `plugins/mdkparser/`，全局类 `MDKParser.loadScenario()` | 完全缺（依赖 `TJSReservedWordsHashAddRef` OURS 已有） |
| `ExtKAGParser.dll`（约 4700 行，第二个解析器） | `plugins/extkagparser/`，`ExtKAG.cpp:31` **无条件覆盖**全局 `KAGParser` | 只有空壳注册名；`kagparserex_plugin.cpp` 是 25 行空实现 |
| `kagparserex` 真实实现 | 109 行：按需安装核心 `KAGParser`、设 `AetherKiriKAGParserEx` 标记、引用计数卸载 | 空函数体（`LinkKAGParserUncompatibility(){}`） |
| `CompatibleNativeFuncs.{h,cpp}` | 仅 Win32 `USER32.DLL` 触摸/手势函数指针表，且**在 REF 里是死代码**（调用点在 `#if 0`、不在 CMake 源列表） | 无需移植 |
| `packinone.cpp` / `archiveUniqueKey` | `"AetherKiri.CompoundStorageMedia"` | 用 TJS 注入，键为 `"KrKr2Next.CompoundStorageMedia"`（可观察差异） |

## 6. 合并冲突清单（若两套实现同时进一个二进制）

1. **源码级**：`KAGParser.cpp` 两树都定义 `tTVPCharHolder`/`tTJSNI_KAGParser`/`TVPCreateNativeClass_KAGParser`/
   全局 `TVPKAG*` 常量 ⇒ 只能选一份。`kagparserex_plugin.cpp` 两树都定义
   `extern "C" TVPRegisterKAGParserExPluginAnchor` ⇒ 重复符号。
2. **头文件保护宏撞车**：`ExtKAGParser.hpp:12-13` 的 `#ifndef KAGParserH` 与 `core/base/KAGParser.h:12-13` 逐字节相同
   ⇒ 同 TU 包含两者会静默丢掉后者；且两者都在全局命名空间定义 `enum tTVPKAGDebugLevel` 与 `class tTVPCharHolder`
   （目前只靠上面这个 bug 隔离）。移植前必须改名。
3. **运行时 `KAGParser` 全局类最多四个写者**：核心注册（REF `ScriptMgnIntf.cpp:576` / OURS `:515`）、
   `KAGParserEx` 安装、`ExtKAGParser` 覆盖、OURS 空壳。`ExtKAGParser` 的卸载恢复顺序可能让全局 `KAGParser` 变回未定义。
4. **模块名不按"最后注册者赢"**：`ncbAutoRegister::AllRegist` 对每个 (模块, 行) 追加条目，
   `LoadAllModules` 按 **std::map 的字母序** 遍历 ⇒ `ExtKAGParser.dll` 的 PreRegist 早于 `KAGParserEx.dll`。
   两树重名的模块还有 `k2compat/kagexopt/lzfs/kztouch/packinone/squirrel/multiimage/win32ole/xpzdec/extnagano/pkutil/drawdeviceD3D*`。
5. **`KAGWindow_createDrawDevice` 两份定义**（OURS `krkrgles.cpp:3263-3289` 选项门控 / REF `GpuCompatScript.h:11-26` 惰性触发），最后跑的那个赢。
6. **`Window.OGLDrawDevice`/`GLESAdaptor` 两个写者**：OURS 走 C++ `PropSet`，REF 走 TJS 赋值（需要白名单）；
   加上白名单后，游戏补丁里的同类 TJS 赋值也会开始成功——这正是白名单的意图，但会改变当前静默失败的游戏行为。
7. **`taglist` 隐藏成员**与其它标签字典消费者的兼容：REF 自己的 ExtKAGParser 不调用 `TVPSetKagTagList`
   ⇒ 选它时元数据静默消失；用 `EnumMembers` 的消费者看不到 `taglist`，必须直接 `PropGet`。
8. `archiveUniqueKey` 字面量不同（见 §5）；`kirikiriz` 类型不同（整数 vs mock，见 §2）。

## 7. 结论：三条独立缺口

- **(a) KAG 运行时补丁层**：27 个文本补丁 + 11 个类包装 + 3 个每帧修复 + 晚 patch 韧性 + 伴生脚本替换
  （约 1500 行 `ScriptMgnIntf.cpp` + 约 200 行 `StorageIntf.cpp`）。OURS 完全没有。
  ⚠️ 这套东西存在的**前提是 REF 的 `patch.tjs` 在 startup 之后**；OURS 的 `patch.tjs` 在 startup 之前，
  那时 `KAGWindow` 与框架类都还不存在 ⇒ **不能照搬**，要么连 `patch.tjs` 时机一起改（影响所有游戏），
  要么把这批包装改写成"startup 之后由钩子执行"。
- **(b) `ExtKAGParser`**：完整第二解析器（约 4700 行），需先解决 §6.2/§6.3 并决定
  `paramMacros`/`copyTag` 丢失、与 `kagparserex` 空壳互斥。
- **(c) TJS2 内核回退**：约 200 行 + 17 行白名单，最小最便宜；建议按层开关。
