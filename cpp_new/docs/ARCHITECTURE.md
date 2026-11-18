# cpp_new 架构约束

本文件约束所有对 `cpp_new/` 的修改，包括人工与 AI 辅助的开发。与 `AGENTS.md` §11（注释风格）
配合使用；发生冲突时以本文件的分层约束为准。

## 1. 目标

在不修改权威 TJS 实现的前提下，为 Android 提供 KiriKiri2 运行环境，并允许按层独立编译、
按模块加载与停用。任何改动都应能回答两个问题：它属于哪一层；它是否破坏了层间依赖方向。

## 2. 分层与目录

```
tjs/        权威实现。krkrz 原样拷贝，不承载适配逻辑。
vendor/     权威实现的第二个来源（krkrz 的 visual 切片 + 我们补的 msg 实现，见 docs/visual-vendor.md）。
            上游源码原样拷贝；「平台 porting」实现也放在这里，但用 §4 的命名区分。
shim/       垫层。连接抽象层与权威实现，承载 TJS 原生类桥接与脚本装载。
visual/     合成与位图。图层树 → DrawList、软件后端；混合数学不在这里（见 pixel/）。
pixel/      混合操作的抽象层。把 vendor 的上游像素函数收成 `krkr::pixel` 的窄接口。
            渲染的现状与目标见 docs/rendering-architecture.md。
platform/   抽象层。实现平台能力（Android / 桌面），不感知 TJS。
core/       基座。日志、存储、能力契约、对外 API。
engine/     装配层。装配脚本引擎、兼容声明与原生类注册表；引擎侧唯一的"接线处"。
debugger/   调试器子包。实现调试契约（事件流与执行控制），由宿主注入；引擎不依赖它。
host/       测试台。不属于引擎产物。
tools/      代码生成、构建与静态检查。
docs/       规格与约束文档。
```

各层职责边界：

| 层 | 必须做 | 禁止做 |
|---|---|---|
| `tjs/` | 提供脚本解释器与内置类 | 承载平台适配、引擎调用、日志输出 |
| `vendor/` | 提供渲染的像素与图层语义（tvpgl、Layer/Bitmap、字体、图片解码） | 被 `vendor/` 之外的层直接包含；修改上游源码而不登记 |
| `shim/` | TJS 原生类注册、编码转换、宿主符号提供 | 直接访问文件系统、窗口、线程原语 |
| `pixel/` | 单像素混合、操作词汇表（`krkr::pixel::LayerOp`）与上游函数映射 | 引用 `visual/`、`shim/`、`tjs/`、平台实现 |
| `legacy_support` | 上游 visual 依赖、但必须由本仓库实现的那一层：消息文案、存储媒体、时间/线程、位图内存、字体 | 引用上游 `visual/` 或 `shim/` |
| `visual/` | 位图、场景数据、DrawList 合成与软件后端 | 引用 `shim/`、`tjs/`、`vendor/`、平台实现 |
| `platform/` | 实现 `core/contracts` 的能力接口 | 引用 `shim/` 或 `tjs/` 的任何头 |
| `core/` | 基座服务、能力契约、模块注册、对外 API | 引用 `shim/`、`platform/`、`tjs/`、`engine/` |
| `engine/` | 装配引擎与原生类、应用兼容声明、把脚本运行时注入 `core/api` | 承载平台或 TJS 适配逻辑（那属于 `shim/`） |
| `debugger/` | 实现 `core/contracts/DebugServices.h` 的事件与控制接口，提供录制/回放与协议 | 被任何下层引用；改变运行时语义 |
| `host/` | 可执行的测试台与断言脚本 | 被引擎产物链接 |

## 3. 依赖方向

