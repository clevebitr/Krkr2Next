# 老树（`cpp/core/**`）能力盘点与去向：**0 行复用**

> 结论先说：**不从 `cpp/` 复用任何代码。** 老树只有三种正当用途：
> ① 行为参考（游戏实际怎么跑）；② 兼容补丁的**需求清单**（zcompat 之类）；③ 第 11 步的 ABI 需求书
> （`bridge/engine_api/include/engine_api.h` 本身，不是老树实现）。
>
> 能力去向只有三类：**vendor krkrz 上游** / **按本仓库架构新写** / **明确不做**。
> 分层、契约、装配位置一律以 `docs/ARCHITECTURE.md` 为准；本文只回答"要什么、从哪来、放哪层"。

---

## 1. 为什么不能复用（实测）

| 老树模块 | 规模 | 状态 | 决定 |
|---|---:|---|---|
| `cpp/core/visual` | 104,295 行 / 156 文件 | Kirikiroid2 后裔，已漂移（`tvpgl.cpp` 14,139 行 vs 上游 `tvpgl.c` 6,658；`LayerBitmapIntf.cpp` 与上游 diff 9,025 行；另有 5,278 行自研 GL） | **vendor krkrz 上游**，不取本树 |
| `cpp/core/utils` | 71,140 行 / 43 文件 | Windows 平台实现（剪贴板/Pad/Menu/SysInit/Thread/RenderManager_ogl 辅助） | 新写需要的部分；其余不做 |
| `cpp/core/movie` | 22,207 行 / 85 文件 | krmovie + ffmpeg 胶水 | **不做**（见 §3） |
| `cpp/core/sound` | 17,602 行 / 41 文件 | DirectSound/WASAPI + PhaseVocoder DSP | 新写（契约 + AAudio），不抄 |
| `cpp/core/base` | 17,346 行 / 48 文件 | 消息/存储/事件/配置（含 `win32/`） | 只 vendor **接口头**，实现新写（§2） |
| `cpp/core/environ` | 9,738 行 / 34 文件 | Application / 启动顺序（"不要抄 1062 行的结构"） | 已有 `core/api` + `engine/`，不取 |
| `cpp/core/io` | 9,179 行 / 20 文件 | XP3 / 挂载 / 名字 | 已有 `core/io`（逐字节验收过），不取 |
| `cpp/core/plugin` | 1,317 行 / 5 文件 | ncbind / Susie / 动态库 | **不做**（静态插件注册表已有） |

一句话：老树里"能跑"的部分基本都绑在 Windows 上，剩下的部分（`io`/`api`/`shim`）我们已经有更干净的实现。

## 2. 真需要的那一点点：seam 预算（实测，不是估计）

对 19 个 vendor 候选文件（`tvpgl.c`、`drawable.h`、`LayerBitmapIntf.*`、`LayerIntf.*`、`LayerManager.*`、
`TransIntf`、`ImageFunction`、`ComplexRect`、`BitmapIntf`、`FontSystem`、`FreeTypeFontRasterizer`、
`PrerenderedFont`、`GraphicsLoaderIntf`、`LoadPNG`、`LoadTLG`、`SaveTLG5`）做符号扫描：

| 项 | 数量 | 说明 |
|---|---:|---|
| 引用的不同 `TVP*` 符号 | 634 | 大部分是 visual 自身函数 |
| 其中来自 `base/`、`utils/`、`msg/` 头 | 108 | = 需要我们补的部分 |
| → 需要实现的**函数** | **20** | `TVPThrowExceptionMessage`、`TVPAddLog`、`TVPGetTickCount`、`TVPPostEvent`（+ 4 个 hook）、`TVPCreateBinaryStreamForRead`、`TVPIsExistentStorage`、`TVPNormalizeStorageName`、`TVPExtractStorageExt`、`TVPExecuteExpression`、`TVPBeginThreadTask`/`TVPExecThreadTask`/`TVPEndThreadTask`、`TVPGetProcessorNum`/`TVPGetThreadNum`、`TVPFormatMessage`、`TVPCancelSourceEvents` |
| → 需要生成的**消息常量** | **83** | 上游 `msg/MsgIntfInc.h` 共 152 条，X-macro 形式；由 `tools/gen_legacy_msg.py` 生成，**不手抄、不从老树取** |
| 其余 526 个 | — | visual 自带（`tvpgl` 等）或只在 `win32/`（不取） |

