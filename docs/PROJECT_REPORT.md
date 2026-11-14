# KrKr2Next 引擎项目报告

> 生成时间：基于工作区当前状态（HEAD = `74d5be3 refactor(plugin): move plugin host API into krkr::plugin`）
> 数据来源：`git log`、`git status`、`find`/`grep` 静态统计、`cpp/core/*/CMakeLists.txt`
> 范围：`cpp/`（引擎核心 + 插件）与 `bridge/engine_api`、构建脚本、验证工具

---

## 1. 项目定位与构建拓扑

KrKr2Next 是面向 **Android** 的 KiriKiri2（吉里吉里2）运行环境：

| 层 | 路径 | 产物 |
|---|---|---|
| Kotlin/Compose 宿主壳 | `app/` | APK |
| C++ 引擎核心 | `cpp/core` | `krkr2core`（INTERFACE 聚合 12 个 STATIC） |
| 兼容插件 | `cpp/plugins` | `krkr2plugin`（单个大 STATIC） |
| 宿主桥接 | `bridge/engine_api` | `libengine_api.so`（最终链接） |

引擎**在 Gradle 之外独立构建**（`build.sh` → `scripts/build_engine_android.sh`），再投放到
`app/app/src/main/jniLibs/arm64-v8a/`。原因是根 `CMakeLists.txt` 设置 vcpkg 的
`CMAKE_TOOLCHAIN_FILE`，与 Gradle 传入的 NDK toolchain 冲突。

构建入口：

```bash
./build.sh debug --engine-only      # 只出 libengine_api.so
./build.sh debug                    # 出 APK
./build.sh debug --configure-only   # 只跑 configure（vcpkg 124 个依赖）
```

运行时链路：

```
JNI / engine_create → engine_open_game → TVPEngineBootstrap::Initialize
  → Application::StartApplication → engine_tick → EngineLoop::Tick → Application::Run
  → TJS 脚本 / Layer 合成 / RenderManager_ogl
```

`cpp/core` 的 `add_subdirectory` 顺序被刻意安排：`io` 必须排在 `base` 之前（base 依赖 io 的路径解析）。
模块之间存在**故意保留的静态库循环依赖**（`base ↔ io`、`visual ↔ movie`），是历史迁移的过渡状态。

---

## 2. 代码规模

| 模块 | 文件数 | 行数 | 职责 |
|---|---:|---:|---|
| `core/visual` | 158 | 104 472 | 图层、位图、字体、图片、GLES 渲染 |
| `core/tjs2` | 73 | 38 441 | TJS2 脚本引擎（**不在 namespace 迁移范围**） |
| `core/movie` | 86 | 22 287 | FFmpeg 播放器 |
| `core/sound` | 41 | 17 635 | Wave/Mixer/解码 |
| `core/base` | 39 | 15 685 | 引擎服务：Msg/Event/Script/System + 存储门面头 |
| `core/io` | 20 | 9 197 | 存储、归档、路径、挂载 |
| `core/environ` | 37 | 9 063 | Application/Bootstrap/Loop/ConfigManager/平台 |
| `core/utils` | 45 | 7 132 | 字符串/线程/定时器/FFT/编码 |
| `core/plugin` | 8 | 4 322 | NCB 绑定、插件注册表 |
| `core/compat` | 6 | 600 | 兼容层策略 |
| `core/extension` | 2 | 130 | 扩展注册表 |
| `core/common` | 1 | 58 | 公共小头 |
| `plugins/` | 302 | 100 369 | 一等公民插件（Cubism/Steam/motionplayer/xp3filter/…） |
| **合计（cpp/）** | **820** | **329 435** | |

`plugins/` 与 `core/visual` 两项合计占全仓 **62%** 的代码量。

---

## 3. Namespace 重构现状（重点）

### 3.1 目标地图

