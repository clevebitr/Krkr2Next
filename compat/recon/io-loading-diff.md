# 对照证据：文件 IO 与加载逻辑（KiriNext vs AetherKiri）

> 本文件是**证据**，不是规范：记录 2026-09-18 那次逐文件对照的结论，供 §5 决策表与后续
> 实现引用。路径缩写 `KN/` = `KiriNext/cpp`，`AK/` = `AetherKiri/cpp`。所有条目带行号，
> 复核时以行号为准（行号会随改动漂移，复核请重新定位函数名）。

## 1. 启动序列（引擎起来 → 游戏 startup.tjs 执行）

KN：`EngineLoop::StartupFrom`(KN/core/environ/EngineLoop.cpp:137) → `Application::StartApplication`(Application.cpp:325)
→ `TVPBeforeSystemInit`(core/base/impl/SysInitImpl.cpp:158) → `tvpLoadPlugins()`(Application.cpp:378)
→ `TVPSystemInit()`(:382) → `TVPAutoMountSiblingXP3Archives()`(:421) → `TVPAutoMountProjectXP3Archives()`(:428)
→ `TVPInitializeStartupScript()`(:432) → `TVPBoostAutoMountPaths()`(:434)。

AK：同前段，但 `TVPBeforeSystemInit`(AK/core/base/impl/SysInitImpl.cpp:167) 会额外 `TVPAddAutoPath(项目目录)`
（:185/:193），插件加载按 `plugin_load_mode` 分支（AK/core/plugin/PluginImpl.cpp:57/737-762），
挂载只有一个函数 `TVPAutoMountSiblingXP3Archives()`(Application.cpp:431) → startup(:435) → boost(:437)。

`TVPExecuteStartupScript` 内的差异（KN/core/base/ScriptMgnIntf.cpp:1037 vs AK:4013）：

| 步骤 | KiriNext | AetherKiri |
|---|---|---|
| 读编码复位 | 无 | `TVPSetDefaultReadEncoding("utf-8")`（AK:4018，避免上一局 `setTextEncoding` 泄漏） |
| `patch.tjs` 时机 | **startup.tjs 之前**（KN:1048-1050，与 krkr2/Kirikiroid2/PocketKrKr/KrKr2-Next 一致） | **startup.tjs 之后**（AK:4144-4205，注释说明是有意偏离），并包了全局可调用成员快照/恢复与 patch 运行时注册表合并 |
| startup 失败 | 仅探针下记录（KN:1143-1175） | 分类型 catch + 日志 + 回落 `system/Initialize.tjs`（AK:4079-4126） |
| 之后的钩子 | `TVPInstallKagRuntimeDefaults` + `TVPInstallPatchWindowPrerequisites` + `AfterStartup.tjs` | 同上 + `TVPInstallKagNoTransWaitRepairHelper` + `TVPInstallKagLoadContractGuard` + `TVPRunPostStartupScriptHooks`（AK:4138-4216） |

`Config.tjs` / `override.tjs`：两边 C++ 都没有定位逻辑（都是 TJS 层）。引擎定位的脚本名只有
`msgmap.tjs`、`patch.tjs`、`startup.tjs`（可被 `-startup` 覆盖）、`system/Initialize.tjs`、`AfterStartup.tjs`。

## 2. 行为差异表

### 2A. auto-path 优先级（**两边语义相反**）

- **KN = 先注册者优先**：建表时 `if(!TVPAutoPathTable.Find(sname)) Add(...)`（KN/core/base/StorageIntf.cpp:1079-1081、
  1106-1108，注释在 :1016-1032 说明是为某个发行版的诱饵 `startup.tjs` 而做）；`TVPAddAutoPath` 只追加不挪位
  （KN:953-969）⇒ 新加的路径优先级**最低**。
- **AK = 后注册者优先（上游语义）**：`TVPRebuildAutoPathTable` 无条件 `Add`（AK:1705/1741/1772），
  哈希表同键覆盖（`tjsHashSearch.h:246-254`，两树逐字节相同）；`TVPAddAutoPath` 先删同值再 `push_back`
  （AK:1619-1660）；`TVPFindExactArchiveAutoPath` 反向遍历并注明 "last-added-path-wins"（AK:1520-1523）。
- 因此两边的 `TVPBoostAutoMountPaths` 方向相反：KN 把补丁档案插到第一条档案条目**之前**（KN/impl/StorageImpl.cpp:1818-1831），
  AK 对补丁档案重新 `TVPAddAutoPath` 即挪到**队尾**（AK/impl/StorageImpl.cpp:1794-1806）。