加 4 个**接口类**（不是函数）：`iTVPStorageMedia`（12 个方法，接 `krkr::io::Storage`）、
`iTVPSysFont`（FreeType）、`iTVPDrawDevice`（我们定义无 `HWND` 子集）、
`iTVPLayerTreeOwner`/`iTVPWindow`（由 `core/api` 的窗口实现）。

**这就是 R1 的全部"接线成本"**：20 个函数 + 1 张生成表 + 4 个接口。不是"搬半个 `base`"。

## 3. 能力盘点（要什么 / 从哪来 / 放哪层）

| 能力 | 老树来源 | 决定 | 归属（按 ARCHITECTURE） | 里程碑 |
|---|---|---|---|---|
| 像素合成（29 算子、中性色、舍入） | `visual/tvpgl.cpp` | **vendor 上游** `tvpgl.c` | `vendor/krkrz/visual/`，经 `shim` 使用 | R1-1 |
| 图层模型 / 转场 / 效果 | `visual/LayerIntf.cpp` 等 | **vendor 上游** | 同上 | R1-3 |
| 位图 / 字体 / 图片解码（PNG/JPEG/TLG5/6） | `visual/LayerBitmapIntf.cpp`、`FreeType*`、`Load*` | **vendor 上游**（FreeType 路径） | 同上；`porting/` 放 `BitmapBitsAlloc`、`SysFont` | R1-2 |
| 消息文本与异常文案 | `base/*` + `msg/MsgIntfInc.h` | **生成**（X-macro → `.inc`） | `vendor/krkrz/msg/` + `tools/gen_legacy_msg.py` | R1-1 |
| 事件/定时器（`TVPPostEvent`、compact/continuous hook） | `base/EventIntf.h`（366 行头） | **新写**（基于 tick 泵） | 契约 `core/contracts/`，实现 `core/api`，pump 在 `core/api::Tick` | R1-3 |
| 存储/XP3/auto-path/补丁优先级 | `cpp/core/io` 9,179 行 | **已有**（`core/io`，逐字节验收） | 不取老树；只需补 `iTVPStorageMedia` 适配 | R1-2 |
| 窗口 / 输入 / 触摸 / 键 | `visual/WindowIntf.cpp` 1,922 + `win32/WindowImpl.cpp` 2,383 | **新写**（Android 单窗口全屏） | `platform/android` + `core/contracts`，`iTVPWindow` 实现 | R1-3 / 第 9 步 |
| 音频（WAVE/OGG/混音/循环平滑） | `cpp/core/sound` 17,602 行（DirectSound/PhaseVocoder） | **新写**：契约 + AAudio/OpenSL 后端 + vendored 解码器；PhaseVocoder 属增强，暂不做 | `core/contracts/IAudio`、`platform/android/AAudio*`、`shim/classes/TjsSound` | 第 10 步 |
| 视频（.mpg/.wmv/AMV） | `cpp/core/movie` 22,207 行（ffmpeg） | **不做**（明确报错）；将来用 MediaCodec + 外部纹理 | 留 `core/contracts/IVideo` 占位 | 第 12 步（不在当前计划） |
| 插件（ncbind / Susie / 动态库） | `cpp/core/plugin` 1,317 + 老树插件目录 | **不做**：静态注册表（已有 `Plugins.link`）+ 显式失败 | 已在 `shim/classes/TjsPlugins` | — |
| E-mote（Q 版动画） | `motionplayer` 7,615 + `psbfile` 6,351 + 自开 EGL 的 `krkrgles.cpp` | **单独立项**：只保留解码/动画，绘制改走统一 seam | 插件层（不得自开 GL） | 8d 之后 |
| Live2D（Q 版动画） | `cubism` + `krkrlive2d.cpp`，SDK proprietary 被 gitignore | **显式化**：要么私有构建纳入 SDK，要么明确不支持并报错（不许静默跳过） | 插件层 | 8d 之后 |
| `layerEx*`（透视/长曝光/光栅）特殊 CG | 297 + 281 + … 行，靠 `GetRenderMethod("...")` | **单独立项**：字符串契约列清单，缺失即显式报错 | 插件层 | 8d 之后 |
| 第三方文本渲染 | `textrender.cpp` 1,259（3c1u/TextRender）+ `FontBaseline.h` | **收口为一条文本路径**（见 `docs/legacy-render-failures.md` §5） | 插件层 / 本仓库字体层 | 字体立项时 |
| 配置 / 存档路径 / 剪贴板 / `System` 其余成员 | `cpp/core/utils` 71k 行 | **新写**，宿主注入 | `core/contracts/ScriptServices.h` + `shim/classes/TjsSystem` | 第 9/11 步 |
| 线程 / 临界区（`tTJSCriticalSection`、ThreadIntf） | `base/`、`utils/` | **新写**（`std::mutex`/`std::thread`），只在 `porting/` 暴露 | `vendor/krkrz/visual/porting/` | R1-2 |
| 调试事件 / 日志 | 老树 `DebugIntf` | **已有**（`debugger/` + `core/log`）；只沿用语义 | 不取 | — |
| 对外 ABI（生命周期、tick、surface、日志、输入注入） | 老树实现（不抄） | **已有** `core/api`，按 `engine_api.h` 逐项补齐 | `core/api` + `bridge/engine_api` 薄壳 | 第 11 步 |