```
krkr::utils      字符串/线程/定时器/FFT/编码
krkr::io         存储、归档、路径、挂载
krkr::base       引擎服务：Msg/Event/Script/System
krkr::visual     图层、位图、字体、图片
krkr::visual::gl GLES 渲染
krkr::sound      Wave/Mixer/解码
krkr::movie      FFmpeg 播放器
krkr::plugin     NCB、插件注册表
krkr::compat     兼容层策略
krkr::environ    Application/Bootstrap/Loop/平台
krkr::ext::xxx   plugins/ 下的一等公民插件
core/tjs2        保持 TJS namespace，不迁移
```

### 3.2 完成度总表

统计口径：头部 `^namespace krkr*` 打开次数（不含闭合注释）/ 含 namespace 的文件数 / `using krkr::` 桥接行数。

| 模块 | namespace 打开次数 | 含 krkr 命名空间文件 | 文件总数 | `using krkr::` 桥接 | 状态 |
|---|---:|---:|---:|---:|---|
| `utils` | 37 | 34 | 45 | 80 | ✅ 基本完成，例外已登记 |
| `sound` | 36 | 32 | 41 | 65 | ✅ 完成，正在做注释清理收尾 |
| `visual` | 30 | 20 | 158 | 0 | 🟡 20/158，且 namespace 目标不一致 |
| `io` | 19 | 19 | 20 | 23 | ✅ 完成（Phase 1–3 已合并） |
| `base` | 16 | 4 | 39 | 65 | 🟡 只有 4 个文件（Storage 门面 + SysInit） |
| `compat` | 6 | 6 | 6 | 0 | ✅ 完成（`krkr::compat` 原生新代码） |
| `environ` | 5 | 5 | 37 | 0 | 🟡 5/37，多数仍是全局 |
| `plugin` | 4 | 4 | 8 | 25 | 🟡 宿主 API 完成，NCB 框架待做 |
| `movie` | 3 | 3 | 86 | 2 | 🔴 3/86，几乎未开始 |
| `extension` | 2 | 2 | 2 | 4 | ✅ 完成 |
| `tjs2` | 0 | 0 | 73 | 0 | ⛔ 明确排除 |
| `plugins/` | 0 | 0 | 302 | 0 | ⛔ 未开始 |

命名空间落点分布（全仓，`^namespace krkr...` 打开次数）：

| namespace | 打开次数 | 分布 |
|---|---:|---|
| `krkr::utils` | 38 | utils 35 / environ 3 |
| `krkr::sound` | 38 | sound 36 / movie 2 |
| `krkr::io` | 33 | io 19 / base 14（门面转发） |
| `krkr2` | 21 | visual 21（全部在 `simd/`，历史遗留落点） |
| `krkr`（无子空间） | 11 | visual 6 / environ 2 / utils 2 / movie 1 |
| `krkr::compat` | 6 | compat 6 |
| `krkr::plugin` | 4 | plugin 4 |
| `krkr::host` | 2 | base 2 |
| `krkr::extension` | 2 | extension 2 |
| `krkr::alphamovie` | 2 | visual 2 |
| `krkr::font` | 1 | visual 1 |

两个需要注意的“目标不一致”：

- `core/visual/simd/*`（11 个文件）用的是 **`namespace krkr2`**（第三代命名），不是任何 `krkr::*` 子空间。
- `visual` 同时存在 `krkr`、`krkr::font`、`krkr::alphamovie` 三种落点，`movie` 里还引用了 `krkr::sound`。

意即：`visual` / `movie` 不只是“没搬完”，而是“已经搬的部分没按同一目标搬”。
这一点应该在阶段 5 开工前先定下契约（参见 §7）。

### 3.3 已完成模块的细节

**io（最完整，一整个 Phase 链已落地）**

- 全部 11 个 `.cpp` 都只有一个顶层 `namespace krkr::io` 块，无散落的全局定义：
  `IoPath` / `IoStorage` / `IoStorageLocal` / `XP3Archive` / `XP3ArchiveCxDecoder` /
  `ZIPArchive` / `7zArchive` / `TARArchive` / `IoPolicy` / `IoModuleLocator` / `IoVirtualFile`。
