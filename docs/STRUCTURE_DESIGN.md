# 文件结构重设计（设计稿）

> 状态：**部分执行**。§3（base 拆 4 个 target）与 base 的四目录重组已落地
> （`e1f80ea` / `7edaf4e`，未编译验证）；其余步骤未执行。执行记录见 §9。
> 输入数据：2026-11 实测（`core/` 下 11 个模块的头文件引用图，已剔除 `tjsCommHead.h`）
> 关联决策：`DECISIONS.md` #6（阶段计划）、#8（5 条建议）、#11（拆环结果）

---

## 0. 前置条件

本文档的所有动作都在**编译验证通过之后**执行。当前状态：

- 已合入 30 个 namespace 迁移 + 拆环提交，**尚未编译验证**
- 模块间双向依赖已从 14 组降到 **5 组**（见 §1.1）
- 剩下的环不是"删死 include"能解决的，需要拆 target 和接口抽取

顺序铁律：**先编译绿 → 再拆 target → 再动目录**。反过来会让 bisect 失效。

---

## 1. 现状实测

### 1.1 模块间依赖（头引用次数，已剔除 tjsCommHead.h）

```
utils     -> tjs2(21)                                            叶子
io        -> base(30), tjs2(11), utils(8)                        干净三层
movie     -> visual(9), utils(9), base(6), sound(3), tjs2(2), environ(2)   干净顶层
extension -> tjs2(2)
compat    -> io(3), tjs2(2), base(1)
base      -> tjs2(63), utils(45), visual(26), environ(15), sound(4), plugin(2), extension(1)
environ   -> base(56), visual(28), utils(19), tjs2(15), plugin(2), io(2)
sound     -> tjs2(29), base(29), utils(15), environ(2)
visual    -> base(147), tjs2(121), utils(45), environ(11), sound(2), plugin(2)
plugin    -> tjs2(10), base(9), utils(4), sound(2), compat(1), io(1)
```

剩余 **5 组环**：

| 环 | 权重 | 根因文件 |
|---|---|---|
| `base ↔ visual` | 26 / 147 | `base/ScriptMgnIntf.cpp`（TJS 类注册表）<br>`base/impl/SysInitImpl.cpp`（GraphicsLoaderIntf / LayerIntf / tvpgl） |
| `base ↔ sound` | 4 / 29 | 同一张注册表（CDDA / MIDI / Wave 工厂） |
| `base ↔ plugin` | 2 / 9 | 同一张注册表（Plugins 工厂） |
| `base ↔ environ` | 12 / 56 | `Application.h`(7)、`SystemControl.h`(5) |
| `environ ↔ visual` | 28 / 11 | visual 侧 11 处用了 environ 的 11 个不同头 |

### 1.2 base 的对外面：**28 个公共头 / 4 个内部头**

这是"base 能不能拆"的关键数据。被其它模块 include 的次数：

| 次数 | 头 | 使用者 |
|---:|---|---|
| 64 | `MsgIntf.h` | environ, io, movie, plugin, sound, visual |
| 33 | `StorageIntf.h` | compat, environ, io, movie, sound, visual |
| 30 | `EventIntf.h` | environ, io, movie, plugin, sound, visual |
| 26 | `SysInitIntf.h` | environ, io, sound, visual |
| 15 | `Platform.h` | environ, io, sound, visual |
| 14 | `UtilStreams.h` | io, sound, visual |
| 12 | `StorageImpl.h` | environ, io, plugin, visual |
| 11 | `ScriptMgnIntf.h` | environ, plugin, visual |
| 9 | `IndividualConfigManager.h` | environ, visual |
| 9 | `SysInitImpl.h` | environ, plugin, visual |
| 7 | `LocaleConfigManager.h` | environ, visual |
| 5 | `BinaryStream.h` / `DetectCPU.h` / `NativeEventQueue.h` / `SystemImpl.h` | sound, visual, environ |
| 4 | `combase.h` / `UserEvent.h` | io, movie, sound, visual, environ |
| 3 | `CharacterSet.h` / `TVPMmapAlloc.h` / `EventImpl.h` | plugin, sound, visual, io |
| 1–2 | `SystemIntf.h` / `typedefine.h` / `vkdefine.h` / `cpu_types.h` / `TextStream.h` / `GlobalConfigManager.h` / `FileSelector.h` / `ScriptMgnImpl.h` | environ, visual, io |

