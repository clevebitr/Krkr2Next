# 对照证据：插件模拟层（KiriNext vs AetherKiri）

> 证据文件，非规范。`KN/` = `KiriNext/cpp`，`AK/` = `AetherKiri/cpp`。2026-09-18 对照结论。

## 1. 注册机制差异

| 机制 | KiriNext | AetherKiri |
|---|---|---|
| 模块聚合 | `ncbAutoRegister` 按小写模块名分 PreRegist/ClassRegist/PostRegist 三个列表，`LoadModule` 执行该名下**全部**条目（`core/plugin/ncbind.cpp:64-83`）⇒ 一个模块可由多个 TU 拼装 | 同（AK `ncbind.cpp:104-132`） |
| 模块别名 | **无** `NCB_REGISTER_MODULE_ALIAS` | 有（AK `ncbind.hpp:2181-2190`、`ncbind.cpp:57-67`） |
| 注册失败回滚 | **无**：某个 registrar 抛异常 ⇒ `LoadModule`/`LoadAllModules` 直接向上抛（KN `ncbind.cpp:71-78/123-130`），**一个坏注册会中断其后所有模块**（`zcompat_plugin.cpp:48-53` 记录的正是这个坑） | 有逐条回滚（AK `ncbind.cpp:117-126`） |
| 启动加载范围 | `AllRegist()` + `LoadAllModules()`（全部） | 有 `plugin_load_mode`：默认只加载 `xp3filter/varfile/shrinkCopy`（+krkrgles），`aether_all` 才全量（AK `PluginImpl.cpp:57/737-762`） |
| `.tpm`→`.dll` | 有（`ncbind.cpp:26-60`） | 无该回退 |

## 2. 覆盖情况（按模块名）

**完全缺失（KN 无任何注册）**，共约 60 个：
`zlib.dll`、`version.dll`、`dmmcloud.dll`、`layerExSubImage.dll`、`kropus.dll`、`libegl.dll`、`libglesv2.dll`、
`msbtnhook.dll`、`layeredwindow.dll`、`drawdevice.dll`、`drawdeviceZ_D3D9.dll`、`drawdeviceOgre.dll`、
`drawdeviceIrrlicht.dll`、`layerEx.dll`、`layerExCairo.dll`、`layerExGdiPlus.dll`、`layerExAgg.dll`、
`gameswf.dll`、`magickpp.dll`、`videoEncoder.dll`、`tftSave.dll`、`msdfrender.dll`、`windowExProgress.dll`、
`httpserv.dll`、`wsh.dll`、`wmrdump.dll`、`xpressive.dll`、`mkpj.dll`、`process.dll`、`shellExecute.dll`、
`systemEx.dll`、`registory.dll`、`stdio.dll`、`htmlhelp.dll`、`adjustMonitor.dll`、`fpslimit.dll`、
`httprequest.dll`、`xmlhttprequest.dll`、`javascript.dll`、`messenger.dll`、`msgreceiver.dll`、
`tasktray.dll`、`sigcheck.dll`、`oleclass.dll`、`resourceRW.dll`、`wuffmpeg.dll`、`wumsadp.dll`、
`onigruma.dll`、`gfxEffect.dll`、`flashPlayer.dll` 等。

**部分覆盖**：
| 模块 | KN 现状 | 缺口 |
|---|---|---|
| `kztouch.dll` | 空桩（`zcompat_plugin.cpp:44-46`） | 缺 `KZTouch` 类（enabled/available/enable/disable/reset） |
| `layerExColor/Mosaic/AVI.dll` | `extrans.cpp:30-36/50-52` 空桩 | 缺 AK 的 `Layer.colorize/mosaic/…` 附着（注意：KN 的真实 `Layer.colorize` 来自 `layerExImage.cpp:560-562`，不能覆盖） |
| `lzfs.dll` | 媒体真实、**透传**（`zcompat_plugin.cpp:98-166`） | 缺 `Lzfs` 类（normalize/exists）；LZ4 解码两边实际都没开 |
| `k2compat.dll` | 空桩；**`zcompat/k2compat_scripts.cpp`（2359 行）没有被任何 target 编译，`TVPInstallK2CompatScripts()` 零调用 ⇒ 死代码** | 整块脚本级兼容未接线 |
| `squirrel.dll` | 空桩 | 缺 `SQFunction`/`SQContinuous` 等类表面 |
| `win32ole.dll` | 空桩 | 缺 `WIN32OLE`/`ActiveX` 类表面 |
| `DrawDeviceD2D.dll` | 只注册了别名名 `DrawDeviceD2Dm.dll`（`drawDeviceD2DCompat.cpp:951-962`）；且 `present()` 与 `Method(present)` 被删 | 缺规范名注册（需模块别名机制）与 `present()` |
| `layerExSave.dll` | `extrans.cpp:38-40` 空桩 | 缺 11 个 `Layer.*`（`saveLayerImagePng`/`getCropRect`/`getDiffPixel`…），AK 侧是真实像素实现（368 行） |
| `csvParser.dll` | 表面相同 | 缺 `NCB_PRE_UNREGIST_CALLBACK`（`ArrayClearMethod` 不释放） |
| `textrender.dll` | 表面少一批成员 | 缺 `newline/redraw/renderCount`、`Layer.EdgeShadowDrawText*`、若干 delegate；且 `renderDelay`/`renderOver` 注册成**方法**，AK 明确警告应为只读属性（调用方会无限重试溢出文本） |
| `menu.dll` | 空桩 + 核心 `Window.menu` | 缺 `MenuItem.textToKeycode/keycodeToText/HMENU` |