- 新增的内部头：`IoArchiveInternal.h`（集中归档后端间的前置声明）、`IoStorageInternal.h`。
- **中央门面已打通**：`core/base/StorageIntf.h` 里 52 条 `using krkr::io::…`、
  `core/base/impl/StorageImpl.h` 里 13 条 `using krkr::io::…`。
  即"方案 A"已实施：base 头保留为门面，声明体已在 `krkr::io`，旧调用点零改动。
  - 已迁移的代表符号：`tTVPArchive`、`iTVPStorageMedia`、`TVPOpenArchive`、`TVPCreateStream`、
    `TVPExtractStorageExt`、`TVPArchiveDelimiter`、`tTVPLocalFileStream`、`tTVPPluginHolder`、
    `TVPCreateIStream`、`TVPResetStorageImplForRestart` 等。
- 与 AGENTS.md §4 的 Phase 1/2/3 对应提交：`83128b7 → cc6699c → 0fb9328 → 2e1c987 → 54db7a8
  → 1e7b0dc → 9e5a27f → 4876270 → 4915fc3 → dd83640`。

**sound**

- 覆盖 32/41 文件：`WaveIntf`、`WaveSegmentQueue`、`WaveLoopManager`、`WaveFormatConverter`、
  `SoundBufferBaseIntf`、`MIDIIntf`、`CDDAIntf`、`FFWaveDecoder`、`VorbisWaveDecoder`、
  `PhaseVocoder*`、`RingBuffer`，以及整个 `sound/win32/`（在 Android 上也参与编译）：
  `WaveMixer`、`WaveImpl`、`MIDIImpl`、`CDDAImpl`、`SoundBufferBaseImpl`。
- 提交链：`6439492 → 9738d5a → 5f2cbd0`。

**utils**

- 覆盖 34/45 文件。已登记例外（有意保留全局）：
  `md5.h`（C API）、`iconv/`（生成表/adapter）、`MathAlgorithms*`（与
  `core/sound/MathAlgorithms.h` 重复声明）、`Debugger.h`（无人 include）。
- 残留未迁移文件：`core/utils/ThreadIntf.cpp`、`win32/ClipboardImpl.h`、`win32/DebugImpl.h`、
  `win32/PadImpl.cpp`——这几个的具体表现是头文件已进 `krkr::utils`，但对应 `.cpp` 或
  实现头仍是全局，属于"声明/定义同一 namespace"待核对项。

**plugin**

- `PluginIntf.h` / `PluginImpl.h` 已把宿主 API 搬进 `krkr::plugin`（25 条桥接 `using`），
  覆盖 `TVP_malloc/TVP_free`、`ZLIB_*`、`TVP_md5_*`、`TVPRegisterGlobalObject`、
  `TVPDoTryBlock`、`tvpLoadPlugins`、`TVPResetPluginSystemForRestart` 等。
- 未迁移：`ncbind.hpp` / `ncbind.cpp` / `ncb_invoke.hpp` / `ncb_foreach.inc`（宏与模板极重，需单独一轮）。

### 3.4 未完成模块的形态

| 模块 | 现状 | 主要障碍 |
|---|---|---|
| `base` | 仅 `StorageIntf.h`、`impl/StorageImpl.h`、`impl/SysInitImpl.*` | 中央接口层，46 个文件包含 `StorageIntf.h`、20 个包含 `StorageImpl.h`；`tTVPArchive` 等基类牵动面大 |
| `environ` | 仅 `Application.cpp`、`android/AndroidUtils.cpp`、`android/KrkrJniHelper.*`、`stubs/platform_linux.cpp` | `EngineBootstrap` / `EngineLoop` / `ConfigManager` 全是全局 |
| `visual` | 20/158；`simd/`、`ogl/`、`AlphaMovieDecoder`、`FontBaseline`、`RenderManager.h` 已包 | 目标不统一（`simd/` 是 `namespace krkr2`，其余是 `krkr` / `krkr::font` / `krkr::alphamovie`）；`LayerIntf.cpp` 12 864 行、`tvpgl.cpp` 14 139 行 |
| `movie` | 3/86；仅 `ffmpeg/AEStream.h`、`KRMoviePlayer.h`、`MoviePlayerJanitor.h` | 86 个文件，`motionplayer/Player.h` 4 958 行 |
| `plugins/` | 0/302 | 无契约，任意插件可 include 任何核心头、调任意全局 |