**只有 4 个是内部头**：`ApplicationSpecialPath.h`、`KAGParser.h`、`MsgIntfInc.h`、`impl/MsgImpl.h`。

> 推论：**base 不是"一个模块"，它是 4 层叠起来的**。它的问题不是"内部泄露到外部"，
> 而是"低层和高层塞在同一个 target 里"，导致高层对 environ/visual 的依赖被记在
> 整个 base 头上。

### 1.3 base 内部分簇依赖矩阵

按职责把 base 的 60 个文件分成 7 簇，实测互相 include 的次数：

```
FROM\TO      util    msg  event iostore runtime  script sysinit
util            0      4      2       4       0       0       1
msg             1      0      0       0       0       0       1
event           0      1      0       0       0       2       2
iostore         1      1      0       0       0       0       1
runtime         0      4      4       1       0       0       5
script          4      4      4       5       0       0      14
sysinit         2      1      0       3       0       3       0
```

跨模块边（`→ visual / sound / plugin / extension / environ`）只从 **script** 和 **sysinit** 两簇出去。

---

## 2. 目标树

```
cpp/core/<module>/
  include/           ← 只进 target_include_directories(PUBLIC)
    krkr/<module>.h  ← 门面头（facade）
    krkr/<module>/   ← 子模块头（显式 include，允许跨模块只用门面）
  src/               ← 只进 PRIVATE include path
  platform/
    android/         ← 现有 android/ 与 stubs/ 的实际语义
    posix/           ← 现在散在根目录的、Android 也编译的部分
    windows/         ← 现有 win32/（注意：这些在 Android 上也编译）
```

三条硬规则：

1. **`src/` 和 `platform/` 不在公共 include path 上** → 谁 include 内部头就是编译错误。
   这是唯一能落地的约束；文档约束在这个仓库已经失效过一次（`CompatLayer.h` 自己写着
   "对运行时行为没有影响"）。
2. **门面头不得 include `src/` 下的实现头**，只能 include 其它模块的门面头或自身子模块头。
3. **跨模块只能 include 门面头**；子模块头只在模块内部使用。

### 2.1 门面头清单（每个模块对外只暴露这些）

| 模块 | 门面头 | 内容 |
|---|---|---|
| `utils` | `krkr/utils.h` | StringUtil / FilePathUtil / Random / Exception / ObjectList / RealFFT / md5（+ `AsyncTriggerMode.h` 的枚举、`DeferredDestroyer`） |
| `base` | `krkr/base.h` | MsgIntf / EventIntf / SysInitIntf / StorageIntf（转发 io）/ ScriptMgnIntf / Platform / ConfigManager |
| `io` | `krkr/io.h` | StorageIntf 的实体 + IoPolicy / IoVirtualFile / StoragePolicy |
| `sound` | `krkr/sound.h` | WaveIntf / MIDI / CDDA / SoundBufferBaseIntf / WaveMixer |
| `visual` | `krkr/visual.h` | LayerIntf / BitmapIntf / WindowIntf / FontSystem / RenderManager |
| `movie` | `krkr/movie.h` | KRMoviePlayer / VideoOvl |
| `environ` | `krkr/environ.h` | Application / EngineBootstrap / EngineLoop / MainScene / SystemControl |
| `plugin` | `krkr/plugin.h` | PluginIntf / ncbind / 注册器 |
| `compat` | `krkr/compat.h` | CompatLayer / ModuleGate |
| `extension` | `krkr/extension.h` | Extension 注册表 |

### 2.2 子模块头（示例，按需生长）

```
utils/include/krkr/utils/string.h      StringUtil, FilePathUtil
utils/include/krkr/utils/thread.h      ThreadIntf（接口）, TVPTimer
utils/include/krkr/utils/encoding.h    CharacterSet, iconv 包装, md5
utils/include/krkr/utils/fft.h         RealFFT
base/include/krkr/base/msg.h           消息目录 + throw helpers
base/include/krkr/base/event.h         事件队列 + 输入事件基类
base/include/krkr/base/storage.h       —— 转发到 krkr/io.h，不再持有实体
```

**注意 `base/include/krkr/base/storage.h` 这一条**：`StorageIntf.h` / `StorageImpl.h`
现在物理上在 `core/base/`，但实体在 `core/io/`。命名空间迁移时用了"方案 A（门面转发）"，
目录重构时应该把**实体**搬回 io，base 只留一个 30 行的转发头，或者干脆让所有调用点改用
`krkr/io.h`（更彻底，但要动 46 个调用点）。

