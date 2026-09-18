# 兼容层（compat）

本目录是**兼容层**的家：两套兼容层的边界与选择规则、单一 IO/加载组件的约束、从上游移植
文件的溯源清单。本文只写当前事实、约束、验收标准，不写推理过程与个人验证记录。

## 1. 两套兼容层

| 层 id | 名称 | 何时生效 | 行为来源 |
|---|---|---|---|
| `krkr2-classic` | 旧版 krkr2 层 | **缺省**（`krkr2next.json` 没有 compat 字段时） | 本仓库既有行为（PocketKrKr / KrKr2-Next 血脉） |
| `aetherkiri` | AetherKiri 层 | 按游戏显式开启（设置里的运行模式，或 `krkr2next.json` 的 compat 字段） | AetherKiri（`compat/upstream/aetherkiri_ports.json` 里钉住的 rev） |

选择链路（现状 + 本次接线）：

```
krkr2next.json（每游戏 GameConfig.EngineOverride.runMode）
  → 壳 RunMode（compatProfile + oglDrawDeviceCompat 两个原始值）
  → 引擎选项 game_compat_profile / game_compat_game_root（engine_options.h）
  → 引擎内 CompatLayer 激活（cpp/core/compat）
```

约束：

- **一个进程只激活一层。** 同一个 Windows 插件名被两层各自模拟时，只有激活层的注册生效；
  非激活层的模块注册与钩子一律不跑（否则同名 TJS 类/全局会被注册两次）。
- 缺省必须是 `krkr2-classic`：AetherKiri 层是新增代码路径，未真机回归前不能默认生效。
  现有的 `auto` 判档（按游戏目录标记判 `krkrz-gpu` / `krkrz-kag` / `krkrz-ogl`）继续只决定
  **渲染侧**选项（`ogldrawdevice_compat`），**暂不**据此切换兼容层；等 AetherKiri 层回归完
  成后再把 `auto` 扩到层选择。
- 层的差异必须收敛成"具名 + 带版本号"的东西（沿用 `CompatProfile` 的做法），真机日志能看出
  用的是哪一层、哪一版口径。

### 1.1 层专属模块与模块归属门（M1.6 起步）

同一个 Windows 插件名被两层各自模拟时，**只有激活层注册**。实现：`cpp/core/compat/ModuleGate.{h,cpp}`
+ `ncbind` 注册前查询。规则保守——**未登记归属的模块一律放行**（旧层现状不受影响），只有显式登记的
层专属模块会在层不匹配时被跳过并打一条 info。

已登记归属（截至 2026-09-18）：

| 模块 | 归属层 | 实现 |
|---|---|---|
| `zlib.dll` | aetherkiri | `cpp/plugins/compat/aetherkiri/legacy_zlib_version.cpp`（自 AetherKiri 片段移植） |
| `version.dll` | aetherkiri | 同上 |

新增层专属模块时：写实现 → 在文件里 `RegisterModuleOwner(name, LayerId::Xxx)` → 登记进
`compat/upstream/aetherkiri_ports.json`（片段移植用 `partial-extract` 类别）→ 跑
`python3 scripts/check_port_drift.py --update`。

## 2. 依赖方向与隔离（不变量）

```
app/ ──→ bridge/engine_api ──→ cpp/core/compat        （层框架：选择、激活、钩子）
                                    │
                                    ├──→ 层实现：旧版 krkr2 层 / AetherKiri 层
                                    │
                                    └──→ cpp/core/io  （单一 IO/加载组件）
                                              ↑
              cpp/core/*、cpp/plugins/*（既有消费者，公开 API 不变）
```

- `cpp/core/io/` **不得**包含任何具体层、具体游戏或具体发行版的知识；它只认"媒体提供者
  注册表、挂载表与排序策略、路径解析、归档后端"这些抽象。
- 层实现**不得**各自实现挂载、路径解析、归档打开、auto-path 排序；一律经 `cpp/core/io/`
  的接口。层能贡献的只有：媒体提供者、挂载项、排序策略、脚本前奏、模块注册集合。