### 3.5 遗留全局符号规模（用于衡量剩余工作量）

| 指标 | 数量 |
|---|---:|
| `TVP*` 全局函数定义（粗略匹配） | **≈981** |
| 全仓唯一 `TVP*` 标识符 | ≈1 034 |
| 头文件中 `extern` 声明 | **448** |
| 命名世代 | `TVP*` / `tTVP*`（TVP2 时代）、`krkr2`（第三代，仍存于 `visual/simd`）、`krkr::`（新） |

即：namespace 迁移覆盖了 **utils / io / sound / extension / compat / plugin 宿主 API**，
但绝对符号量的大头仍在 `visual`（176 个头内 `TVP*` 声明）、`base`（121）、`environ`（41）、
`utils`（26）、`sound`（22，多为桥接层保留）。

---

## 4. 工作区当前状态（未提交）

```
 M cpp/core/sound/CDDAIntf.cpp      (-31 行，纯注释分隔线删除)
 M cpp/core/sound/CDDAIntf.h        (-11 行，纯注释分隔线删除)
 M cpp/core/sound/MathAlgorithms.cpp (-2 行，纯注释分隔线删除)
?? TODO.md                          (92 行，结构问题清单，report 素材)
```

行为影响：**零**。三个文件的 diff 只有 `//-----` 分隔线与过时的类注释块，
没有改动任何代码、签名、字符串。可安全提交为 `docs(sound): drop stale separator comments`。

`MathAlgorithms` 仍在两处重复声明（`core/utils/MathAlgorithms.h` 与
`core/sound/MathAlgorithms.h`），AGENTS.md §2.2 已登记为有意保留的例外。

---

## 5. 结构性债务

### P0 — 结构性问题

1. **源码级依赖成环**（不是 CMake 的锅，STATIC 库不报错，但理解成本和增量编译都失效）
   - `core/base` 6 处 `#include "Application.h"`，而 Application 在 `core/environ` → base ↔ environ 环
   - `core/io` 依赖 base 的 `MsgIntf`/`UtilStreams`，base 又依赖 io 的存储实现 → base ↔ io 环
   - `core/io/IoStorageLocal.cpp` 为 `TVPSelectFile` 直接 `#include "WindowImpl.h"` → io → visual 泄漏
   - `core/movie` ↔ `core/visual` 互依

2. **`--allow-multiple-definition`** 仍在 Android 链接选项里，掩盖了重复定义/潜在 ODR 与初始化顺序问题。

3. **半接线抽象**（最坏状态：付了认知成本，没拿到行为收益）
   - `core/compat/CompatLayer.h` 自己写明"策略还没有接到 StorageIntf/StorageImpl 的实现上，
     本文件当前对运行时行为没有影响"。
   - `IoPolicy` / `StoragePolicy` / `ModuleGate` 已存在，但只登记取值 / 默认放行。

4. **全局状态与隐式初始化顺序**：`Application` 指针、`TVPMainThreadID`（dylib 加载时取初值）、
   448 个 `extern`、22 处 `GetInstance()/Instance()`。二次启动/重启类 bug 多源于此。

### P1 — 可理解性

5. **巨型 TU**：`LayerIntf.cpp` 12 864、`tvpgl.cpp` 14 139、`RenderManager_ogl.cpp` 5 278、
   `motionplayer/Player.h` 4 958 行。合并冲突频繁、编译慢。
6. **死代码 / 条件编译**：298 处 `#if 0`、177 处 `TODO/FIXME/HACK`、8 处被注释掉的 CMake 源。
7. **命名误导目录**：`core/sound/win32/`、`core/utils/win32/`、`core/environ/win32/`
   在 Android 构建中全部参与编译。
8. **巨型头**：`tjsCommHead.h` 被 165 个文件包含。
9. **插件无契约**：10 万行打成一个 STATIC，兼容 hack 因此容易漏进 `core`。