---

## 3. base 拆成 4 个 target

这是本设计里收益最大的一步：**它是"base 依赖 7 个模块"的全部原因。**

### 3.1 分配

| target | 文件 | 依赖（模块级） |
|---|---|---|
| **`core_base_core`** | util: `CharacterSet` `TVPMmapAlloc` `BinaryStream` `TextStream` `UtilStreams` `MiscUtility`<br>msg: `MsgIntf` `MsgIntfInc` `impl/MsgImpl`<br>event: `EventIntf` `impl/EventImpl` `NativeEventQueue` `UserEvent`<br>iostore 门面: `StorageIntf.h` `StorageImpl.h` `FileSelector` | tjs2, utils, **io** |
| **`core_base_runtime`** | `impl/DebugIntf` `impl/ClipboardImpl` `impl/ThreadImpl` `impl/TickCount` `impl/TimerIntf` `impl/TimerImpl` | base_core |
| **`core_base_script`** | `ScriptMgnIntf` `impl/ScriptMgnImpl` `KAGParser` `SystemIntf` `impl/SystemImpl` | base_core, base_runtime, **io / visual / sound / plugin / extension** ← 注册表 |
| **`core_base_sysinit`** | `SysInitIntf` `impl/SysInitImpl` `Platform.h` `combase.h` `typedefine.h` `cpu_types.h` `vkdefine.h` `DetectCPU` `ApplicationSpecialPath.h` `ConfigManager/*` | 全部 + **environ / visual** ← 启动路径 |

### 3.2 拆分后，环落在哪

| 环 | 拆分前归因 | 拆分后归因 |
|---|---|---|
| `base ↔ visual` | 整个 base | `base_script`（注册表）+ `base_sysinit`（GraphicsLoaderIntf / LayerIntf / tvpgl） |
| `base ↔ sound` | 整个 base | `base_script`（CDDA / MIDI / Wave 工厂） |
| `base ↔ plugin` | 整个 base | `base_script`（Plugins 工厂） |
| `base ↔ environ` | 整个 base | `base_sysinit`（Application / Platform / SystemControl） |

**`base_core` 和 `base_runtime` 拆分后是零违规边的**——它们只依赖 tjs2 / utils / io。
这一步之后，"base 依赖 7 个模块"这个说法消失，变成"两个高层 target 各有自己的对外依赖"。

### 3.3 然后这两块可以整体搬走

- `base_sysinit` 是**启动路径**（SysInit + 平台契约 + 配置），本质上属于 `environ`。
  搬过去后 `base ↔ environ` 的 12 次引用直接消失。
- `base_script` 里的注册表按 `DECISIONS.md` #9 改成注入（base 留注入口，environ 注册）。
  搬完后 `base ↔ visual / sound / plugin` 三组一起消失。

做完这两步，**5 组环只剩 `environ ↔ visual`(28/11)**。

---

## 4. 迁移顺序与验收

每一步独立可回滚，且都能单独编译验证。

| # | 动作 | 验收 | 风险 |
|---|---|---|---|
| 0 | 30 个提交编译通过 | `libengine_api.so` 产出 + 启动日志正常 | — |
| 1 | TJS 类注册表反转（`base/ScriptMgnIntf.cpp` → 注入口 + environ 侧实现） | 脚本能拿到全部 TJS 类（`Storages`/`Layer`/`Window`/`WaveSoundBuffer`/`AsyncTrigger`…） | **高**：注册顺序影响 `TJS_MEMBERENSURE`；注入口设晚 = 所有类消失 |
| 2 | `iTVPApplication` / `iTVPSystemControl` 接口抽到 base | `Application->Terminate()` / `ProcessMessages()` 时序不变 | 中 |
| 3 | base 拆 4 个 target（§3） | 每个 target 单独 `cmake --build` 通过；`--allow-multiple-definition` 依赖减少 | 中：CMake 循环依赖要一次性重构好 |
| 4 | `base_sysinit` 整体并入 `environ` | 启动路径不变 | 中 |
| 5 | `tjsCommHead.h` 的 164 处清理 | 全量编译通过 | 低（单个）/ 高（量大）：必须改一个编一个 |
| 6 | 目录重构（`include/src/platform` + 门面头） | 见 §5 | 中 |
| 7 | 去 `--allow-multiple-definition` | 链接无重复定义 | 需要先解决已知 3 处重复定义 |