- `cpp/core/base/StorageIntf.h` 的既有公开 API（`TVPCreateStream` / `TVPGetPlacedPath` /
  `TVPRegisterStorageMedia` / `TVPNormalizeStorageName` …）**签名与行为保持不变**，作为门面
  继续服务既有调用方（当前 23 个文件用 `TVPCreateStream`、11 个用 `TVPIsExistentStorage`）。
  迁移期间门面只做转发，不允许顺手改语义。

## 3. IO 组件边界（组件 → 现状位置 → 迁移后）

| 组件 | 现状位置（文件:行） | 迁移后 | 对外接口（示意） |
|---|---|---|---|
| 媒体注册表 | `cpp/core/base/StorageIntf.cpp:192-527`（`tTVPStorageMediaManager`、`TVPRegister/UnregisterStorageMedia`） | `cpp/core/io/IoMedia.*` | `IoRegisterMedia(iIoMediaProvider*)` |
| 路径解析 | `StorageIntf.cpp:241-470`、`:801-925`（normalize / extract / chop） | `cpp/core/io/IoPath.*` | 现有 `TVP*` 函数转发 |
| 挂载表与 auto-path | `StorageIntf.cpp:926-1200`、`impl/StorageImpl.cpp:1512-1852` | `cpp/core/io/IoMount.*` | `IoMountArchive()` / `IoAddAutoPath()` / `IoRebuildAutoPathTable(policy)` |
| 归档工厂与后端 | `StorageIntf.cpp:32-101`（`tTVPArchive`）、`:569-751`、`XP3Archive/ZIPArchive/7zArchive/TARArchive` | `cpp/core/io/IoArchive.*` + 后端注册 | `IoRegisterArchiveBackend()` / `TVPOpenArchive()` 转发 |
| 流与本地文件 | `StorageIntf.cpp:1277-1313`、`impl/StorageImpl.cpp`（`tTVPLocalFileStream`、`TVPCheckExistentLocalFile/Folder`、`TVPCreateFolders`） | `cpp/core/io/IoStream.*`、`cpp/core/io/IoLocal.*` | 现有 `TVP*` 函数转发 |
| TJS 门面 | `StorageIntf.cpp:1347-1517`（`tTJSNC_Storages`） | `cpp/core/io/IoStoragesTJS.*` | 脚本侧 `Storages` 表面不变 |

排序策略（`patch.xp3` 优先级等）在 M1.3 变成策略对象：`IMountPolicy` 由激活层提供，
`krkr2-classic` 用现有行为，AetherKiri 层用其行为（差异见 §5，逐条待裁决）。

## 4. 阶段计划

| 阶段 | 内容 | 行为变化 | 验收 |
|---|---|---|---|
| **M1.1** | 新增 `cpp/core/io/` 骨架与接口；`StorageIntf/StorageImpl` 只做转发 | 无 | CI 绿 + 千恋万花/NEKOPARA 启动日志与迁移前逐行一致 |
| **M1.2** | 迁路径解析 + 媒体注册表 | 无 | 同上 + `lzfs://` / `psb://` 打开日志不变 |
| **M1.3** | 迁 auto-path/挂载 + 排序策略接口（策略暂等于现状） | 无（策略切换留到 M1.6） | auto-path 队首与 patch 优先级日志不变 |
| **M1.4** | 迁归档工厂与后端注册 | 无 | XP3/ZIP 打开与缓存行为不变 |
| **M1.5** | TJS `Storages` 门面归位 | 无 | 脚本侧 `Storages.*` 可用性清单不变 |
| **M1.6** | 层框架接线：`game_compat_profile` → 层激活；模块注册按层过滤 | 仅显式选 AetherKiri 层时 | 两层各自可用；缺省游戏行为不变 |

