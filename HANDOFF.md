# 交接文档（KrKr2Next / KiriNext）

> 用途：把**当前目标、已落地的东西、未完成项、硬约束、验证办法**交给下一个上下文。
> 读这份之前先读根目录 `AGENTS.md`（项目级规则）。兼容层的细节事实与逐条差异见
> `compat/README.md`，本文件是"接手第一份"。

---

## 0. 一句话现状（2026-09-19）

两条并行目标都在推进：

1. **兼容层**：`cpp/core/io/` 单一 IO 组件已建成（存储/归档/路径/TJS 门面全在里面），两层机制
   （按游戏选 `krkr2next.json` 的 compat 档）已接线；**A 块（TJS2 内核回退）、C1、C4 已完成**；
   剩余 C3/C5/C6/C7、M6 插件层分批、以及**一项等用户裁决的 M1 尾巴**。
2. **壳（Kotlin/Compose）**：用户新提的九项改造**除"目录收藏 UI 打磨"外全部落地**，最近一次
   CI（`b2e9fb4`，run `35455858612`）**绿**，含 APK 产物。

最新提交：`67c95b0`（已推送）。工作区除用户自己的 `.gitignore`/`README.md` 外干净；本轮又新增
一批 M6 插件桩（未提交，见下）。

---

## 1. 目标一：KAG 兼容层对齐 AetherKiri

用户确认的范围是五块（原话概括）：

| 块 | 内容 | 状态 |
|---|---|---|
| A | TJS2 内核兼容读写：未定义全局回退(34 名) + 启动名回退 + `touchImage`/`renderCount` 合成 + 8 名启动期**写**白名单 | ✅ 完成 |
| B | KAGWindow / krkrgles 脚本别名与绘制设备接管 | 🟡 B2：4 目标扇出 + 每帧重试 600 帧 + 卸载摘钩 + **drawDevice/gpuDrawDevice/nativeDrawDevice 契约与核心全局镜像**（2026-09-21，非覆盖语义）；B1（GPU 伴生脚本替换）未做 |
| C | KAGParser / extkagparser / kagparserex 行为对齐 | 🟡 **C1 ✅、C4 ✅**；C3/C5/C6/C7 未做 |
| D | 插件模拟层（旧 Windows 插件全量照搬，分批） | 🟡 已移 6 个 AetherKiri 层专属模块 + 本批 8 个（systemEx/registory/stdio/javascript/messenger/msgreceiver/tasktray/adjustMonitor）；其余约 50 个缺失模块待分批 |
| E | 启动与资源加载顺序（startup/patch/auto-path/插件解析） | 🟡 I 系列大部分已做；`patch.tjs` 分两层（E1）未做 |

架构要求（用户明确）：
- 先做 **M1：把文件 IO/加载逻辑收敛为单一隔离组件**（新目录 + 稳定接口，现有公开 API 保持兼容）——
  **已完成**：`cpp/core/io/`（`IoPath.cpp` / `IoStorage.cpp` / `IoStorageLocal.cpp` /
  `XP3Archive*` / `ZIPArchive` / `7zArchive` / `TARArchive` / `tar.h` / `StoragePolicy.h` /
  `IoPolicy.*` / `IoModuleLocator.*`），公开声明仍在 `base/StorageIntf.h` 与 `base/impl/StorageImpl.h`。
- 兼容层按游戏选择：`krkr2next.json` 的 compat 字段 → 引擎选项 `game_compat_profile`；
  **缺省走旧版 krkr2 层**（`krkr2-classic`），AetherKiri 层显式开启（新增 `aetherkiri` 档）。
- **架构收敛为两层**（2026-09-19）：删除 `kag` 渲染档与 `krkrz-kag` 兼容档，其 KAGWindow
  接管能力归 AetherKiri 层（`krkrgles` post-regist 按 `ActiveLayer()==AetherKiri` 安装）；
  `auto` 判到 `motionplayer*` 直接激活 AetherKiri 层。Live2D 仍用本仓库原生 Cubism 实现。
- 两层共用同一 IO 组件，不重复实现。

---

## 2. 目标二：壳（Kotlin/Compose）改造清单

用户原话要点与状态：

| 要求 | 状态 | 提交 |
|---|---|---|
| 引擎日志**按游戏分开**（不要混一个文件） | ✅ | `9b1c713` + `6a2431c`（剪枝/清空/分享/记住上一局） |
| 点**封面**进详情页；启动游戏/移除游戏**只在详情页** | ✅ | `b2e9fb4` |
| 移除游戏**不删文件**，只从库移除 | ✅（`GameLibrary.remove` 只改库文件；UI 已写明） | `2716920` `b2e9fb4` |
| 详情页按 **MD3** 漂亮实现 | ✅ 重写过一版（`GameDetailScreen.kt` 723 行改动） | `b2e9fb4` |
| **游戏设置单独一个页面** | ✅ `ui/GameSettingsScreen.kt` + 路由 | `b2e9fb4` |
| 加**导航栏**，适配平板与手机 | ✅ 底部 `NavigationBar` / ≥600dp 左侧 `NavigationRail` | `a033a30` |
| 去掉设置里多余说明（G2、千恋万花等开发测试游戏名） | ✅ 改通用表述 | `0e0d70a` |
| 文件浏览器：两个 topbar 合一 + 完整路径跳转 + 目录收藏 | 🟡 顶栏合一 ✅(`3cc7a85`)、路径跳转 ✅（既有点标题打开对话框）、目录收藏**数据层**✅(`b5b44c0`)+UI 改动(`b2e9fb4`)，**交互待上机确认** | — |
| 游戏库：收藏游戏 + 分组（便签式） | ✅ 数据层 `c3c3673` + UI `b2e9fb4` | — |
| 关于页：作者/开源协议/仓库/技术栈/版本号 | ✅ `ui/AboutScreen.kt` + `Routes.ABOUT` | `0e0d70a` |