### 4.1 为什么第 5 步排在拆 target 之后

`tjsCommHead.h` 是 164 个文件的机械改动，每个文件要补正确的显式 include。
**这个活必须有快编译循环**（改一个编一个）。放在最前面会制造一个 164 文件的未验证大 diff，
一旦出错无法定位。放在这里时，编译链路已经稳定，增量编译也快。

### 4.2 为什么第 3 步在目录重构之前

拆 target 是 CMake 层面的，不移动文件；目录重构是文件系统层面的。
先拆 target 可以在**不移动任何文件**的前提下验证分层假设（如果 target 之间出现新的
循环链接依赖，说明分层设计错了），成本远低于移动文件后再发现。

---

## 5. 目录重构的具体做法

### 5.1 一个模块一个 PR

以 `utils` 为样板（它已经是叶子，风险最低）：

```
core/utils/
  include/krkr/utils.h            ← 门面
  include/krkr/utils/string.h
  include/krkr/utils/thread.h
  include/krkr/utils/encoding.h
  include/krkr/utils/fft.h
  src/                            ← 原 .cpp
  platform/windows/               ← 原 win32/（Android 也编译，不要改名成 "android 不编译" 的错觉）
  platform/posix/                 ← 原 win32/DebugImpl.cpp 和 PadImpl.cpp 的通用部分
```

CMake：

```cmake
target_include_directories(${PROJECT_NAME}
    PUBLIC  ${UTILS_PATH}/include    # 只有这一条是公共的
    PRIVATE ${UTILS_PATH}/src ${UTILS_PATH}/platform/windows
)
```

### 5.2 每步的验收

```bash
# 1. 反向依赖没有新增（见 §6 的 ratchet）
python3 scripts/check_include_graph.py --baseline .ci/include-graph.json

# 2. 公共 include path 上的头，只有 include/ 下的
#    （故意在 src/ 里 include 一个内部头，应当编译失败）

# 3. 全量构建
./build.sh debug --engine-only
```

### 5.3 迁移顺序

```
utils（叶子）→ io（干净三层）→ base_core → base_runtime → sound → movie
→ visual → environ → base_script → plugins/
```

`visual` 在 `environ` 之前，因为 `environ → visual` 是合法方向，`visual` 先稳定后
`environ` 的重构才有稳定输入。

---

## 6. 防腐：include-graph ratchet

没有 ratchet，这次重构三个月后就会回退到原状（`CompatLayer` 的半接线状态就是前例）。

### 6.1 判据

```python
# scripts/check_include_graph.py
# 1. 层间反向 include = 0（按 §3.2 的目标分层，模块间只允许向下）
# 2. 每个模块的门面头 include 数不得超过基线（单调不增）
# 3. 跨模块只允许 include 门面头（禁止 include 其它模块的 src/ 或子模块头）
```

基线文件 `.ci/include-graph.json` 由当前实测生成，**只允许人工下调**。

### 6.2 分层定义（写进脚本，不只是文档）

```
tjs2
  └─ utils
       └─ io
            └─ base_core ─ base_runtime ─ base_script ─ base_sysinit
                 └─ sound
                      └─ movie
                           └─ visual
                                └─ environ
                                     └─ plugin / compat
```

注意这不是严格的 DAG（`movie → sound`、`visual → sound` 都是向下的，可以；
`environ → visual` 也是向下 ✓）。当前唯一的向上边是 §1.1 里那 5 组环。

---

## 7. 明确不做的事

| 不做 | 原因 |
|---|---|
| 把 `tjs2` 纳入命名空间/目录重构 | `TJS` 命名空间是第三方契约，`core/tjs2` 73 个文件独立演进 |
| 清理 390 条 `using krkr::` 桥接 | 那是"调用点迁移"任务，与目录重构正交；混做会让 diff 无法审阅 |
| 改 `--allow-multiple-definition` | 会暴露大量历史重复定义，需要单独一轮（已知至少 3 处：`TVPWriteDataToFile` / `TVPCheckAndSendDumps` / `TVPShowFileSelector`） |
| 一次性把所有模块的 `win32/` 改名 | 它们**在 Android 上也编译**，改名要同时确认哪些是真的 Windows-only（`tvpsnd.cpp` 是，`WaveMixer.cpp` 不是） |
| `plugins/` 的目录重构 | 前提是先定插件契约（能不能 include 核心头、能调哪些符号），否则每次核心重构都波及 10 万行 |