> **已落地（2026-09-18）**：
> - `cpp/core/io/StoragePolicy.h`：策略契约（tie-break / 补丁规则 / 优先级落点 / 包内根目录顺序 /
>   缺分隔符修复 / app path 语义 / 档案工程兄弟挂载 / XP3 段缓存预算）。
> - `cpp/core/compat/{CompatLayer.h,CompatLayer.cpp}` + `core_compat_module`：层注册表、两层各自的
>   策略取值、`SetActiveLayer*/ActiveStoragePolicy()`。
> - 引擎档：新增 `aetherkiri`（`engine_options.h`）；`ApplyCompatProfileLocked()` 解析后调用
>   `SetActiveLayerByName()`，`auto` 与其余档一律保持旧层；壳侧 `RunMode.AETHERKIRI` 可逐游戏选择。
> - 边界：策略**只登记取值**，尚未接到 `StorageIntf/StorageImpl` 的实现（M1.2–M1.6 的工作）；
>   未接通前对运行时行为零影响。
> - `cpp/core/io/`（目标 `core_io_module`）现持有存储系统的**实现**：
>   - `IoPath.cpp`：`TVPArchiveDelimiter` 定义 + `TVPExtractStorageExt/Name/Path`、`TVPChopStorageExt`；
>   - `IoStorage.cpp`（原 `base/StorageIntf.cpp`，git mv）：媒体注册表、名字规范化、
>     auto-path 表与放置路径搜索、`TVPCreateStream`、TJS `Storages` 门面；
>   - `IoStorageLocal.cpp`（原 `base/impl/StorageImpl.cpp`，git mv）：本地文件媒体、
>     归档打开、启动挂载（兄弟/工程 xp3）、补丁优先级。
>   公开声明仍在 `cpp/core/base/StorageIntf.h` 与 `base/impl/StorageImpl.h`，约 200 个调用点
>   一行未改（"稳定接口 + 实现归位"）。
> - 依赖是**双向过渡态**：`base -> io`（存储 API 的实现）与 `io -> base`（归档工厂
>   XP3Archive/ZIP/7z/TAR、消息常量、`TVPCreateFileMedia` 之外的核心设施）。两者都是
>   STATIC 库，CMake 会在最终链接时重复它们（CI 35340472264 实证通过）。**回边必须在
>   M1.4（归档工厂搬入 io）后消失**——那是本组件"单一性"的验收点。
>
> **IO 搬迁顺序（后续轮次按此执行，每步都要求行为等价 + CI 绿）**：
> 1. ✅ 媒体注册表 + `tTVPFileMedia` + 本地文件流（`IoStorage.cpp` / `IoStorageLocal.cpp`）。
> 2. ✅ auto-path 表与放置路径搜索、`TVPCreateStream`（同上）。
> 3. ✅ 归档工厂与后端（`tTVPArchive` + `TVPOpenArchive` 的创建者表 + `XP3Archive*`/
>    `ZIPArchive`/`7zArchive`/`TARArchive` + `tar.h`）→ io。
>    **口径修正**：`io -> base` 的回边**不会归零**（也不该按"消失"验收），它现在只剩
>    **基础设施**：`MsgIntf`（消息常量）、`UtilStreams`（流基类）、`TVPMmapAlloc`、
>    `TickCount`/`Random`/`StringUtil`/`FilePathUtil`/`Platform`、`ncbind`（插件注册表，
>    见下条）、`Application.h`/`WindowImpl.h`（进程级宿主）。验收口径应为：
>    **IO/归档/挂载/路径的知识全部在 io 内，base 侧不再有这些实现**。
> 4. ✅ TJS `Storages` 门面（随 `IoStorage.cpp` 一起搬入）。
> 5. 🟡 `StoragePolicy` 已接到实现上（M1.3，`io/IoPolicy.{h,cpp}` 注入点 + compat 侧注入）。
>    **已接线**：auto-path 表 tie-break、缺分隔符自动修复、重复 addAutoPath 语义、
>    补丁判定规则（子串 / 前缀+序号）、补丁优先级落点（队首 / 队尾）、`TVPGetAppPath()`
>    的 '>' 剥离。
>    **未接线（明确标注）**：`xp3SegmentCacheBytes`（XP3Archive.cpp 里是静态初始化期定型的
>    全局，需"策略变更通知"或惰性计算，代码内留 TODO）、`archiveRoot`（包内根目录先/后）、
>    `mountSiblingsForArchiveProject`（档案工程挂兄弟 patch*.xp3，与 I3 一并处理）。
> 6. ✅ 模块存在性查询改注入式：`io/IoModuleLocator.{h,cpp}`（`SetModuleLocator`/`HasModule`），
>    `core/plugin` 在 `TVPLoadInternalPlugins()` 的 `AllRegist()` **之前**注入
>    `ncbAutoRegister::HasModule`（注入前 io 返回 false = "注册表还没填充"，行为不变）。
>    io 源码现在不再 include 插件头、也不引用插件注册表符号（可用
>    `grep -rn "ncbind\|ncbAutoRegister" cpp/core/io/` 复核）。