- **后果（最高影响项）**：KN 的 boost 在 startup 之后（Application.cpp:434），而 `data.xp3` 先于 `patch.xp3` 挂载，
  先注册者优先 ⇒ **第一次读 `startup.tjs` 时补丁层压不住原版**；AK 靠"挂载阶段就按补丁名排序"（AK:1750-1763）
  让 `patch*.xp3` 后挂载，后注册者优先 ⇒ 补丁的 `startup.tjs` 第一读就赢。

补丁判定规则对照：

| 维度 | KiriNext（StorageImpl.cpp:1794-1802） | AetherKiri（StorageImpl.cpp:1579-1619） |
|---|---|---|
| 生效时机 | 只在 startup 之后的 boost | 挂载时排序 + boost 各一次 |
| 匹配 | 去 `>` 后的路径里**子串**含 `patch` | 文件名（去 `.xp3`）前缀 `patch`+纯数字→序号；`patch`+非数字（patchAI/patch_data1080）→具名；以 `patch` 结尾→具名 |
| 误判 | `unpatched.xp3`、`spatchcock.xp3` 也算 | 不算 |
| 补丁之间 | 保持挂载顺序，配合先注册者优先 ⇒ **序号小的赢** | 序号升序 + 具名最后，配合后注册者优先 ⇒ **序号大的/具名的赢** |
| 非补丁档案 | 不动 | 不动（但后注册者优先 ⇒ 后挂载的赢） |

### 2B. 挂载扫描范围与顺序

| 维度 | KiriNext | AetherKiri |
|---|---|---|
| 函数 | `TVPAutoMountSiblingXP3Archives`(:1516) + `TVPAutoMountProjectXP3Archives`(:1665) | 只有 `TVPAutoMountSiblingXP3Archives`(:1679)，内部按工程类型分支 |
| 工程目录 xp3 | 独立函数扫工程目录并注册包内每个目录前缀（:1712-1759），调用在 startup 之前（Application.cpp:428） | 同一函数内 `directoryProject → 扫 TVPProjectDir`（:1695-1696） |
| **档案工程启动**（`.../data.xp3>`） | 两个函数都因 `TVPProjectDir` 不以 `/` 结尾而提前返回（:1522/:1666）⇒ 兄弟 `patch*.xp3` **从不挂载** | 扫 xp3 所在父目录、跳过被选中的 xp3（:1770-1774）、把兄弟档案按补丁优先级挂上，再挂被选中的档案（:1782-1791） |
| 同名镜像 xp3 | 兄弟扫描会跳过与工程目录同名的 xp3（:1581-1585）；工程扫描不跳 | 只在档案工程时跳过（:1770）⇒ 目录工程下同名镜像会被挂载 |
| 排序 | `std::sort` 字节序，不认补丁（:1591/:1703） | 非补丁 → 数字补丁 → 具名补丁，再按小写字典序（:1750-1763） |
| 包内目录条目顺序 | `std::set<std::u16string>` 默认序 ⇒ 根 `""` **最先**（:1614/:1729） | 专用比较器 ⇒ 根**最后**（AK:1636 + `impl/ArchiveAutoPathOrder.h:10-17`，注释：避免 `tools/startup.tjs` 之类遮住包根 startup.tjs） |
| 记录给 boost | 全部挂载条目都记（:1642-1643/:1755-1756），boost 时再筛 | 只记补丁档案（:1664-1666） |

### 2C. 归档后端