```
platform ──► core
vendor   ──► tjs（只允许 tjs 与自身；平台能力通过 vendor 的 porting 实现注入）
pixel    ──► vendor、core (log)
visual   ──► pixel、core
shim     ──► core (contracts, log) 、tjs、visual、pixel、vendor
legacy   ──► tjs、core（上游 visual 需要的消息/存储/时间/线程/内存/字体由本层实现）
tjs      ──► 无源码依赖（宿主符号在链接期由 shim 提供）
core     ──► 无上层依赖
engine   ──► shim、core、platform
debugger ──► engine、core（只实现契约，不被任何层引用）
host     ──► engine、debugger、core
```

规则：

1. 任何方向相反的引用均为缺陷，优先改设计而不是加临时声明。
2. `vendor/` 是唯一允许包含「老树头」（`tjsCommHead.h`、`MsgIntf.h`、`StorageIntf.h` …）的地方；
   铁律「不 include 老树任何头」的适用范围是 `vendor/` 之外的层。`vendor/` 只对 `shim/` 暴露
   稳定接口，不得让 `core/`、`engine/` 出现 `vendor/` 的包含。
3. 需要"逆向调用"时使用 `core/contracts` 的接口：`engine/` 实现、`core/api` 通过接口调用
   （`IScriptRuntime`）。不得让 `core` 反向引用 `shim`、`tjs` 或 `engine`。
4. `shim/` 不依赖 `platform/` 的源码；平台能力通过 `core/contracts` 接口取得，
   因此缺少平台实现时 `shim/` 仍可独立编译与测试。
5. 装配只发生在 `engine/`：`shim/` 提供"装什么"的入口（注册函数），`engine/` 决定"装哪些、按什么顺序"。
   新增原生类时改 `engine/CompatProfile.cpp` 的列表，不要在 `shim/` 里做自注册。
6. 调试与可观测性走 `core/contracts/DebugServices.h`：下层只发事件/读控制接口，
   实现全部在 `debugger/`（规格见 `docs/debugger-spec.md`）。缝合点必须"空指针即跳过"。
7. 同一时刻只有一个组件回答「这个像素应该是什么颜色」：R1 完成后 `visual/Scene`、
   自写混合数学与自建图层树必须退场，不允许保留第二条合成路径（`docs/visual-vendor.md` §3）。

## 4. 权威实现的约束

1. `tjs/` 与上游的差异必须为零，唯一例外是登记在 `tjs/VENDOR.md` 的补丁；新增补丁需附证据
   说明上游代码在目标工具链下无法编译，而非为了迁就我们的设计。
2. 平台与工具链差异一律通过 `shim/` 解决：预置头文件、宿主符号、生成的源文件、编译参数。
3. 上游不提交的生成文件（如日期词表）由 `tools/` 生成，产物落在 `shim/`，不得写回 `tjs/`。
4. 上游同步流程：重新拷贝 `tjs/`，重放 `tjs/VENDOR.md` 中的补丁，运行 `host/` 下的全部测试台。
5. `vendor/krkrz/` 适用同样的规则（原样、补丁登记在 `vendor/krkrz/VENDOR.md`、逐文件 sha256），
   版本以 `docs/visual-vendor.md` 记录的上游 tag 为准。

## 5. 模块与生命周期

模块（原生类、平台能力、插件）的生命周期分三级，与权威实现的实现限制一致：

| 级别 | 适用对象 | 实现方式 |
|---|---|---|
| 可卸载 | 全局对象上的方法与属性、插件导出的函数、平台服务对象 | 从全局对象删除属性，引用计数归零后释放 |
| 可停用 | TJS 原生类实例 | 从全局对象摘除，保留类标识 |
| 不可卸载 | 脚本引擎本体、原生类注册表、全局字符串映射 | 进程内一次性初始化 |

约束：

1. 插件加载时注册的原生类只注册一次；同一进程内重复加载复用既有条目，不得重复注册。
2. 模块对外只暴露契约接口；新增模块需在 `core/module` 登记名称、依赖与初始化顺序。
3. 模块初始化顺序由注册表解析，不允许在构造函数中隐式完成跨模块调用。

## 5.1 能力接口