| **M2** | 层 A：TJS2 内核兼容读写（未定义全局回退 + 启动期可写白名单） | AetherKiri 层内 | 见 §5 清单 |
| **M3** | 层 B：KAGWindow / krkrgles 脚本别名与绘制设备接管 | AetherKiri 层内 | 见 §5 清单 |
| **M4** | 层 C：KAGParser / extkagparser / kagparserex 行为对齐 | AetherKiri 层内 | 见 §5 清单 |
| **M5** | 层 E：启动与资源加载顺序对齐 | AetherKiri 层内 | 见 §5 清单 |
| **M6** | 层 D：插件模拟层全量移植（compatLegacy/MediaLayer/System + AetherKiri 独有插件） | AetherKiri 层内 | 见 §5 清单 |

## 5. 差异清单（逐条待裁决）

> 裁决记录（2026-09-18，用户确认）：
>
> | 议题 | 裁决 |
> |---|---|
> | A 块 TJS2 内核回退（A1/A2/A4） | **只给 AetherKiri 层开启**；A3 白名单（17 行）两层都移植 |
> | C 块解析器 | **C1 `taglist`+`copyTag` 两层都要**；**C3 `GetNextTag` 文本段聚合只给 AetherKiri 层**（classic 层存档/断点语义不变）；C4 成对移植 |
> | E1 `patch.tjs` 时机 | **分两层**：classic 保持 startup 之前；AetherKiri 层照搬 REF（含晚 patch 韧性层） |
> | I2 首次读 `startup.tjs` 被原版压住 | **在 classic 层修**（补丁包的 startup.tjs 第一读就应生效） |
> | B2 别名扇出/重试/卸载清理、P1 注册失败回滚、P3' k2compat 死代码 | **本轮顺带做** |
>
> 下面各表的"状态"列在对应改动落地后改为"已实施 / 已否决"。


> 证据见 `compat/recon/io-loading-diff.md`（IO/加载）与 `compat/recon/plugin-compat-diff.md`（插件层）。
> 列 = 项 / 我们的行为 / AetherKiri 行为 / 影响面 / 建议 / 状态（待裁决 / 照搬 / 保留 / 并存开关）。

### 5.1 IO 与加载（M1、M5）