| 维度 | KiriNext | AetherKiri |
|---|---|---|
| 创建者顺序 | ZIP → 7z → TAR → XP3，尝试间 `st` 回绕（impl/StorageImpl.cpp:680-700） | 相同（AK:726-746） |
| **XP3 v3 `hnfn` 真实文件名** | **完全没有** | 有：`cn_hnfn` 块（XP3Archive.cpp:402-403），跨续接索引块收集（:409-417/:482-508），逐项应用（:597-604） |
| XP3 基点探测（诱饵头） | **仅我们有**：`TVPFindXP3ArchiveBase`（XP3Archive.cpp:235-278）+ 触发条件（:360-377），注释记录真实案例 `nainiuniu5krkr.xp3` | 无 |
| XP3 索引加固 | 更多边界检查（:551-576/:617-635/:775-800）、空 segm 返回空流（:737-742）、段读钳位（:1147-1160） | 检查较少；空 segm 会读 `Segments[0]` |
| XP3 段缓存上限 | **1 MiB**（XP3Archive.cpp:840） | **256 MiB**（AK:785） |
| Cx 保护包 | 同一套方案，算法逐字节相同；只有一处注释换行不同 | 同 |
| ZIP | 加固 `unzGetCurrentFileInfo64` 名截断（ZIPArchive.cpp:980-999）与读取字节数校验（:2123-2147） | 无（忽略返回值 ⇒ 可能把未初始化堆内存交给脚本） |
| 7z | 校验返回/尺寸并正确释放（7zArchive.cpp:124-170） | 分配器不匹配 + `offset != 0` 时切片错误 |
| TAR | 析构不再吞异常（TARArchive.cpp:53） | 仍包 try/catch |

### 2D. 路径规范化 / 大小写

| 维度 | KiriNext | AetherKiri |
|---|---|---|
| 缺尾部分隔符 | `TVPAddAutoPath`/`RemoveAutoPath`/`SetCurrentDirectory` **抛异常**（StorageIntf.cpp:956-959/975-978/446-449） | `FixMissingPathDelimiter` 自动补（按扩展名补 `>` 或 `/`）并打 info 日志（AK:928-952） |
| 工程目录加入 auto path | 从不加（SysInitImpl.cpp:169-174） | 两种工程都加（AK:185/193）⇒ 大小写敏感文件系统上能解析大写文件名 |
| `TVPGetAppPath()` | 不去 `>`（impl/StorageImpl.cpp:599-608）⇒ 档案工程返回 `.../data.xp3>`，于是 `patch.tjs` 在包内找、`TVPGetTemporaryName()` 会往包里写 | 去 `>` 后取父目录（AK:627-635） |
| 单局复位 | 清 app-path 缓存 + 整张 auto-path 表（:610-618 + StorageIntf.cpp:999-1004） | `TVPResetAutoPathsForGameSession` 还清 XP3 段缓存与 auto-path 缓存（AK:2306-2311） |
| 清缓存语义 | 清段缓存 + 搜索缓存，表保持有效（StorageIntf.cpp:1314-1317） | 缓存与表都清（AK:2301-2304） |
| 文本编码 | 无"显式编码优先于统计猜测"规则（TextStream.cpp:190-195） | 有该规则 + 每局复位（AK/TextStream.cpp:448-460 + ScriptMgnIntf.cpp:4018） |

### 2E. 错误处理与写路径

| 维度 | KiriNext | AetherKiri |
|---|---|---|
| `.dll/.tpm` 存在性 | `TVPIsExistentStorage` 回落 `ncbAutoRegister::HasModule`（StorageIntf.cpp:1205-1210） | 同（AK:2086-2091）**且** `TVPGetPlacedPath` 对已注册模块直接返回规范化名（AK:1821-1828） |
| `TVPCreateStream` 写路径 | 只有 `TJS_BS_WRITE` 走 normalize（:1224-1228） | 写/追加/更新都走 normalize（AK:2142-2147） |

### 2F. TJS 存储表面

`Storages.*`：AK 比我们多 `addAutoToolsPath`(AK:2369)、`addArchive`(:2383)、
`isExistentStorageNoSearchNoNormalize`(:2466)、属性 `archiveUniqueKey`(:2480)。
C 层 API：AK 另有 `TVPCreateArcMedia`、`TVPRegister/UnregisterStorageResolver`、
`TVPFindLegacySaveThumbnail`、`TVPIsVirtualSolidVectorStorage`、`TVPResetAutoPathsForGameSession`。

### 2G. 存储媒体

两边都按名字去重（重名静默保留旧实例：KN StorageIntf.cpp:216-227 / AK:698-710）。

| 媒体名 | KiriNext | AetherKiri |
|---|---|---|
| `file` | `tTVPFileMedia`（impl/StorageImpl.cpp:86） | 同（AK:93） |
| `arc` | — | `tTVPArcMedia : tTVPFileMedia`（AK:537，PackinOne 用） |
| `proxy` | `tTVPProxyStorageMedia`（PluginImpl.cpp:237，惰性注册于 proxyfs 链接失败路径 :283-294） | 同（AK:421/559） |
| `psb`/`psd`/`var` | 有 | 有（同源） |
| `lzfs` | zcompat（zcompat_plugin.cpp:123/157），**透传**（:132-139，不做 LZ4 解码） | compatLegacyPlugins.cpp:137/177；LZ4 包装在 `AETHERKIRI_INTERNAL_LEGACY_PLUGINS` 下，但该宏未开启 ⇒ 实际也是透传 |
| `mem` | — | memfile.cpp:94/321 |
| `zip` | — | minizip.cpp:282/425 |