宿主能力按用途拆成 `core/contracts/ScriptServices.h` 中的小接口（启动参数、输出、退出、
平台信息、路径、窗口）。`HostServices` 用空指针表达"该能力未提供"，桥接层必须显式失败，
不得用默认值代替。宿主（`core/api`、测试台）实现接口集合，`shim` 只读。

## 5.2 兼容声明

"引擎声称支持什么"由 `engine/CompatProfile` 的 `Profile` 表达：预处理器值
（如 `kirikiriz=0`）、原生类列表、脚本前奏。脚本因此走对应分支，而不是在运行期逐个探测。
新增兼容行为时改 profile，不写进 `shim` 的 `if`。

## 6. 新增或修复功能的固定流程

1. **判定归属**：功能属于 `core`、`platform`、`shim` 还是插件。
2. **先补下层**：`shim` 需要的引擎能力，必须先在 `core`（或 `platform`）以契约形式实现；
   不允许在 `shim` 内部临时实现本应属于引擎的逻辑。
3. **再写桥接**：`shim` 只做"TJS 调用 → 契约调用"的翻译、编码转换与错误映射。
4. **失败显式化**：能力未实现时返回失败并记录日志，禁止返回占位值；过渡期占位必须以
   `TODO(<步骤>):` 标注并登记到 `todo.md`。
5. **补测试**：`host/` 下增加脚本级断言（正常路径与错误路径各一），并运行现有三个测试台。
6. **更新文档**：接口或优先级规则变化时同步 `docs/` 与 `todo.md`。

## 7. 编码约定

1. 层内文本统一 UTF-8；`tjs_char` 的宽度差异只在 `shim/` 出现。
2. 异常不得穿过层边界：跨层接口以返回值与错误文本表达失败。
3. 注释遵循 `AGENTS.md` §11：书面语、适量、无分隔线、不复述代码。
4. 头文件只包含本层及其下方层的头；跨层包含视为缺陷。
5. **命名**：抽象层用抽象层的名字（`pixel::LayerOp`、`krkr::io::Storage`），不要用上游前缀
   （`TVP`/`tTVP`/`iTVP`）给新东西命名 —— 那些前缀只属于 `vendor/` 里的上游代码。
   文件名与职责一致（`LayerOps.h` 是词汇表、`Blend.cpp` 是混合映射），不出现
   `Impl`/`Compat`/`Util` 这类无信息后缀。

## 8. 静态检查与构建

```bash
bash cpp_new/tools/run_probes.sh          # CMake 配置 + 构建 + CTest 全部断言
python cpp_new/tools/check_layer_deps.py cpp_new
```

CMake 目标与分层一一对应：`krkr_tjs` / `krkr_tjs_host` / `krkr_core` / `krkr_platform` /
`krkr_shim` / `krkr_engine`，测试台链接 `krkr_engine`（产品入口与测试台共用同一段装配）。

`tools/check_layer_deps.py` 校验第 3 节的依赖方向（含 `vendor/` 规则），作为提交前的强制检查。
违规输出现式形如 `core/io/Storage.cpp: include "shim/..." 违反分层（core 不得引用 shim）`。

## 9. 诊断与调试

分层之后，"出错时能不能定位"是独立的设计对象。约束：

1. **失败必须带上下文**：跨层返回值以"码 + 文本"表达；脚本异常统一经
   `shim/TjsDiagnostics` 格式化为 `调用上下文 + 消息 + [script=块名 line=行号 trace=调用链]`。
2. **不得吞异常**：桥接层只记录上下文，随后原样重抛，由虚拟机转成脚本侧异常并保留 trace。
3. **空消息视为缺陷**：TJS2 的内置消息表在 vendored 源码里为 NULL，必须由垫层安装
   （`tools/gen_tjs_message_map.py` 生成），不允许出现无消息异常。
4. **脚本入口唯一**：所有脚本装载走 `shim/engine/ScriptLoader`（解码、字节码、异常映射集中一处）。
5. **测试台与产品同源**：装配只在 `engine/ScriptRuntime`；测试台不得自行拼装引擎，否则装配缺失
   无法被回归发现。