| # | 项 | 我们 | AetherKiri | 影响 | 建议 | 状态 |
|---|---|---|---|---|---|---|
| I1 | auto-path 表优先级语义 | **先注册者优先**（`StorageIntf.cpp:1079-1081/1106-1108`，为某发行版诱饵 startup.tjs 而做） | **后注册者优先**（= 上游语义，`StorageIntf.cpp:1705/1741/1772` + `tjsHashSearch.h:246-254`） | 极高：决定同名资源谁生效 | 收进 `IAutoPathPolicy::Tie()`：两层各自保持；**不做全局统一**（统一=改所有游戏行为） | 待裁决 |
| I2 | 首次读 `startup.tjs` 时补丁层是否生效 | **不生效**：boost 在 startup 之后（`Application.cpp:432` vs `:434`），且 data.xp3 先挂载 + 先注册者优先 | **生效**：挂载阶段就按补丁名排序（`StorageImpl.cpp:1750-1763`）+ 后注册者优先 | 极高：汉化/整合补丁的 startup.tjs 可能被原版压住 | 在 `krkr2-classic` 层内**修**（把 boost 提到 startup 之前，或让挂载排序认补丁）；这是行为变更，需要你确认并逐游戏回归 | 已实施（classic 层：boost 提前到 startup 之前） |
| I3 | 档案工程启动（`.../data.xp3>`）时兄弟 `patch*.xp3` | **从不挂载**（两个挂载函数都在非 `/` 结尾时提前返回） | 扫父目录、挂兄弟、再挂被选中档案（`StorageImpl.cpp:1770-1791`） | 高：xp3 直启的补丁包失效 | 照搬 AK 的档案工程覆盖逻辑（对 classic 层是新增能力，风险中） | 待裁决 |
| I4 | `TVPGetAppPath()` 是否去 `>` | 不去（档案工程返回 `.../data.xp3>`）⇒ `patch.tjs` 在包内找、临时文件会试图写进包 | 去 `>` 取父目录（`StorageImpl.cpp:627-635`） | 高：`patch.tjs`/`AfterStartup.tjs` 定位与临时文件位置 | 做成 `Config` 开关，按层取值；先不统一 | 待裁决 |
| I5 | 缺尾部分隔符的路径 | **抛异常** `TVPMissingPathDelimiterAtLast` | `FixMissingPathDelimiter` 自动补 + info 日志 | 中：脚本传不规范路径时老层直接失败 | 收进 `IAutoPathPolicy::FixMissingDelimiter()`；AetherKiri 层开启 | 待裁决 |
| I6 | 补丁判定规则 | 路径子串含 `patch`（`unpatched.xp3` 也中） | 名字前缀 `patch`+数字/具名、或结尾 `patch`（`StorageImpl.cpp:1579-1619`） | 中：误判会打乱优先级 | 收进策略；AetherKiri 层用 AK 规则，classic 层保持 | 待裁决 |
| I7 | 包内目录条目顺序（根目录先/后） | 根目录**先** | 根目录**后**（避免 `tools/startup.tjs` 遮住包根） | 中 | 收进 `IMountPolicy::OrderRootLast()` | 待裁决 |
| I8 | XP3 v3 `hnfn` 真实文件名 | **缺失** | 有（`XP3Archive.cpp:402-604`） | 中：部分 v3 包文件名解析不到 | **两边都要**（格式能力，不是策略）：把 AK 的 `hnfn` 实现并入我们的 XP3 解析器 | 待裁决 |
| I9 | XP3 诱饵头基点扫描 | **仅我们有**（`XP3Archive.cpp:235-278`，`nainiuniu5krkr.xp3` 案例） | 无 | 中 | 保留（并作为 XP3 provider 的能力保留） | 待裁决 |
| I10 | XP3 段缓存上限 | 1 MiB（`XP3Archive.cpp:840`） | 256 MiB（`XP3Archive.cpp:785`） | 中：读大包性能 | 变成 `Config` 数值，按层或按设备内存取值 | 待裁决 |
| I11 | 每局文本读编码复位 | 无 | `TVPSetDefaultReadEncoding("utf-8")`（`ScriptMgnIntf.cpp:4018`） | 中：上一局 `setTextEncoding` 可能泄漏到下一局 | 照搬（对 classic 层也是修 bug） | 已实施 |
| I12 | `Storages` 表面 | 缺 `addAutoToolsPath` / `addArchive` / `isExistentStorageNoSearchNoNormalize` / `archiveUniqueKey` | 有 | 中：部分脚本读不到 | 照搬（纯新增，兼容） | 部分实施（已补 isExistentStorageNoSearchNoNormalize；其余待做） |
| I13 | 存储媒体 | `file/proxy/psb/psd/var/lzfs` | 另有 `arc`(PackinOne) / `mem` / `zip` | 中 | `arc`/`mem`/`zip` 各自归其层注册；先补**归属与冲突报告**（重名注册静默保留旧实例，注销按名字删） | 待裁决 |
| I14 | `patch.tjs` 执行时机 | startup **之前**（与 krkr2/Kirikiroid2/PocketKrKr/KrKr2-Next 一致） | startup **之后**（注释说明是有意偏离） | 高：补丁能否改写框架 | classic 层保持现状；AetherKiri 层照搬（含全局可调用成员快照/恢复） | 待裁决 |
| I15 | `plugin_load_mode` | 无（启动注册全部内置模块） | 默认只加载 `xp3filter/varfile/shrinkCopy`(+krkrgles)，`aether_all` 才全量 | 中高：影响启动期可见模块集合 | 作为层的配置项引入；classic 层保持"全量" | 待裁决 |