## 3. 兼容层与 IO 的纠缠点（实现隔离时要处理的清单）

KiriNext：
1. `StorageIntf.cpp:1016-1032` 的"先注册者优先"是为特定发行版（kirikiriz 保护壳 + 诱饵 startup.tjs）写的。
2. `StorageIntf.cpp:1205-1210` IO 层回答 `.dll/.tpm` 是否存在 ⇒ 依赖插件注册表。
3. `impl/StorageImpl.cpp:1774-1851` 补丁优先级 + 子串判定（含用户报的汉化补丁案例注释）。
4. `impl/StorageImpl.cpp:1665-1772` 工程目录 xp3 挂载（为打包版启动而加），服务于 `GameEntry.kt` 的镜像/补丁约定。
5. `XP3Archive.cpp:215-278`+`:360-377` 诱饵头基点扫描，绑定具体发行版。
6. `XP3Archive.cpp:655-680` + `XP3ArchiveCxDecoder.cpp:505/721-731` Cx 保护包，只硬编码一套方案。
7. `plugin/PluginImpl.cpp:305-309` 插件文件名别名（`emoteplayer.dll`→`motionplayer.dll` 等）。
8. `plugin/PluginImpl.cpp:216-262/283-310` 从插件加载失败路径里注册 `proxy` 媒体与 Gamepad 桩。
9. `plugin/PluginImpl.cpp:654-708` `Plugins.linkZ` + `tTJSNC_BootstrapLinkZResult`。
10. `plugin/ncbind.cpp:20-95` `.tpm`→`.dll` 模块别名（被 `TVPIsExistentStorage` 消费）。
11. `plugins/zcompat/zcompat_plugin.cpp` 约 20 个插件名注册（多数为 no-op 桩）+ 真实 `lzfs`。
12. `plugins/zcompat/k2compat_scripts.cpp` 2359 行内嵌 TJS（目前只登记名字）。
13. `ScriptMgnIntf.cpp:937-1006` 在 startup 执行路径里注入 KAG/KiriKiriZ 兼容全局。
14. `impl/StorageImpl.cpp:1429-1478` `searchCD`/`getLocalName`/`selectFile` 兼容壳。

AetherKiri（其 IO 里同样有层代码，移植时要**放进层**而不是进 IO 组件）：
`StorageIntf.cpp:231-283`（虚拟实体向量）、`:197-229`（分割 emote 虚拟存储）、`:40-137/368-526/2178-2208`
（伴生脚本虚拟存储，含内嵌 TJS）、`:1534-1616`（KAG 存档缩略图别名）、`:476-486/1985-2021`（`dx_*` 别名）、
`:4152-4177`（发行版相关的跟踪白名单）、`impl/StorageImpl.cpp:530-540`（`arc` 媒体）、
`plugin/PluginImpl.cpp:57-96/645-700`（`plugin_load_mode` 与 mock 开关）。

## 4. 隔离方案要点（已并入 §3/§4 的组件边界）

- IO 组件拥有：媒体注册表、路径解析、auto-path 表与缓存、挂载与排序、归档工厂、虚拟存储解析、模块定位查询。
- 层通过 `Config` 注入策略对象：`IAutoPathPolicy`（tie-break / 追加语义 / 分隔符修复 / 补丁分类）、
  `IMountPolicy`（候选收集 / 排序 / 根目录先后 / 优先级落点）、`IModuleLocator`（`.dll/.tpm` 存在性与别名）、
  归档提供者列表、虚拟存储列表，以及 `stripArchiveDelimiterInAppPath`、段缓存上限等具体开关。
- 旧头文件（`StorageIntf.h` 等）保留为一行转发，避免动 ~200 个调用点；`iTVPStorageMedia`/`iTVPStorageLister`/
  `tTVPArchive` 的 ABI 不动（约 110 个 ncb 插件与 `aetherkiri_ports.json` 的哈希纪律都依赖它）。
- 移植/移动文件会触碰 `compat/upstream/aetherkiri_ports.json` 的哈希条目 ⇒ 同一提交内 `--update`。