---

## 3. 硬约束（踩过的坑，别再犯）

1. **不要提交/推送用户自己的改动**：`.gitignore`、`README.md` 在会话开始时就是 modified，
   始终不要 `git add` 它们；也不要 `reset --hard` / `checkout --` / `clean -f`。
2. **构建只在 CI**：本机没有 NDK/vcpkg/cmake，`./build.sh` 跑不了。可用的是
   `bash scripts/check_static.sh`（JNI 符号、移植清单、语法检查）。
3. **推送后会取消上一条 run**：工作流 `concurrency: cancel-in-progress: true`，
   连推几次时中间 run 显示 `cancelled` 属**预期**，不是失败；要验证就以最后一次为准。
4. **Kotlin 无法本地类型检查**：本机有 `kotlinc` 但缺 android.jar 与 androidx 依赖，
   所以壳改动**必须靠 CI 的 APK 任务验证**。已踩过一次：加导航栏时漏 import
   （`MainActivity` 少了 `ShellScaffold`/`Routes`），CI 报 `Unresolved reference`，
   后续提交修好 —— 新加跨文件符号时**顺手补 import**。
5. **移植纪律**：独立新文件的移植要登记 `compat/upstream/aetherkiri_ports.json`
   （片段移植用 `partial-extract` + `source_ref`）并跑 `python3 scripts/check_port_drift.py --update`；
   **既有文件内部的片段移植不进清单**，改为在落点写"移植自 AetherKiri <文件>:<行范围>"，
   理由见 `compat/README.md §6`。
6. **不要动内存预算/压力相关逻辑**（用户明确禁止，历史上改坏过）；**渲染改动要小**。
7. 诊断探针：能默认关闭就默认关闭；高风险/高频日志要采样或只打边沿。
8. 真机验证只能靠用户装 CI 产物；本机看不到设备。

---

## 4. 已裁决的决策（不要重新讨论）

| 议题 | 裁决 |
|---|---|
| A 块回退（全局名/启动名/renderCount/touchImage） | **只给 AetherKiri 层**（开关 `TJS::TJSSetCompatFallbacksEnabled`，缺省关） |
| A3 启动期**写**白名单（8 名） | **两层都要**（无开关） |
| C1 `taglist` + `copyTag` | **两层都要** |
| C3 `GetNextTag` 文本段聚合 | 决定仍是"只给 AetherKiri 层"，但**上游该实现建在 C2 翻译层之上**（`TVPTransformText`/`TVPPrefetchText`），C2 未移植时无法落地（详见 `compat/README.md §5.4` 注） |
| C4 `.scn` 容错 + 标签回调 | 两层（未注册回调时行为与移植前逐字相同） |
| E1 `patch.tjs` 时机 | **分两层**：classic 保持 startup 之前；AetherKiri 层照搬上游（**必须连晚 patch 韧性层一起**） |
| I2 首次读 `startup.tjs` 被原版压住 | **在 classic 层修**（已做：boost 提到 startup 之前） |
| 缺省兼容层 | **旧版 krkr2 层**；`aetherkiri` 档显式开启 |
| B2 别名扇出/重试/卸载、P1 注册回滚、P2 模块别名、P3' k2compat 门控 | 全部已实施 |

---

## 5. 未完成 + 阻塞

| 项 | 规模 | 阻塞 |
|---|---|---|
| **I3（唯一等用户一句话）**：classic 层在档案工程直启 `.../data.xp3>` 时，是否挂兄弟 `patch*.xp3`？ | 小 | 决定 M1 最后一个策略开关 `mountSiblingsForArchiveProject` |
| C3 `GetNextTag` 文本段聚合（只给 AetherKiri 层） | ~230 行 | **阻塞**：上游依赖 C2 `TVPTransformText`/`TVPPrefetchText`；C2 未移植时聚合无功能收益、只改存档位置语义 |
| C5 每帧 KAG 修复（`envclear` 复位、`[endtrans]` 无 trans 等待） | ~120 行 | 依赖 C6 部分前提 |
| C6 KAG 运行时补丁层（27 文本补丁 + 11 类包装） | ~1500 行 | 前提是 patch.tjs 晚执行（E1） |
| C7 `ExtKAGParser`（第二解析器） | ~4700 行 | 先改 `ExtKAGParser.hpp` 的 `KAGParserH` 保护宏、定 `paramMacros`/`copyTag` 缺失、与 `kagparserex` 空壳互斥 |
| E1 `patch.tjs` 分两层（含韧性层） | 中 | 无（已裁决），影响面大需逐游戏回归 |
| B1 GPU 伴生脚本惰性注入 | 中 | 无（可选） |
| M6 其余插件（约 50 个缺失 + 部分覆盖项） | ~3300 行 | 无；清单见 `compat/recon/plugin-compat-diff.md`；本批已落地 systemEx/registory/stdio/javascript/messenger/msgreceiver/tasktray/adjustMonitor |
| 壳：目录收藏交互打磨、平板双栏（列表-详情）、库页/详情页 MD3 细节 | 小-中 | 无 |