### 5.3 KAG 脚本层 / 内核层（M2、M3、M4、M5）

证据见 `compat/recon/kag-script-diff.md`。溯源结论：`KAGParser.cpp` 我们与 PocketKrKr/KrKr2-Next 语义一致
（**= 上游基线**），AetherKiri 是同基线 + 724 行新增 ⇒ 解析器层"REF 是严格超集"。

| # | 项 | 我们 | AetherKiri | 建议 | 状态 |
|---|---|---|---|---|---|
| A1 | 未定义全局成员读取回退（34 个名字，`tjsObject.cpp:249-308`） | 没有，一律抛 `Member "x" does not exist` | 有（白名单式，失败行为不变；带 `thread_local` 重入保护） | 移植，**按层开关**（会改变当前"抛异常"的游戏行为） | 待裁决 |
| A2 | 启动名回退（11 个 no-op 函数 + 数组/整数/类，`tjsObject.cpp:171-247`） | 没有 | 有 | 同 A1 | 待裁决 |
| A3 | 启动期可写白名单（8 个名字，`tjsObjectExtendable.cpp:9-19/96-105`） | 没有（靠 C++ 侧 `krkrgles.cpp:3254-3256` 绕开） | 有 | 移植（固定白名单，风险低）⇒ 让脚本侧 `Window.OGLDrawDevice = X` 生效 | 待裁决 |
| A4 | `TextRender.renderCount` / `touchImage` 合成 | 没有 | 有 | 随 A1/A2 一起移植 | 待裁决 |
| A5 | `kag.*` 六个默认值 | 用 `kag_runtime_defaults.tjs` 注入（等价） | 内核回退返回 0 | 保持我们的实现 | 待裁决 |
| B1 | GPU 伴生脚本注入方式 | 引擎选项 `ogldrawdevice_compat` 门控 + 首帧一次性钩子 | `TVPRegisterStorageResolver` + 惰性打开（打开 11 个 GPU 存储名时注入，无条件） | 保留我们的门控；把"惰性注入"作为 AetherKiri 层行为可选引入 | 待裁决 |
| B2 | `KAGWindow`/`kag` 别名扇出 + 600-tick 重试 + 卸载清理 | 只写 `Window.<name>`；无重试；无 unregist 清理 | 三目标扇出 + prototype + 重试 + `PreUnregist` | 照搬（对 classic 层也是修"脚本晚加载就失效"） | 已实施（4 目标扇出 + 每帧重试 600 帧 + 卸载摘钩） |
| C1 | `taglist` 标签元数据 + `copyTag`（约 140 行） | 没有 | 有（KAGParserEx 文档化特性） | 移植（自包含、风险最低） | 待裁决 |
| C2 | 明文行翻译（`TVPTransformText`/`PrefetchText`） | 没有 | 有（依赖 REF 独有 `utils/TextTransform.h`） | 暂不移植（本仓库无翻译功能） | 待裁决 |
| C3 | `GetNextTag` 文本段聚合 + `TextTagQueue` | 没有（`return _GetNextTag()`） | 有（会改变 `kag.curLine/curPos` 与存档位置语义） | **按层开关**：AetherKiri 层启用，classic 层保持（存档兼容） | 待裁决 |
| C4 | `.scn` 编译场景容错 + 标签解析回调（含 psbfile 侧标签收集） | 没有 | 有（成对实现） | 移植（成对，否则回调无消费者） | 待裁决 |
| C5 | 每帧 KAG 修复（`envclear` 环境复位、`[endtrans]` 无 trans 等待） | 没有 | 有（`EngineLoop` tick 钩子 + `ScriptMgnIntf` 实现） | 移植到 AetherKiri 层 | 待裁决 |
| C6 | KAG 运行时补丁层（27 个文本补丁 + 11 个类包装，约 1500 行） | 没有 | 有 | **不整体照搬**：其前提是"patch.tjs 在 startup 之后"，与我们的顺序相反；按游戏逐条摘 | 待裁决 |
| C7 | `ExtKAGParser`（第二个解析器，约 4700 行） | 只有空壳注册名 | 有（含 `goToLine`/`localvar`/`fuzzyReturn` 等） | 延后；移植前必须改 `ExtKAGParser.hpp` 的 `KAGParserH` 保护宏并决定 `paramMacros`/`copyTag` 缺失 | 待裁决 |
| C8 | `kagparserex` 真实实现（109 行：按需安装核心 `KAGParser` + 标记 + 引用计数卸载） | 25 行空实现 | 有 | 照搬（便宜） | 待裁决 |
| C9 | `MDKParser.dll`（4470 行，MIT） | 没有 | 有 | 延后（M6） | 待裁决 |
| E1 | `patch.tjs` 执行时机 | startup **之前**（与 krkr2 全家一致） | startup **之后** + 晚 patch 韧性层（全局可调用成员快照/恢复、运行时注册表合并） | classic 层保持；AetherKiri 层照搬（**必须连韧性层一起**，否则更糟） | 待裁决 |
| E2 | `kag` 默认值在 `AfterStartup.tjs` 之后是否再补一次 | 只补一次 | 补两次 | 照搬（便宜，且是修 bug） | 已实施 |
| E3 | 模块名注册不按链接序（`LoadAllModules` 按字母序遍历 map） | 同 | 同 | 引入层过滤时一并解决（同层内仍按字母序） | 待裁决 |
| P3' | `k2compat_scripts.cpp`（2359 行 Krkr2Compat TJS，**当前是死代码**：无 target 编译、安装函数零调用） | 有资产未接线 | 完全没有 | 接线（放 classic 层，注意历史上执行它会黑屏，需按 PreRegist/PostRegist 时机接）或明确删除 | 已实施（同上） |