### P2 — 工程能力

10. **测试几乎为零**：仅 3 个测试源（ABI 布局、TJS 字节码加载、tvpgl SIMD 对比）。
    没有启动 smoke test、没有归档 round-trip、没有渲染 golden → 重构基本是盲飞。
11. **平台分支噪音**：`WIN32` 相关 225 次、`__ANDROID__` 52 次、`__APPLE__` 45 次；
    实际只出 Android + Linux 宿主验证。

---

## 6. 验证手段现状

| 手段 | 命令 / 文件 | 说明 |
|---|---|---|
| 引擎构建 | `./build.sh debug --engine-only` | 真正判据，需要 NDK + vcpkg |
| Linux 宿主 | `cmake --preset "Linux Debug Config"` + `--build --preset "Linux Debug Build"` | 无 NDK 的编译验证 |
| 静态检查 | `bash scripts/check_static.sh` | 不需要 NDK |
| 单文件语法 | `scripts/check_syntax.sh <file.cpp>` | 仅适合依赖少的文件 |
| GL 符号 | `scripts/check_gl_symbols.py` | 按平台单独跑 |
| JNI 符号 | `scripts/check_jni_symbols.py` | |
| 端口漂移 | `scripts/check_port_drift.py` | |
| so 校验 | `scripts/verify_engine_so.sh` | CI 里的符号断言 |
| 符号核对 | `llvm-nm -C --defined-only libengine_api.so \| grep 'krkr::io::'` | 迁移后确认新符号存在 |
| 行为对比 | 启动日志 diff（千恋万花 / NEKOPARA） | 关注 XP3 挂载顺序、`patch.xp3` 优先级、模块查询、字体/插件加载 |

## 6.1 CI

`.github/workflows/android_build.yml`：推分支 / PR 后编译，引擎 job 先出 `libengine_api.so`
再做符号断言。

---

## 7. 建议的下一步

按"收益 / 风险"排序，每步一个 commit，一个 cluster 一件事：

1. **收尾 sound 工作区**（零风险）
   - 提交现有 3 个文件的注释清理；
   - 核对 `ThreadIntf.cpp`、`win32/ClipboardImpl.h`、`win32/DebugImpl.h`、`win32/PadImpl.cpp`
     是否满足"声明与定义同 namespace"。
2. **补测试地基**（最高杠杆，先把盲飞变有仪表）
   - 归档 round-trip（XP3/ZIP/7z/TAR 打开 + 列举 + 读取 + 缓存命中）
   - 启动 smoke test（Linux 宿主跑 `startup.tjs` 到首帧）
   - `--allow-multiple-definition` 的可诊断化：先把它变成 warning 清单，再逐条消掉。
3. **`core/plugin` 的 NCB 框架**（`krkr::plugin::ncb`）
   - 完成后 plugin 模块边界最完整；风险在 `NCB_*` 宏需要同步改。
4. **`core/environ`**（`Application` / `EngineBootstrap` / `EngineLoop` / `ConfigManager`）
   - 平台边界更清晰；同时是解开 base ↔ environ 环的入口。
5. **`core/visual`** 分两步
   - 先统一目标：现有 `krkr2`（simd）、`krkr`、`krkr::font`、`krkr::alphamovie` 四种落点需要先定契约，
     再决定 simd/ogl 是否按 `krkr::visual` / `krkr::visual::gl` 收口；
   - 再按子目录（`simd/` → `gl/` → `impl/` → 根目录大文件）分批。
6. **`core/movie`**：86 个文件、`motionplayer` 是最大变量，放最后。
7. **`plugins/`**：需要在核心边界稳定后再定插件契约（`krkr::ext::xxx`），
   否则每次核心重构都会波及 10 万行插件。

**不建议现在做**的：把 `tTVPArchive`/`tTVPStorageMedia`/`tTJSNC_Storages`/`TArchiveStream`
从 base 头搬出（Phase 4），需要先做 io 独立公开头的设计任务；以及为"干净"去改 CMake
依赖方向或删 `--allow-multiple-definition`。