## 4. 新实现必须遵守的流程（ARCHITECTURE §6）

任何"缺的功能"都按同一顺序落地，禁止先写桥接再补契约：

1. **判定归属**：`core`（契约/基座）/ `platform`（平台）/ `shim`（TJS 翻译）/ `engine`（装配）。
2. **先补下层契约**：`core/contracts/` 加小接口（一能力一接口，空指针 = 未提供），
   `HostServices` 聚合；不得在 `shim` 里临时实现引擎逻辑。
3. **再写桥接**：只做"TJS 调用 → 契约调用"的翻译、编码与错误映射。
4. **失败显式**：未实现就抛带上下文的异常/返回码，不返回占位值；过渡期写 `TODO(<步骤>):` 并登记 `todo.md`。
5. **补测试**：`host/` 增加正常路径 + 错误路径断言，注册进 CTest。
6. **更新文档**：`docs/` 与 `todo.md` 同步（含技术债编号）。

## 5. 老树作为参考的正确姿势

| 用途 | 做法 | 产出 |
|---|---|---|
| 行为参考 | 在 Windows 上用 krkrz/老树跑真游戏，记录"哪里不对" | `todo.md` 条目（含游戏名与复现脚本名），不是代码 |
| 兼容补丁 | 老树插件目录里的 `zcompat` 之类 | **需求清单**：每条写清触发条件与期望行为，由我们按 §4 重新实现 |
| ABI | `bridge/engine_api/include/engine_api.h`（547 行 / 109 个入口） | 建议加 `tools/gen_abi_checklist.py` 生成逐函数核对表，第 11 步当检查单 |
| 特殊语义 | auto-path 补丁优先级、大小写回退、`Caps`/中性色 | 已记录在 `todo.md` §4.9 / `docs/render-spec.md` §3 |

## 6. 删除计划

1. **现在起冻结**：`cpp/**` 不再新增功能，不再修 bug（除阻塞当前构建的）。
2. **R1-5**：删除 `cpp/core/visual`、`cpp/core/base`、`cpp/core/utils`、`cpp/core/environ`、
   `cpp/core/io`、`cpp/core/sound`、`cpp/core/movie`、`cpp/core/plugin`、`cpp/core/tjs2`、
   `cpp/tests`，以及老树草稿 `cpp/core/extension/krkrMsgImpl.cpp`（技术债 #6）。
3. **保留**：`bridge/engine_api`（ABI 需求书）、`app/`（Kotlin 宿主）、`vcpkg/`（尚未移除时）。
4. **收口检查**：`python cpp_new/tools/check_layer_deps.py cpp_new` 仍为 0 违规；
   `check_layer_deps.py` 增加 `vendor/` 规则（见 `docs/visual-vendor.md` §3）；
   `tools/run_probes.sh` + CTest 全绿；`docs/` 中所有指向 `cpp/core/**` 的路径改成"参考实现已归档"。

## 7. 与 R1 计划的对接

`docs/visual-vendor.md` 的 R1-0…R1-5 已按本文的归属表分配：R1-1（tvpgl + 消息表生成）、
R1-2（位图/字体/解码 + `porting`）、R1-3（图层 + 事件/窗口 seam）、R1-4（Android 呈现）、
R1-5（删除与文档收口）。第 9 步（输入）与第 10 步（音频）的新实现在 R1 之后独立开工，
接口按 `core/contracts` 定义，不依赖老树。