---

## 6. 怎么验证

**本地（每次改完都要跑）**
```bash
cd KiriNext && bash scripts/check_static.sh          # JNI 符号 / 移植清单 / 语法
python3 scripts/check_port_drift.py                  # 只查移植漂移
clang++ -std=c++17 -fsyntax-only -I<...> <file>      # 单 TU 语法检查（缺 spdlog/boost 时
                                                     # 可用 .scratch/shim 里的最小替身）
```

**CI（唯一真构建）**
```bash
git push origin main
RID=$(gh api "repos/clevebitr/Krkr2Next/actions/runs?per_page=1" --jq '.workflow_runs[0].id')
gh run watch "$RID" --repo clevebitr/Krkr2Next          # 注意 --exit-status 对 cancelled 不可靠
gh api "repos/clevebitr/Krkr2Next/actions/runs/$RID" --jq '.conclusion'   # 以此为准
gh api "repos/clevebitr/Krkr2Next/actions/runs/$RID/artifacts" --jq '.artifacts[].name'
```
产物：`KrKr2Next-apk-debug`、`libengine_api-debug`。失败时用
`gh run view "$RID" --log-failed | grep -E "e: |error:"` 抓 Kotlin/NDK 错误。

**真机回归清单（交给用户）**
读档、动态立绘、`AetherKiri 层接管完成（…别名 N/4…）`、`io policy: tie-break=… patch-rule=…`、
`module gate:`（选 AetherKiri 档时）、`FontSystem: 已注册字体 N 个 -> …`、
`logs/games/<游戏名>-<短哈希>/engine-<时间戳>.log` 是否按游戏分开。

---

## 7. 关键文件与证据索引

| 东西 | 位置 |
|---|---|
| 兼容层事实/约束/阶段表/差异清单/裁决记录 | `compat/README.md`（§1 层与选择、§2 依赖不变量、§3 目录边界、§4 阶段、§5 差异+裁决、§6 移植溯源、§7 恢复指引） |
| IO 对照证据（auto-path 语义、挂载、归档、路径、纠缠点） | `compat/recon/io-loading-diff.md` |
| KAG 脚本层对照证据（KAGParser、ScriptMgnIntf 补丁层、TJS 内核回退、ExtKAGParser、冲突清单） | `compat/recon/kag-script-diff.md` |
| 插件层对照证据（约 100 个模块覆盖表、合并冲突、移植成本） | `compat/recon/plugin-compat-diff.md` |
| 渲染层对照证据（兼容层↔渲染耦合、`ogl/` 相对上游的功能差异、未覆盖的 ES2-only 路径） | `compat/recon/render-diff.md` |
| 移植溯源清单（含 `partial-extract` 类别） | `compat/upstream/aetherkiri_ports.json` |
| IO 组件 | `cpp/core/io/`（`StoragePolicy.h` 策略契约；`IoPolicy.*` 注入点；`IoModuleLocator.*` 模块查询注入） |
| 兼容层框架 | `cpp/core/compat/`（`CompatLayer.*` 层注册表 + 策略注入；`ModuleGate.*` 模块归属门） |
| 层专属插件 | `cpp/plugins/compat/aetherkiri/`（`legacy_zlib_version.cpp`、`legacy_system_misc.cpp`） |
| 壳 UI | `app/app/src/main/kotlin/org/dpdns/clevebitr/ui/`（`Nav.kt` 路由+导航条、`LibraryScreen`、`GameDetailScreen`、`GameSettingsScreen`、`PickerScreen`、`SettingsScreen`、`AboutScreen`） |
| 每游戏日志 | `app/.../core/LogFiles.kt`（`gameLogDir`/`gameEngineLog`）、`core/EngineSession.kt`（`switchEngineLogToGame`） |

---

## 8. 下一轮建议顺序

1. 若用户回了 **I3**：接完 `mountSiblingsForArchiveProject`（M1 收尾，小）。
2. 否则：继续 **M6 小模块批次**（每批 2–4 个，机械、可验证）——下一批候选见
   `compat/recon/plugin-compat-diff.md §2`（如 `systemEx` 剩余函数、`layerExSave`、`msdfrender`）。
3. **C3 已阻塞**：上游 `GetNextTag` 聚合依赖 C2 翻译层（本仓库未移植）；若要推进需先裁决 C2。
4. 壳侧：目录收藏交互打磨 → 平板双栏 → 库页/详情页 MD3 细节。