---

## 8. namespace 迁移的验收基线（沿用 AGENTS.md）

- [ ] `git diff -w` 只有 namespace / using / include
- [ ] 声明与定义在同一 namespace
- [ ] 不新增全局 `TVP*` / `tTVP*` 符号
- [ ] 旧调用点无需修改（全局 `using` 生效）
- [ ] `./build.sh debug --engine-only` 通过，CI 绿
- [ ] 启动日志与迁移前一致（至少一个测试游戏）
- [ ] 一个 commit 只做一件事，并更新 AGENTS.md §2 进度表


## 9. 2026-11 后续进展（覆盖本文前面若干已过期结论）

本文 §2 / §3.2 / §4 / §5 的实测数字取自较早的一次快照（那时 `base` 还是
60 文件 / 单 target，`sysinit/` 还在 base 里）。**结构部分以 `cpp/core/README.md`
与 `docs/STRUCTURE_DESIGN.md` §9 为准**，下面只列差异：

| 本文的说法 | 现在的状态 |
|---|---|
| `base` 仅 4 个文件迁 namespace，单 target | 已拆 `core/` `runtime/` `script/` 三层 + INTERFACE 聚合层；`sysinit/` 整簇并入 `environ/` |
| `io` 20 文件扁平目录 | 已拆 `policy/` `path/` `archive/` `storage/` 四层（DAG，反向边 0 条） |
| `sound` 有 `win32/` 子目录 | 已拆 `common/` `intf/` `codec/` `impl/` 四层；`win32/` 改名 `impl/`（它在 Android 上也编译） |
| §5 第 7 条「命名误导目录」 | `sound/win32` → `sound/impl`、`environ/win32` 已解散（实现 → `platform/`、契约 → `intf/`）；只剩 `utils/win32` 未解决 |
| `environ` 未拆层（`sysinit/` 刚搬进来） | 已拆 `intf/`（纯契约头，INTERFACE 库）`sysinit/` `platform/` `app/` 四层；内部是 DAG，四组环全在跨模块 |
| `plugin` 只做了宿主 API | 已拆 `host/` `ncb/` `loader/` 三层；宏层未动（NCB → `krkr::plugin::ncb` 仍待做） |
| `utils` 已迁 namespace、`win32/` 参与编译 | 已拆 `c_api/` `misc/` `intf/` `impl/` 四层；`win32/` → `impl/`。至此全仓没有"名字说 Windows、实际编 Android"的目录 |
| `compat` 是「新代码，规模小」 | 已拆 `layer/`（策略与门控）+ `companions/`（层专属内容），"策略不认识内容"变成构建图性质；半接线仍未接 |
| `extension` 是「新代码，规模小」 | 判定为**有意不拆**（1 个实现文件），理由写在 `cpp/core/extension/README.md` |
| 未重组的模块 | 只剩 `visual`(156 文件) 与 `movie`(85 文件) |
| §4「工作区当前状态」 | 已提交；工作区干净 |
| §6「验证手段」 | 新增 `scripts/check_cmake_graph.sh`：无 NDK 也能验 target 图与源文件存在性 |

**没有变的部分（仍是最高优先级）**

- 5 组模块间双向依赖一组都没消。三条路径（TJS 类注册表反转、`iTVPApplication`
  接口抽取、environ↔visual 的 visual 侧 11 处）全部需要编译器。
- `--allow-multiple-definition` 仍在；半接线抽象（`CompatLayer` / `IoPolicy` /
  `ModuleGate`）仍在；`#if 0` 299 处、`win32/` 命名、测试几乎为零。
- `visual`（156 文件）与 `movie`（85 文件）**主体未动**，且 `visual/simd/` 仍是
  `namespace krkr2`。这是剩余工作量的绝对大头。
- **全部 namespace / 目录改动都还没有过编译器。** 模块整合时第一件事是
  `./build.sh debug --engine-only`（风险最高的提交清单见 `docs/HANDOVER.md` §5.2）。