**已等价（whitespace-only diff）**：`wfBasicEffect.dll`、`wfTypicalDSP.dll`、`waveFilterCompat.hpp`。
**已具备且部分超集**：`win32dialog.dll`（KN 原生实现更全）、`layerExDraw.dll`、`json.dll`、`psd.dll`、`fstat.dll`、`motionplayer.dll`、`psbfile.dll`、`kagparserex.dll`。

## 3. 两层同时编译会冲突的点

1. **同名模块两套 registrar**（`kztouch/k2compat/kagexopt/lzfs/layerExColor/layerExMosaic/layerExAVI/wuopus/wuflac/krmovie/m2vdec/squirrel/win32ole/menu`）：`LoadModule` 会跑全部条目，结果是两套表面的并集。
2. **同名 C++ 类重复定义**：`drawDeviceD2DCompat.cpp` 在两边都在**全局作用域**定义 `D2DView`/`DrawDeviceD2D` ⇒ 同时编译是 ODR/多重定义错误（或链接器任选其一）。
3. **同名 TJS 类注册两次**：`wfBasicEffectCompat.cpp`/`wfTypicalDSPCompat.cpp` 两边注册同名类；`ncbClassInfo<T>::Set` 按 C++ 类型，不会报"已注册"，但两个类对象共用同一 class ID ⇒ 实例可能被按另一类型解释（类型混淆 UB），卸载时还会互相 `DeleteMember`。**结论：同一模块名只保留一份源码。**
4. **同名 Layer 方法附着两次**：`NCB_ATTACH_CLASS` 用 `TJS_MEMBERENSURE`（覆盖），卸载用 `DeleteMember`（`ncbind.hpp:2035-2060`）⇒ 后注册的桩会覆盖 KN 的真实实现，卸载后真实实现被删。
5. **存储媒体重名**：`lzfs` 两边都注册；管理端按名字去重（重复注册静默保留旧实例），但 `TVPUnregisterStorageMedia` **按名字删记录、不看指针**（KN `StorageIntf.cpp:230-238`）⇒ 非属主注销会毁掉记录并让属主注销时抛 `TVPMediaNameIsNotRegistered`。合并前必须先定属主。
6. AetherKiri 自身也有重复：44 个模块名同时被 `compat*Plugins.cpp` 与 `dummy_plugin_stubs.cpp` 注册。

## 4. 移植成本与依赖

| 批次 | AK 行数 | 真实工作量 |
|---|---|---|
| `compatLegacyPlugins.cpp` | 657 | 真实实现只有 zlib(~102)、version(~29)、lzfs 类(~29)，其余是空桩/转名 |
| `compatMediaLayerPlugins.cpp` | 942 | 真实实现只有 msdfrender(~220) |
| `compatSystemPlugins.cpp` | 1447 | 真实：popen 捕获(~110)、存储助手(~35)、env/URL(~170)、HttpRequest/XMLHttpRequest(~150) |
| `layerExSaveCompat.cpp` | 368 | 单文件、11 个 Layer 方法 |
| `drawDeviceD2DCompat.cpp` 差量 | ~45 | 补 `present()` + 模块别名机制（`ncbind.hpp` ~35 行） |
| 合计 | ≈3450 行 C++ + CMake | — |

依赖（KN 均已具备，除注明外）：`LayerIntf.h:482-518` 的 province/main image 与 `Update(rect)`、
`LayerIntf.cpp:11224-11264` 的 `mainImageBuffer*`（int64 指针）、`Layer.saveLayerImage`(`:8196`)、
`DrawDevice.h`/`WindowIntf.h:319`/`LayerManager.h:24`、`WaveIntf.h`、`ncbDictionaryAccessor`、
`NCB_ATTACH_FUNCTION_WITHTAG`。**缺**：`find_package(ZLIB)`（vcpkg 有 zlib，CMake 没接）、
进程内 HTTP 客户端（AK 用外部 `curl` 二进制，AOSP Android 没有这个二进制）。

## 5. 风险（按严重度）

1. **全部跑在脚本线程**：`process.commandExecute`/`HttpRequest.send`/`httpserv.start`/popen 捕获都无超时；
   Android 上脚本线程就是持有 EGL 的渲染线程（AGENTS 规则 7）⇒ 端口阻塞会直接卡帧。
2. **无 D3D/D2D**：`DrawDeviceD2D` 是装饰器，`__captureBaseDrawDevice` 没被调用时 `real_` 为空、所有调用静默 no-op；
   真机真实设备是 `krkrgles` 的 `OGLDrawDevice`，移植后必须验证 `TVPMainWindow->GetDrawDevice()` 非空（且保留 RTTI）。
3. **GPU→CPU 回读**：`layerExSave`/`msdfrender` 读 `mainImageBuffer(ForWrite)`（COW 回读），逐像素循环 O(w·h)，
   必须在 EGL 线程且 `GetScanLine` 有效期内；代码假定 32bpp BGRA，8bpp 层未处理。
4. **启动脆弱性**：KN `LoadAllModules` 抛异常不回滚 ⇒ 新增 registrar 必须异常安全，且不能碰可能还不存在的全局/类。
5. **注册顺序敏感**：同名成员互相覆盖、卸载互相删除（见 §3.4）。
6. **64 位指针属性**：Layer 缓冲属性是 `tTVInteger` 编码的指针，必须 `reinterpret_cast<tjs_intptr_t>`，不得窄化。