### 5.2 插件模拟层（M6）

| # | 项 | 我们 | AetherKiri | 建议 | 状态 |
|---|---|---|---|---|---|
| P1 | 注册失败回滚 | **无**（一个 registrar 抛异常会中断其后所有模块） | 逐条回滚 | 照搬（对 classic 层也是修 bug） | 已实施（逐条隔离 + 失败不写已注册表） |
| P2 | 模块别名机制 | 无 `NCB_REGISTER_MODULE_ALIAS` | 有 | 照搬机制，用来给 `DrawDeviceD2D.dll`/`DrawDeviceD2Dm.dll` 之类建别名 | 已实施（NCB_REGISTER_MODULE_ALIAS） |
| P3 | `k2compat_scripts.cpp`（2359 行 TJS） | **没被任何 target 编译、零调用**（死代码） | — | 先接线或先删除，二选一 | 已实施（编译进目标 + k2compat_scripts 选项门控 + 框架就绪后安装） |
| P4 | 缺失模块（约 60 个） | — | `zlib/version/process/shellExecute/systemEx/stdio/httprequest/msdfrender/layerExSave/…` | 按"脚本真的会调"排序分批移植：先 `zlib`/`version`/`systemEx`/`stdio`/`process`，再 `layerExSave`/`msdfrender` | 待裁决 |
| P5 | 部分覆盖（`layerExSave`/`textrender` 属性形状/`menu.MenuItem`/`csvParser` 注销/`DrawDeviceD2D` 规范名） | 见 `recon/plugin-compat-diff.md §2` | — | 逐条补齐；`textrender.renderDelay/renderOver` 必须改回只读属性（否则调用方无限重试） | 部分实施（DrawDeviceD2D.dll 规范名已用别名补上；textrender/菜单等待做） |
| P6 | 阻塞式实现（popen/curl/httpserv 无超时） | — | 有 | 移植时必须改成非阻塞或加超时；脚本线程即 EGL 线程 | 待裁决 |


## 6. 移植溯源规则

- 从 AetherKiri 移植的文件必须登记进 `compat/upstream/aetherkiri_ports.json`，写明
  `modifications`（`none` / `api-shim` / `bridge-substituted` / `local-fix`）与理由。
- 改动移植文件后运行 `python3 scripts/check_port_drift.py --update` 更新哈希，否则校验失败；
  这是刻意的：漂移必须被显式承认。
- 上游 rev 变更（`upstream.rev`）需要单独一轮同步，不与功能改动混在一起。