---

## 8. 度量口径（避免把"搬家"当成"改善"）

| 指标 | 现在 | 目标 | 说明 |
|---|---:|---:|---|
| 模块间双向依赖 | 5 组 | **0 组** | 唯一硬指标 |
| `base` 的跨模块依赖数 | 7 个模块 | 2 个（io/utils） | 拆 target + 搬 sysinit 后 |
| 公共 include path 上的头数 | 全部（含内部头） | 每模块 1 个门面 + N 个子模块 | 目录重构后 |
| `using krkr::` 桥接 | 390 条 | 不增加 | 清理是后续独立任务 |
| `namespace krkr2` 残留 | 21 处（`visual/simd/`） | 0 | 与本次目录重构一起处理 |

**注意不要用"文件数"当指标**：搬迁文件不改变任何结构性质。唯一有意义的是
"层间反向 include 数"和"双向依赖组数"。

---

## 9. 执行记录

### 2026-11-14 — §3 base 拆 4 target + base 四目录重组（`e1f80ea` / `7edaf4e`）

**做了什么**

| 提交 | 内容 |
|---|---|
| `e1f80ea` | `core_base_module` 从 STATIC 拆成 `core_base_{core,runtime,script,sysinit}` 四个 STATIC + 一个 INTERFACE 聚合层（名字不变，调用方零改动）。同时删掉 `impl/MsgImpl.cpp` 对 `PluginImpl.h` 的死 include —— 它让 `core` 被迫依赖 `plugin`，而该文件没用到其中任何符号。 |
| `7edaf4e` | 文件搬进 `core/` `runtime/` `script/` `sysinit/`；`impl/` 目录消失；新增 `cpp/core/base/README.md` 写清分层规矩与"新文件放哪"。 |

**实际依赖（按 include 图实测，不是按名字猜）**

```
core_base_core     -> tjs2, utils
core_base_runtime  -> core, sysinit, script, utils, visual, environ
core_base_script   -> core, runtime, sysinit, utils, visual, sound, plugin, extension, environ
core_base_sysinit  -> core, script, utils, visual, environ
```

`core` 是真的干净。§3.1 原本把 `EventIntf.cpp` / `EventImpl.cpp` /
`NativeEventQueue.cpp` / `FileSelector.cpp` 放在 core，但实测它们要
`ScriptMgnIntf.h`（script）、`Application.h`/`SystemControl.h`（environ）、
`WindowImpl.h`（visual）。硬放在 core 会让 core 变成最脏的一层，所以按
「这个文件 include 谁」改投 `runtime` —— 和本仓库既有的「头在下层、实现在上层」
一致（`core/utils` 的接口头 + `core/base` 的实现就是同一套）。

**intra-base 的环没消，只是显形了**

- `script ↔ sysinit`：`SysInitIntf.cpp` 要 `ScriptMgnIntf.h`/`KAGParser.h`；
  `ScriptMgnIntf.cpp` 要 `SysInitIntf.h`/`SysInitImpl.h`/`ConfigManager/*`。
- `runtime → script`：`EventIntf.cpp` 推送事件到 TJS。
- `sysinit → environ/visual`：`SysInitImpl.cpp`。

这四个方向恰好就是下一步要做的三件事（TJS 类注册表反转、`iTVPApplication`
接口抽取、`sysinit` 整体并入 `environ`）。拆 target 的价值在这里：**不用再打开
60 个文件才能判断某条依赖属于哪一层**。

**遗留（本步骤没做，也不该混进来做）**

1. 公共 include 路径仍是四条 `-I` 指向四个目录 = 整块 base 还是一张扁平面。
   收紧成门面头 + `#include "base/core/MsgIntf.h"` 是独立的调用点迁移任务。
2. 第三方库仍集中在 `core_base_core`（`unrar` / `LibXml2` / `SDL2` / `zstd` /
   `minizip` 其实 base 自己不用，符号是被全引擎共享的）。没有编译验证前不动。
3. 没有编译验证：本地无 NDK / cmake。文件分配按静态 include 图，
   `scripts/check_tu.sh --module base` 是第一个要跑的东西。
4. 显式路径的调用点共 10 行，已随之更新：`io/IoVirtualFile.h`、
   `compat/AetherKiriCompanions.cpp`、`bridge/engine_api_internal.h`、
   `plugins/motionplayer/Player.h`。
