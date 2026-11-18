# cpp_new — 引擎入口重写（交接 / TODO）

> 目标：在 `cpp_new/` 里从零写一个干净的引擎核心，不 include 老树（`cpp/core/**`）的任何头，
> 长到能跑之后一次性替换 `bridge/engine_api/` 里的实现。
> 老树是参考物和需求书，**不是**依赖。

---

## 0. 一句话现状

分层结构已落地（`tjs` / `shim` / `platform` / `core` / `visual` / `engine` / `debugger`），
脚本引擎可执行真实游戏的启动脚本：`startup.tjs` 经 `Scripts` / `Storages` / `System` /
`Debug` / `Plugins` / `Window` / `Layer` 七个桥接类推进，`system/Initialize.tjs`
（TJS2 字节码）已能加载。窗口与图层现在是真场景树：`visual/Scene` 合成成 DrawList，
软件后端出像素，GLES3 后端在桌面与软件逐像素一致（8a/8b/8c 代码完成，真机一帧待 JNI）。
**当前缺口未知**：最后一个测量点是 `Member "Window" does not exist`，该成员已补齐，
新的缺口需要在有游戏数据的开发机上重新测（§11）。

## 1. 现在能跑什么

```bash
bash cpp_new/tools/run_probes.sh            # CMake 配置 + 构建 + CTest 全部断言（21 项）
python cpp_new/tools/check_layer_deps.py cpp_new   # 期望 0 违规

# 调试事件（可选）：录制 → 汇总 / 过滤
KRKR_DEBUG_EVENTS=build/events.jsonl build/cmake/debug/bin/krkr_next --path-file build/real_root.txt
build/cmake/debug/bin/krkr_debug --summary build/events.jsonl
build/cmake/debug/bin/krkr_debug --filter "ev=script.fail" build/events.jsonl
```

CMake 目标与分层一一对应（`cpp_new/CMakeLists.txt`）：`krkr_tjs` / `krkr_tjs_host` /
`krkr_core` / `krkr_visual` / `krkr_platform` / `krkr_shim` / `krkr_engine` / `krkr_debugger`。**不要再手敲编译命令**；
旧的手写构建脚本已删除（技术债 #38 收口）。预设见 `cpp_new/CMakePresets.json`
（`desktop-debug` / `desktop-release`）。

四个产物：

| 产物 | 组成 | 用途 |
|---|---|---|
| `build/tjs_probe.exe` | `host/TjsProbe.cpp` + SHIM + IO + `core/log` + `core/api` + TJS | 脚本层断言 |
| `build/krkr_next.exe` | `host/main.cpp` + `core/api` + `core/log` + IO | 引擎入口与路径校验 |
| `build/storage_probe.exe` | `host/StorageProbe.cpp` + `core/log` + IO | 存储层断言（18 项） |
| `build/xp3_probe.exe`、`build/xp3pack.exe` | `host/Xp3Probe.cpp`、`host/Xp3Pack.cpp` | 包解析与夹具生成 |
| `build/window_probe.exe` | `host/WindowProbe.cpp` + `host/ProbeHost.h` + 场景合成 | 脚本建窗口/图层 → 逐像素断言 |

断言脚本在 `cpp_new/host/fixtures/demo/`（夹具根目录，同时被 `storage_probe` 使用），
`tjs_probe <script> <root-path-file>` 的第二个参数是**内容为根路径的文本文件**，
不是路径本身（Windows 命令行是 ANSI 代码页，见技术债 #21）。脚本以抛出
`"<name>-ok"` 结尾表示通过，`tjs_probe` 据此决定退出码。

静态检查：`python cpp_new/tools/check_layer_deps.py cpp_new`（要求 0 违规）。

## 2. 文件约定

分层与依赖方向以 `cpp_new/docs/ARCHITECTURE.md` 为准，提交前运行
`python cpp_new/tools/check_layer_deps.py cpp_new`。

```
cpp_new/
├── tjs/                        权威实现（krkrz 原样，差异见 tjs/VENDOR.md 待补）
├── shim/                       垫层
│   ├── engine/TjsEngine.*      tjs 引擎门面（编码入口、异常映射）
│   ├── engine/ScriptLoader.*   脚本装载唯一入口（解码 / 字节码 / 异常映射）
│   ├── engine/NativeClass.*    原生类注册样板（objthis 为空）
│   ├── text/TjsText.*          编码转换（UTF-8 <-> tjs_char、脚本源解码）
│   ├── classes/Tjs{System,Scripts,Storages,Debug,Plugins}.*   TJS 原生类桥接
│   ├── classes/Tjs{Window,Layer}.*                            窗口/图层桥接（写场景树）
│   ├── classes/TjsWindowInternals.h                           两个原生实例类型 + 窗口存活标记
│   ├── tjs_host/               宿主符号、prelude、targetver、生成的日期词表与消息表
│   ├── TjsBridge.*             装配上下文（唯一写入点是 engine/ScriptRuntime）
│   └── TjsDiagnostics.*        异常定位文本、内置消息表安装、桥接失败抛出
├── engine/                     装配层（唯一同时依赖 shim 与 core 的地方）
│   ├── ScriptRuntime.*         引擎 + 原生类 + 兼容声明的装配，实现 core 的 IScriptRuntime
│   └── CompatProfile.*         "声称支持什么"：预处理器值、原生类列表、脚本前奏
├── debugger/                   调试器子包（规格见 docs/debugger-spec.md，尚未实现）
├── platform/                   抽象层（当前仅有目标骨架，见技术债 #39）
├── visual/
│   ├── Surface/Blend/DrawList/SoftwareBackend/GLBackend   合成与后端
│   ├── Scene.*                 图层树 → DrawList（窗口 → 图层，无 TJS 依赖）
│   └── Image.*/ImageDecoder.*  图片解码（stb_image）
├── core/
│   ├── contracts/ScriptServices.h  宿主能力的小接口集合
│   ├── contracts/IScriptRuntime.h  脚本运行时对引擎的契约
│   ├── api/EngineRuntime.*         对外 API 与生命周期（实现能力接口）
│   ├── io/                         存储、XP3、名字与路径
│   ├── log/                        日志
│   └── module/ debug/              预留：模块注册表、调试接线
├── host/                       测试台（不属于引擎产物）
│   ├── ProbeHost.h             测试台共用的宿主实现（tjs_probe / window_probe）
│   ├── WindowProbe.cpp         场景合成与像素断言
│   └── fixtures/demo/          断言脚本、存储层夹具与位图夹具（layer.bmp）
├── tools/                      代码生成、构建与静态检查
└── docs/                       ARCHITECTURE.md、render-spec.md、debugger-spec.md
```

`host/main.cpp` 不属于引擎，对应 Kotlin 宿主的角色。引擎内部辅助函数写入匿名 namespace，
不新增全局符号。

## 3. 总计划

| # | 步骤 | 内容 | 状态 |
|---|---|---|---|
| 1 | 入口容器 | `Create / OpenGame / Tick / Destroy` + 不透明句柄 + 返回码 | ✅ 完成 |
| 2 | 本地测试台 | `host/main.cpp`，clang++ 直编，9 条用例 | ✅ 完成 |
| 3 | 文件系统校验 | 存在性 / 目录 vs 包 / 路径规范化 / `GameKind` | ✅ 完成 |
| 4 | 日志 | 级别 + 帧号 + 4MiB×3 轮转 + 写盘失败降级（std-only，无 spdlog） | ✅ 完成 |
| 5 | 启动状态 | `StartupState` 状态机 + `startup.tjs` 检查 + 日志内存环 `Drain`（**无线程**，见 §4） | ✅ 完成 |
| 6a | 存储层·目录后端 | `IStream` + `Storage` + 名字归一化 + 目录读取 | ✅ 完成 |
| 6b | 存储层·XP3 索引 | 解析 XP3 目录表：文件名 → 偏移/大小（`Xp3Index` + `xp3_probe`） | ✅ 完成 |
| 6c | 存储层·XP3 读取 | 段表 + zlib 解压（puff）+ `Xp3Stream`；包内字节与 zlib 参照一致 | ✅ 完成 |
| 6d | 存储层·auto-path | 目录模式自动挂载 `*.xp3`；**散装 > 补丁包 > 其它包**（`Xp3Pack` 造夹具验证） | ✅ 完成 |
| 7a | 脚本内核 | vendored krkrz tjs2 + 垫层（prelude / host shim / 生成词表），能解析并执行脚本 | ✅ 完成 |
| 7b | TJS ↔ 引擎桥 | `Storages`、`Scripts`（eval/exec/evalStorage/execStorage）已接入 | ✅ 完成 |
| 7c | TJS ↔ 引擎桥（System） | `getArgument` / `inform` / `informError` / `exit` / `title` / `versionInformation` / `desktopWidth·Height` | ✅ 完成 |
| 7d | TJS ↔ 引擎桥（缺口修复） | 原生类注册方式修正（`tTJSVariant` 的 objthis 必须为空）+ XP3 大小写回退 | ✅ 完成 |
| 7e | **TJS ↔ 引擎桥（续）** | 诊断上下文（#40）、字节码脚本、`Debug`/`Console`、`Plugins` | ✅ 完成 |
| 7f | **装配根与 CMake** | `engine/` 装配层、`ScriptLoader`、能力接口拆分、`CompatProfile`、CMake 目标 + CTest；产品入口与测试台共用装配 | ✅ 完成 |
| 7h | **调试器子包** | `debugger/`：事件契约 + JSONL 录制 + `krkr_debug` 前端（D1 精简版）；断点/单步/DAP 见规格 §5、§9 | ✅ D1 完成 |
| 7g | **`Initialize.tjs` 的运行面** | `Window` / `Layer` 桥接 + `visual/Scene` 合成（7g-1，见 §4.27）；随后 `Storages` 挂载面、`System` 其余成员 | 🔄 7g-1 完成 |
| 8 | 渲染 | 见 **`cpp_new/docs/render-spec.md`**；8a 软件后端 + 图片解码、8b GLES3（桌面逐像素对齐）、8c EGL 代码完成。**8d 起改走 R1：像素与图层语义交由 krkrz 权威实现（`docs/visual-vendor.md`）** | 🔄 8c 待真机 / 8d 转 R1 |
| 9 | 输入 | 指针 / 按键 / 文本 → 脚本事件 | 待做 |
| 10 | 音频 | 至少 WAVE + OGG | 待做 |
| 11 | 换回去 | 用 `cpp_new` 替换 `bridge/engine_api/` 的实现，签名不变，Kotlin 不动 | 待做 |

**节奏约定**：每次只做一行，做完能编、能跑、能贴出输出，再进下一行。不许并行铺开。

---

## 4. 已完成的步骤记录

### 4.1 第 3 步：文件系统校验（`6c1c0a2` 一类的提交）

七个要点，都做过验证：

1. `std::error_code ec`；**所有 `fs::` 调用都传 `ec`，绝不使用抛异常的重载**（铁律 #4）
2. `fs::exists` 失败 → `IoError`；不存在 → `IoError`（消息带原路径）
3. `fs::weakly_canonical` 规范化；失败降级 `fs::absolute`；再失败用原路径
4. `fs::is_directory` 为真 → `Directory`，路径末尾补 `/`
5. 否则看扩展名（`.xp3/.zip/.7z/.tar`，**大小写不敏感**）→ `Archive`
6. 都不是 → `IoError`
7. 路径进出都走 `u8string` / `generic_u8string()`：任何平台都按 UTF-8 解释，分隔符永远是 `/`

三个细节（踩过才知道）：

| 细节 | 为什么 |
|---|---|
| `std::tolower(static_cast<unsigned char>(c))` | `char` 在 ARM 上有符号，UTF-8 中文字节是负数，传给 `<cctype>` 是未定义行为 |
| `weakly_canonical` 而不是 `canonical` | 前者不要求路径存在；后者要求，且对包文件路径更苛刻 |
| 不要手写 `ends_with(".xp3") \|\| ends_with(".XP3")` | 老代码只认两种大小写，`game.Xp3` 会漏 |

### 4.2 第 4 步：日志器（`cpp_new/core/log/`，`krkr::log`）

- **零依赖**：只用标准库。`spdlog` 到第 11 步换回 ABI 时才处理，不进 `cpp_new`。
- 行格式：`[frame=N][level] message` + 换行符（一行一条、UTF-8、以 `\n` 结尾 → 宿主能按行切）。
- 级别：`debug < info < warn < error < off`，默认 `info`；`warn`/`error` **立即 flush**，其余每帧 flush 一次。
- 落盘：4 MiB × 保留 3 个历史文件（**ABI 契约**），只在行边界轮转，不会把一行切两半。
- 写盘失败：**静默降级**到 stdout，原因进 `GetFileError()`，游戏照跑（`engine_set_log_file_path(NULL)` 必须返回 OK）。
- 头文件用 pimpl：`FILE*` / `mutex` / `deque` 都不出现在 `Logger.h` 里。
- 头文件里定义的函数必须 `inline` / `constexpr` / `template` —— `LevelName` 少写一个 `constexpr` 就会在**链接期**报 multiple definition。

### 4.3 第 5 步：启动状态（**没有线程，这是刻意的**）

```text
OpenGame():  IDLE ─路径校验通过─► RUNNING ─► 查 <root>/startup.tjs ─┬─► SUCCEEDED  rc=0
                                                                    └─► FAILED     rc=-4
Tick():      状态 != SUCCEEDED → 返回 StartupPending(-7)   ← 同步实现下暂时走不到，留契约
Destroy():   Flush 日志 → delete
```

- 数值对齐 ABI：`StartupState` = `engine_startup_state_t`（0/1/2/3），`StartupPending = -7` = `ENGINE_RESULT_STARTUP_PENDING`。
- 状态字段是 `std::atomic<std::uint32_t>`：ABI 没承诺宿主只在 tick 线程上查它，但这一步**不需要锁**。
- 包模式（`kind=2`）：包内读文件要等第 6 步，所以记一条 `warn` 后直接 `SUCCEEDED`（不判失败）。
- 日志器加了 256 行内存环 + `Drain(out, cap)`，对应 ABI 的 `engine_drain_startup_logs`；只写完整的行。

**为什么这一步不上 worker 线程**：

1. 现在"启动"只有一次 `exists()`，微秒级，同步完全够；
2. 宿主侧 `EngineSession` 用**一条 HandlerThread** 同时承载 `openGame` 和 tick（`post {}` 就是投到这条线程），
   所以引擎在调用线程上同步做启动 = 阻塞 tick。今天这点耗时无所谓，真接 TJS 就未必；
3. 引入线程的动机必须是**实测数据**，不是"接口看起来该有"。触发条件见 §5 技术债 #10。

### 4.4 第 6a 刀：存储层·目录后端（`cpp_new/core/io/`，`krkr::io`）

```text
Storage(root)  ──名字归一化──►  root / name  ──►  OpenDirectoryFile  ──►  IStream
                 (\→/ 合 //)                              (ifstream, 只读)
```

- 接口：`IStream` 只有 `Read` / `Seek`（绝对偏移）/ `GetSize` / `GetPosition`。
- 所有权：`Open()` 返回 `std::unique_ptr<IStream>`，**调用方持有**。不用 AddRef/Release。
- 错误模型：不抛异常。`Open` 返回 `nullptr` + `GetLastErrorText()`；`Read` 返回 -1 失败 / 0 EOF。
- 名字规则：`\` → `/`、合并重复 `/`、去掉开头 `./`；**`..` 放行**（见 §5 #13）。
- 目录后端必须区分「不存在」「是目录」「打不开」三种错误，不能返回 `GetSize()==0` 的假流。
- `PathFromUtf8/PathToUtf8` 搬到 `io/PathUtil`，`EngineRuntime` 改成 `using krkr::io::...`，全引擎只有一份编码实现。
- 独立测试台 `host/StorageProbe.cpp`（18 条检查，含哨兵字节查越界）。

这一刀**没做**（有意，不要顺手加）：XP3/ZIP/7z 后端、目录列举、写文件、
段缓存、`tTVPStorageMedia` 那套多媒体注册表 / auto-path / 补丁优先级。

### 4.5 第 6a 刀的验收（两个测试台）

`storage_probe`（18 条检查）：mount / 尺寸 == 实读字节 / EOF 返回 0 / `Seek(0)` 后逐字节一致 /
读超尾部只返回剩余且不碰哨兵字节 / `Seek(size+1)` 与 `Seek(-1)` 被拒 /
`sub\\a.tjs` 分隔符归一化 / 不存在的名字 / 目录当文件开 / `../escape.txt` 放行。

`krkr_next`：目录模式日志出现 `startup: script size=23 head=// Demo startup script.`；
缺脚本 → `-4` + `startup=3`；包模式 → warn + `startup=2`。

### 4.6 第 6b 刀：XP3 索引（`cpp_new/core/io/archive/Xp3Index`）

只读目录表，**不读数据、不解压**。格式按老树 `XP3Archive.cpp` 的布局实现：

```text
magic(11) + index_offset(u64)
  index_offset 处: flag(1) + index_size(u64) + payload
  payload: “File”+u64 size { “info”+u64 size { flags(4) org(8) arc(8) nameLen(2, 有符号) name(UTF-16LE) }
                                “segm”+u64 size { n × 28: flags(4) start(8) org(8) arc(8) }
                                “aldr”+u64 size { adler32 } }
```

实现要点（全部是“被伪造的包也不能崩”的告警）：

- 每个读偏移都做边界检查；名字长度是**有符号** 16 位，负值/越界直接判损坏。
- 索引上限 64 MiB（真包 2.7 MiB / 21123 条），防止伪造长度变成一次大 malloc。
- 段 `start > indexOffset` 判损坏；`offsetInFile` 累加防溢出。
- 重名：保留第一条，但计数暴露（`GetDuplicateCount`），不静默吞。
- 名字一律 UTF-16LE → UTF-8（含代理对），查表时两侧都过 `NormalizeStorageName`。

**真实包验收（用户提供的游戏）**：

| 包 | 大小 | 条目 | 结果 |
|---|---:|---:|---|
| `patch@r484.xp3` | 3.4 KB | 12 | 与 Python 参照实现**逐字节一致** |
| `data.xp3` | 4.7 GB | 21123 | 与 Python 参照实现**逐字节一致**（含 9 个日文名、3 个带 `\\` 的子目录名） |

损坏输入全部干净报错（不 crash）：截断 / index_offset 越界 / index_size 超限 /
chunk 长度越界 / 魔数错 / 非 XP3 文件。

### 4.7 第 6b 刀顺带修掉的真 bug

`PathFromUtf8` 在 Windows 上遇到**非法 UTF-8** 会抛 `std::range_error`
（`path(u8string)` 要做 UTF-8 → UTF-16 转换），而调用方没接 → `std::terminate` → 进程 `abort()`。
发现方式：用带日文的路径跑测试台，发现退出码 3、零输出（abort 的退出码）。

修法：`PathFromUtf8` 内部 `try/catch` 吞掉，返回**空路径**；所有调用方必须检查 `path.empty()`：

- `EngineRuntime::OpenGame` → `-1 game root is not valid UTF-8`
- `Storage` 根目录/文件名 → 同样挡在 `path` 构造之前
- `Xp3Index::Load` → `archive path is not valid UTF-8`

这也是“入口层不抛异常”这条铁律第一次被真正踩到 —— 之前只是写在文档里。

### 4.8 第 6c 刀：从包里读字节（`Inflate` + `Xp3Stream` + `Storage` 归档后端）

```text
Storage(data.xp3) ──► Xp3Index（目录表）
Open("startup.tjs") ─► Xp3Stream ─► 逐段：seekg(start) + read(arcSize)
                                    └► Inflate(Zlib) ─► puff ─► 解压后的段
                                          （raw 段直接 memcpy）
```

**解压器的选择**（你拍板“若跨平台好且小就引入”）：

| 方案 | 源码 | 跨平台一致 | 构建影响 |
|---|---|---|---|
| **puff（已选）** | 840 行 / 38 KB / ~4 KB 目标码 | ✅ 三平台完全一致 | 零：一个 .c 直接进编译命令 |
| 系统 zlib（`-lz`） | 0 | ❌ **本机 MinGW 没有**，NDK/Termux 有 | 打断“一条命令编译” |
| zlib inflate 子集自带 | ~200 KB | ✅ | 零 |

选 puff 的真正理由不是体积，而是“链系统库 ≠ 跨平台一致”：
Windows 上没装 zlib 就编不过，而 vendored 源码在哪都一样。速度慢 2–4 倍，
但只影响解压，不影响正确性；将来要提速只改 `Inflate.cpp`。

实现要点：

- XP3 的段是 **zlib 格式**（2 字节头 + deflate + adler32），不是裸 deflate；
  `Inflate` 里校验了 `CM=8`、头校验位、不支持 preset dictionary，并**校验 adler32**。
- `puff.c` 用 C++ 编译器编，所以 `Inflate.cpp` 里**不能写 `extern "C"`**（符号名对不上）。
- `Xp3Stream` **拷贝**段表，不持有 `Xp3Entry*` —— 否则流比索引活得久就是悬空指针。
- 打开时校验“段表必须连续且总和 == orgSize”，不连续直接报错，不带着矛盾去读。

**真实包验收（与 Python `zlib.decompress` 参照逐字节 `cmp`）**：

| 条目 | 大小 | 类型 | 结果 |
|---|---:|---|---|
| `startup.tjs` | 13022 | zlib | ✅ 一致 |
| `system\\initialize.tjs` | 33916 | zlib、反斜杠名 | ✅ 一致 |
| `title_bg.png` | 3159521 | zlib | ✅ 一致 |
| `ev_mv001_02_01.mpg` | 3287441 | **未压缩（raw）** | ✅ 一致 |
| `cha027_up.pimg` | 34866852 | zlib、34 MB | ✅ 一致 |
| 上述最大文件的中间 1000 字节 | 1000 | `Seek+Read` 越段片段 | ✅ 一致 |

损坏输入：段数据被改坏 → `read error mid-stream`（adler32 拦住），不 crash；
包被截断 → `index offset out of range`。

**引擎端到端**（把 `data.xp3` 直接当 game root）：

```
startup: storage mounted (archive=1)
startup: script size=13022 head=..t.h.i.s...k.i.r.i.k.i.r.i.z. .=. .+.S.c.r.i.p.t.s...e.v.a.l.(
startup = 2
```

head 里那些 `.` 是因为 `startup.tjs` 本身是 **UTF-16LE** TJS 源码（第 7 步接 TJS 时才会真正解），
字节本身已与 zlib 参照逐字节一致。

### 4.9 第 6d 刀：auto-path（多挂载 + 包优先级）

```text
Storage(root)
  ├─ root 是文件  ──► mounts = [ xp3 自己 ]
  └─ root 是目录  ──► mounts = [ 目录本身, ★补丁包(名字含 patch)…, ★其它包… ]
                                 先命中者优先（FirstRegisteredWins）
```

规则（与老层 `StoragePolicy` 默认值对齐）：

| 规则 | 取值 | 依据 |
|---|---|---|
| 优先级顺序 | 散装（目录）> 补丁包 > 其它包 | 老层：`projectDir` 先注册；`BoostAutoMountPaths` 把补丁插到第一条**档案**条目之前 |
| 补丁判定 | 文件名（小写）含 `patch` | `PatchNameRule::SubstringContainsPatch` |
| 同级排序 | 自然序（`patch2` < `patch10`） | 老树用 `opendir` 顺序，不可复现；这里显式排序 |
| 坏包 | 跳过 + warn，不影响其它包 | 一个坏补丁包不该让游戏开不了 |
| 扫描深度 | **只扫根目录一层**，不递归 | 老层也只扫一层；递归会把存档目录里的包误挂 |
| 失败语义 | 三态：NotFound 继续下一个；**找到了但读失败 → 立刻报错，不回落** | 静默回落会把“补丁损坏”伪装成“补丁未生效” |

验收：

| 场景 | 结果 |
|---|---|
| 真游戏目录（`data.xp3` + `patch@r484.xp3`） | `mount scan: 1 patch + 1 other` → `startup = 2`、`script size=13022` |
| `data.xp3` 与 `patch.xp3` 都有 `a.txt` | 读到 `patch` |
| 再加散装 `a.txt` | 读到 `disk`（散装赢过所有包） |
| `patch.xp3` 截断到 100 字节 | `skipping bad archive patch.xp3: index (96 bytes) runs past end of file`，`a.txt` 回落读到 `data` |
| 删掉 `patch.xp3` | 读到 `data` |

测试基建：`host/Xp3Pack.cpp`（`xp3pack <out.xp3> <file>...`）造 **raw 段**的 XP3 夹具 ——
因为真游戏的两个包没有同名文件，不造夹具就永远验证不了优先级。
夹具自身先用 `xp3_probe` 反查（entries / 名字 / 大小）再拿去做断言。

---

### 4.10 TJS2 的引入方式（7a）

`core/tjs2/` 为 krkrz 原样拷贝，**不承载任何适配逻辑**。差异全部位于 `core/script/`：

| 位置 | 职责 |
|---|---|
| `TjsEngine.h/.cpp` | 门面：上层仅见 UTF-8，不引用 tjs2 类型 |
| `TjsHostShim.cpp` | 宿主符号：调试设施空实现、窄字符转换改为 UTF-8 |
| `TjsRegExpShim.cpp` | 未引入 oniguruma，`RegExp` 不可用 |
| `shim/tjs_prelude.h` | `-include` 前置：清理 Win32 宏、补齐标准头与数学常量、固定包含顺序 |
| `generated/tjsDateWordMap.cc` | 由 `tools/gen_tjs_date_word_map.py` 生成（上游本就在构建期生成） |

vendored 源码仅有两处修改，均属上游在非 MSVC 工具链下从未编译的分支：

| 文件 | 修改 | 原因 |
|---|---|---|
| `tjsArray.cpp` | `stream>Write(` → `stream.Write(` | 上游拼写错误，仅存在于非 CRLF 分支 |
| `tjsConfig.cpp` | `TJSRestoreFPUE()` 内 `if(!TJSFPUInit)` 补 `#if` 守卫 | 该变量只在 MSVC 分支声明 |

编译参数即垫层开关：

```
-DUNICODE -D_UNICODE -DTJS_NO_REGEXP
-include core/script/shim/tjs_prelude.h
-I core/script/generated -I core/script/shim
排除 tjsDebug.cpp、tjsRegExp.cpp
```

`Storages` 桥（7b 的第一个类）语义：`isExistentStorage` / `getPlacedPath` /
`extractStorage*` 直接映射到 `krkr::io::Storage`；`addAutoPath` / `removeAutoPath` /
`clearArchiveCache` 接受调用并记录，不改变挂载表（挂载集合在打开游戏时确定）。

### 4.11 Scripts 桥与真实的脚本推进（7b）

`core/script/TjsScripts.cpp` 实现 `eval`、`exec`、`evalStorage`、`execStorage`、`getTraceString`，
全部经由 `krkr::io::Storage` 读取脚本，编码转换集中在 `TjsText.cpp`。原生类共享的依赖
（引擎实例、存储实例）收拢在 `TjsBridge` 单例，替代此前的文件级指针。

实测：真游戏 `startup.tjs`（13022 字节，UTF-16LE）由原先在第 1 行失败并因缺少 `Scripts` 中断，
推进到第 142 行：

```
==== An exception occured at py_startup.tjs(142) ====
#(142) var local0 = System.getArgument("-debugwin") != "no";
```

即 `Scripts.eval("@if(kirikiriz)1@endif")` 已正确求值，前 141 行脚本已执行。
下一个缺口是 `System`。

### 4.12 System 桥与宿主抽象（7c）

脚本层通过 `core/script/ScriptHost.h` 定义对宿主的最小契约（启动参数、输出、退出请求、
标题、表面尺寸、版本信息），引擎入口 `EngineRuntime` 实现该契约。方向为
"脚本层依赖接口、env 层提供实现"，桥接实现不感知窗口与生命周期细节。

`EngineResult` 新增 `GameTerminated = -6`（与 ABI 的 `ENGINE_RESULT_GAME_TERMINATED` 一致）：
`System.exit()` 置位退出请求，`Tick` 返回该码。

实测：真游戏 `startup.tjs` 由第 142 行继续执行，当前缺口推进到匿名函数内部：

```
An exception occured at py_startup.tjs(6)[(function expression) (anonymous)]
#(6)     Scripts.new9Storage(*);
```

该方法名疑似脚本侧自定义别名，需先确认真实语义再决定是补齐别名字符串
还是实现对应功能。

### 4.13 三层结构落地（重构）

目录按 `cpp_new/docs/ARCHITECTURE.md` 重排：`tjs/`（权威实现）、`shim/`（垫层）、
`platform/`（抽象层，骨架已建）、`core/`（基座）。契约 `IScriptHost` 由垫层下沉至
`core/contracts`，`EngineRuntime` 迁至 `core/api`，`core/script`、`core/env`、`core/base`、
`core/utils` 已移除。`tools/check_layer_deps.py` 校验依赖方向，当前 0 违规。

### 4.14 原生类注册缺陷（真脚本第三个缺口）

现象：脚本保存原生方法后覆盖同名成员，再经别名调用原方法即失败。

```tjs
Scripts.new9Storage = Scripts.execStorage;
Scripts.execStorage = function(arg0) { Scripts.new9Storage(*); };
```

根因：注册时使用了 `tTJSVariant(cls, cls)`，把类对象同时作为 objthis。
权威实现的标准范式是 `tTJSVariant(dsp, NULL)`，objthis 必须为空（见 `tjs/tjs.cpp:151-154`）。
修正后上述模式恢复正常，`typeof` 也不再返回 `Object`。

同时修正：`Xp3Index` 增加大小写不敏感回退，对齐 Windows 文件系统语义
（真脚本请求 `system/Initialize.tjs`，包内为 `system\initialize.tjs`）。

### 4.15 测试台恢复：桥接层重新接上（`host/TjsProbe.cpp`）

三层重构后的 `tjs_probe` 只创建裸 `tTJS`，没有注册任何原生类，断言脚本全部失败。
现在它承担 "宿主" 角色：读根路径文件 → 挂载 `krkr::io::Storage` → 创建 `Engine` →
注册 `Storages` / `Scripts` / `System` / `Debug` / `Plugins`，并提供一个
`IScriptHost` 实现（启动参数、`inform`、退出请求、标题、表面尺寸）。

同时把测试基础设施入库：

| 文件 | 作用 |
|---|---|
| `tools/build_probes.sh` | 四个产物的唯一构建入口（清单不再手敲） |
| `tools/run_probes.sh` | 构建 + 全部断言（目录/包存储、启动状态、8+2 个脚本、可选真游戏） |
| `host/fixtures/demo/` | 断言脚本与存储层夹具（`storage_probe` 直接用这个目录） |

脚本以抛出 `"<name>-ok"` 结尾表示通过；`tjs_probe` 据此返回 0/1，使脚本断言可被 CI 直接使用。

### 4.16 空消息的根因与修法（技术债 #40）

两层原因，分别处理：

1. **消息表为空**。`cpp_new/tjs/tjsErrorInc.h` 是上游 gentext 的产物，默认文本全部是
   `NULL`，所以 `TJS_eTJSError` / `TJSThrowFrom_tjs_error` 抛出的异常消息恒为空串
   （`Member "Plugins" does not exist` 也无法显示）。上游为此提供运行期接口
   `TJSAssignMessage`，于是把消息文本从参照实现取出来，由
   `tools/gen_tjs_message_map.py` 生成 `shim/tjs_host/generated/tjsMessageMap.inc`，
   `Engine` 构造时安装。**`tjs/` 未作任何修改**。
2. **桥接层丢失定位信息**。`shim/TjsDiagnostics` 统一把异常格式化为
   `上下文 + 消息 + [script=<块名> line=<行号> trace=<调用链>]`；`Scripts` 的
   `RunSource` 只记录上下文，然后把异常**原样重抛**，由虚拟机转成脚本侧异常，
   内层 trace 不再被吞掉。存储打开/读取失败改用 `ThrowScriptException`，
   脚本侧看到的是 `Scripts.execStorage("名字"): 原因`，而不是空消息。

实测：迁移前 `Scripts: system/Initialize.tjs:`（空），迁移后

```
Scripts.execStorage("system/Initialize.tjs"): Syntax error (syntax error)
  [script=system/Initialize.tjs line=1]
exec = script exception: build/py_startup.utf8.tjs: ...
  trace=build/py_startup.utf8.tjs(6)[(function expression) (anonymous)]
    <-- build/py_startup.utf8.tjs(147)[(top level script) global]
```

### 4.17 第 1 行缺口的真实身份：`Initialize.tjs` 是字节码

有了 4.16 的诊断，缺口直接现形：包里 `system/Initialize.tjs` 的头 4 字节是 `TJS2`、
版本头 `100\0`，是预编译字节码而不是文本源码。`execStorage` 按文本解析必然第 1 行语法错误。

修法：`RunSource` 先用上游自带的 `tTJSByteCodeLoader::IsTJS2ByteCode` 判定，字节码走
`tTJS::LoadByteCode(buffer, size, result, context, name)`，文本仍走 `ExecScript` /
`EvalExpression`。解码从调用点移入 `RunSource`，两条路径各自保持原样。

推进结果：字节码加载成功，失败点后移到 `Member "Debug" does not exist`，即
`Initialize.tjs` 第 1 行需要 `Debug`。

### 4.18 `Console` / `Debug` 与 `Plugins`

`shim/classes/TjsDebug`：

| 成员 | 行为 |
|---|---|
| `message` / `notice` / `logAsError` | 映射到 `core/log` 的 info / warn / error |
| `logLocation` | 保存路径；`startLogToFile` 用它调 `Logger::SetFile` |
| `logToFileOnError` / `clearLogFileOnError` | 只保存取值（错误触发重定向属宿主管辖） |
| `getLastLog(n)` | 新增 `Logger::GetRecentLines` 契约（不消耗内存环） |
| `console` / `controller` | 返回 `Console` / `Controller` 类对象，`visible` 恒为 0；写入时记录"平台无窗口" |
| `addLoggingHandler` / `removeLoggingHandler` | 需要日志→脚本的回调契约，显式返回 `TJS_E_NOTIMPL` 并记录 |

`shim/classes/TjsPlugins`：静态注册表先行（`RegisterStaticPlugin` 供以后的静态插件调用），
`link` 命中即成功、未命中抛带名字的异常；`unlink` 对静态插件返回 false 并记录原因；
`getList` 返回名字数组（不区分大小写的基名，重复登记去重）。动态库加载（`.dll` / `.so`）
尚未实现，`linkZ` / `CanLoadPlugin` 也未提供，见 §5。

验收：新增 `t_debug.tjs` / `t_plugins.tjs` 两个断言脚本（正常路径 + 错误路径），与既有
8 个脚本一起在 `run_probes.sh` 中运行，`tjs/` 零修改。

### 4.19 装配根（`cpp_new/engine/`）与 CMake

三层落地后出现一个结构性问题：**"谁把引擎装起来"没有家**。装配逻辑留在测试台里，
重构时随旧 `core/script` 一起消失，`tjs_probe` 变成裸 `tTJS` 且无人发现；
产品入口 `EngineRuntime` 至今不会跑脚本，第 11 步没有落脚点。

新增 `cpp_new/engine/`（装配层），依赖方向变成：

```
core ← platform      core ← shim ← engine ← host
                         ↑
                        tjs
```

| 文件 | 职责 |
|---|---|
| `engine/ScriptRuntime.*` | 装配引擎 + 原生类 + 兼容声明；实现 `core/contracts/IScriptRuntime` |
| `engine/CompatProfile.*` | 兼容声明：预处理器值（`kirikiriz=0`）、原生类列表、脚本前奏 |
| `shim/engine/ScriptLoader.*` | 脚本装载唯一入口：解码、字节码分流、异常格式化 |
| `shim/engine/NativeClass.*` | 原生类注册样板（`tTJSVariant(cls, nullptr)`，objthis 为空） |
| `shim/TjsBridge.*` | 装配上下文；**唯一写入点**是 `ScriptRuntime::Startup` |

`EngineRuntime` 通过 `AttachScriptRuntime()` 接收运行时，自身只认识 `IScriptRuntime`，
不引用 shim/tjs。`host/main.cpp`（产品入口）与 `host/TjsProbe.cpp`（测试台）现在共用同一条装配，
装配再被删就会在 CTest 里立刻变红。

CMake（技术债 #38）与分层一一对应：`krkr_tjs`、`krkr_tjs_host`（宿主符号，链接期由 shim 提供）、
`krkr_core`、`krkr_platform`（当前为 INTERFACE 占位）、`krkr_shim`、`krkr_engine`；
测试台链接 `krkr_engine`。全部断言注册进 CTest（14 项，`real_startup` 暂 DISABLED，见 §5 #49）。

### 4.20 能力接口拆分与 `System` 路径成员

`IScriptHost` 已删除，改为 `core/contracts/ScriptServices.h` 的小接口：
`ILaunchArguments` / `IOutput` / `IAppControl` / `IPlatformInfo` / `IPathProvider` / `IWindowInfo`，
由 `HostServices` 聚合。空指针 = 能力未提供，桥接层抛带上下文的异常，不再用 0 / 空串代替
"未实现"。

`System` 因此补齐了 `Initialize.tjs` 需要的成员：`exePath` / `arcPath` / `dataPath` /
`personalPath` / `savedGamesPath` / `osName` / `platformName` / `versionString` /
`getTickCount` / `setArgument`。语义对齐参照实现：

| 成员 | 来源 | 约定 |
|---|---|---|
| `exePath` / `arcPath` | `IPathProvider::GetExecutableDirectory` | 可执行文件目录，以分隔符结尾（参照实现两者同值） |
| `dataPath` | `GetDataDirectory` | 目录模式 = 游戏目录；包模式 = 包所在目录；以 `/` 结尾 |
| `personalPath` / `savedGamesPath` | 宿主注入 | 未注入时显式失败（Android 由第 11 步接） |
| `osName` / `platformName` | 编译期常量 | `Windows` / `Android` / `Linux`；`ARM64` / `x86_64` |
| `getTickCount` | `steady_clock` | 进程启动为 0 的毫秒数 |
| `setArgument` | `ILaunchArguments::SetLaunchArgument` | 覆盖同名项（`-k v` 与 `-k=v` 两种形式） |

实测：真游戏从 `System.arcPath` 一路推进到 `Member "Window" does not exist`（第 8 步的门槛）。

### 4.21 调试器子包（D1 精简版）

结构见 `docs/debugger-spec.md` §12。要点：

- 事件契约在 `core/contracts/DebugServices.h`，通过 `HostServices::debug` 传递，
  **没有新增装配参数**，`ScriptContext` 自动带出；未挂接时各发送点只有一次空指针检查。
- 事件源四处：`ScriptLoader`（begin/end/fail）、`Storage`（open + `DescribeMounts`）、
  `Logger`（log.line）、`EngineRuntime`（state/failure/mount/exit）。
- `debugger/agent/EventRecorder` 写 JSON Lines，`debugger/cli` 的 `krkr_debug` 做汇总与过滤；
  `KRKR_DEBUG_EVENTS=<file>` 开启，未设置时整条链路不参与。
- CTest 新增 5 项（录制 1 + 事件断言 4），当前 19/19 通过。

刻意不做控制平面：断点/单步需要上游 `ENABLE_DEBUGGER` 钩子（§5 已验证），
但它引入"暂停阻塞引擎线程"的语义，应与第 9 步的 tick/输入模型一起设计。

### 4.22 调试器子包与渲染 8a 起步

- 调试器（7h）按规格 §12 落地 D1 精简版：事件契约 + JSONL 录制 + `krkr_debug`
  汇总/过滤，CTest 5 项；断点/单步与 DAP 保留为后续阶段。
- 渲染（第 8 步）从 8a 的像素权威开始：`visual/Surface`（RGBA8888、FillRect、
  `Composite` 的 Opaque/Alpha/Additive/Subtractive/Multiplicative、`SavePpm`），
  `host/RenderProbe.cpp` 用手算期望值断言，`ctest` 项 `render_surface`。
  尚未做：DrawList（等 `Window`/`Layer` 形状确定）、纹理与采样、其余 24 种图层类型、
  PNG 解码（需引入 stb_image）。

### 4.23 渲染 8a：DrawList + 软件后端 + 平台呈现

- `visual/`：`Surface`（RGBA8888）、`Blend`（算子唯一实现）、`DrawList`（纹理表 + DrawItem）、
  `SoftwareBackend`（轴对齐 + 最近邻；非轴对齐项跳过并计数）。
- `core/contracts/IRenderTarget.h`：`Present(pixels,w,h,stride)`，visual 与 platform 的唯一交界。
- `platform/`：`PpmTarget`（离屏，CI）、`Win32WindowTarget`（GDI，`host/RenderWindow` 手工验证，
  实测 60 帧无异常）、`AndroidTarget`（8c 前显式失败）。
- 断言：`host/RenderProbe.cpp` 覆盖原语与 DrawList 两条通道（纯色 / 纹理 / 最近邻缩放 /
  clip / 旋转项跳过 / PPM 输出），`ctest` 项 `render_surface`。

尚未做：双线性与仿射采样、其余 24 种图层类型、遮罩（dfMask）、effect/filter、脏矩形差分、
纹理上传与 GLES3 后端、图像解码（PNG/TLG）、字体。

### 4.24 渲染 8b：GLES3 后端与双平台构建

- `core/contracts/GLApi.h`：函数指针表。放在 core 是因为 `platform` 不能引用 `visual`：
  平台负责装载、visual 负责消费，二者只在这张表上相遇。
- `visual/GLBackend.*`：GLES3 风格（vertex/fragment shader、VAO、VBO、纹理上传、scissor），
  覆盖 8a 的 5 种算子。
- `platform/desktop/gl/GLContext.*`：WGL 隐藏窗口 + 3.3 core 上下文 + 入口点装载。
- `host/RenderGlProbe.cpp`：同一 DrawList 分别走软件与 GL，逐像素比较；实测 **0 个不一致像素**。
- Android：`cmake --preset android-debug`（NDK r27d, arm64-v8a）能编出 `krkr_visual` / `krkr_platform`。

两个踩过的点：

| 现象 | 原因 | 处理 |
|---|---|---|
| GL 画完什么都没有，`glDrawArrays` 无效果 | core profile 下没有绑定 VAO 是无效操作 | 后端自己持有 VAO，`Initialize` 里创建并绑定 |
| Clang(NDK) 编不过 vendored tjs | `register`、取临时量地址、dependent base 的未限定查找、`std::binary_function` 已移除 | 前三个用编译参数，`binary_function` 在 prelude 补 libc++ 版本；`tjsHashSearch.h` 加 `this->` 记入 `tjs/VENDOR.md` #3 |

### 4.25 渲染 8c：EGL 上下文与 surface 生命周期

- `core/contracts/IGLContext.h`：桌面向 WGL、Android 向 EGL 各自实现，`host/engine` 只认接口。
- `platform/android/AndroidGLContext`：`eglGetDisplay/Initialize/BindAPI(ES3)/ChooseConfig/
  CreateWindowSurface/CreateContext(3)`，`Resize` 重建 surface；入口点用
  `eglGetProcAddress` + `dlsym` 装载。
- `platform/android/AndroidSurface`：`krkr_android_surface_attach/resize/detach/valid` 与
  `GetAndroidGLContext()`，是 JNI 唯一需要认识的入口。
- 两个平台的加载器共用 `core/contracts/GLApi.h` 的 `KRKR_GL_FUNCTION_LIST` X-macro，
  新增入口点只改一处，不会一边有一边没有。

验证：Android arm64 构建通过，`libkrkr_platform.a` 内含 `krkr_android_surface_attach` 与
`GetAndroidGLContext()`；桌面 `render_gl` 仍 21/21 通过。

### 4.26 图片解码（8a 验收）

- `visual/Image` + `visual/ImageDecoder`：按 magic 分派，只吃字节；PNG/JPEG/BMP 由
  `stb_image` v2.32（vendored，单文件，仅启用三种格式）解码。
- 真包验收：`title_bg.png` 1920×1080 解码结果与 Pillow 逐像素一致（FNV-1a 相同），
  再经软件后端合成写 PPM；`ctest` 项 `image_decode`。
- Windows 运行验证：`image_window <root-path-file> title_bg.png 120`（窗口显示真素材 + 半透明条）。

### 4.27 第 7g-1 刀：`Window` / `Layer` 桥接与场景合成

`Initialize.tjs` 的门槛是脚本要创建窗口与图层。这一刀把"脚本参数 → 图层树 → 像素"
整条链路接上，验收方式是"脚本设参数 → 宿主逐像素断言"，不是"脚本没报错"。

| 位置 | 内容 |
|---|---|
| `visual/Scene.*` | 场景树 `SceneNode` 与 `Scene::BuildDrawList`（窗口根 → 图层树 → DrawList），附 `SceneBuildStats` |
| `shim/classes/TjsWindow.*` | `Window` 类（几何、caption、visible、zoom、`add`/`remove`、`primaryLayer`、`mainWindow`）+ 窗口登记表 `GetPrimaryScene()` |
| `shim/classes/TjsLayer.*` | `Layer` 类（构造、几何、opacity、type、clip、`loadImages`、`setSizeToImageSize`、`bringToFront`/`moveBefore` 等） |
| `shim/classes/TjsWindowInternals.h` | 两个原生实例类型与窗口存活标记（跨文件共享，不进公开头） |
| `host/ProbeHost.h` | 从 `tjs_probe` 抽出的宿主实现，`window_probe` 与之共用 |
| `host/WindowProbe.cpp` | 跑场景脚本 → 合成 → 软件后端 → 手算像素断言 + PPM |
| `host/fixtures/demo/layer.bmp`、`t_window.tjs` | 8×8 四色位图与"两层 + 错误路径"的场景脚本 |

语义按参照实现对齐（不是自创）：

| 规则 | 取值 | 依据 |
|---|---|---|
| 图层默认值 | `type = ltAlpha`、`visible = false`、32×32、全透明默认位图 | `tTJSNI_BaseLayer` 构造函数 |
| 顶点语义 | `new Layer(window, null)` = 主图层；`new Layer(window, parent)` = 父层的子层 | `tTJSNI_BaseLayer::Construct` |
| `Window.add(obj)` | 只钉住对象不让它被回收，不改变树结构 | `tTJSNI_BaseWindow::Add`（ObjectVector） |
| `left`/`top` | 相对父层原点 | 参照实现的图层几何 |
| `clip` | 只作用于本层四边形，不下传给子层 | `SetClip` 是图层自身属性 |
| `visible` | 为假时本层与子层都不画 | 参照实现的 `nodeVisible` |
| `opacity` | 只作用于本层，不向子层累积 | 图层 opacity 的定义 |
| 未实现的混合算子 | 跳过自身 + 计入 `nodesUnsupported`，子层照画 | "宁可少画，不可画错" |

三个踩过的点：

1. **`CreateNativeInstance()` 必须 override**。不写它，`tTJSNativeClass::CreateNew` 会把空实例登记进对象，
   构造时 `NativeInstanceSupport(GETINSTANCE)` 取回空指针，脚本只看到一句 `Invalid object context`
   （`TJS_E_NATIVECLASSCRASH`）。此前只有静态方法的类（`System` 等）不会暴露这个问题。
2. **断言脚本要 `throw new Exception(...)`**，不能 `throw "字符串"`。纯字符串抛出到达宿主只剩
   `script exception`，`-ok` 标记会丢，CTest 因此变红（此前 `t_*` 恰好都用了 Exception）。
3. **`Window` / `Layer` 互相持引用**。`layer.window` 是 AddRef 的 TJS 对象，`window.add(layer)` 又钉住图层，
   构成环。参照实现同样不回收窗口（窗口活到进程结束），这里接受，记入技术债 #65。

验收（CTest `window_scene`，全部 21 项通过）：

```
nodes=3 emitted=2 hidden=0 unsupported=0 clipped=0
PASS  opaque layer paints its red quadrant (pixel 4,2 = 0xFFFF0000, expected 0xFFFF0000)
PASS  clip drops the green quadrant (pixel 8,2 = 0xFF000000, expected 0xFF000000)
PASS  child layer blends red at opacity 128 (pixel 12,10 = 0xFF800000, expected 0xFF800000)
```

期望值是手算的（`ltAlpha` 下 `a = (255*128+127)/255 = 128`，`out = a`），
不是拿另一份渲染结果对照——那样两边会一起错。

## 5. 已知问题 / 技术债

| # | 问题 | 处理 |
|---|---|---|
| 1 | `main.cpp:25` 少一个右括号，编译失败 | ✅ 已修 |
| 2 | 头文件里 `#include <cstdio>` 是无用的 | ✅ 已删 |
| 3 | 函数定义后多余的 `;` | ✅ 已清 |
| 4 | `EngineRuntime.h` 缺 `#include <cstdint>`，靠 `<string>` 传递包含 | ✅ 已补 |
| 5 | 目录 `core/env/app/` 与 namespace `krkr::environ` 不一致 | ✅ 定为 `krkr::env`（理由见 §2） |
| 6 | `core/base/core/src/krkrMsgImpl.cpp` 是老树消息系统的搬运草稿，include 了 `tjsCommHead.h` 等**老树头**，在 `cpp_new` 里编不过 | 保留，**不要接进任何构建**，第 7 步处理 |
| 7 | `tests/` 里没有 `cpp_new` 的 CI | 第 4 步的日志器已就绪，可以加 smoke 脚本了；目前靠手工跑 16 条用例 |
| 8 | `krkr::environ` 在 Windows/MinGW 上编不过（`environ` 是 `stdlib.h` 的宏） | ✅ 已改 `krkr::env`；以后任何 namespace/类型名都不许用 C 运行时标识符 |
| 9 | 方法名 `GetLastError` 与 `windows.h` 的宏同名 | ✅ 已改 `GetLastErrorText` |
| 10 | **启动仍在 tick 线程上同步执行** | 见下方「触发条件」。现在只做一次 `exists()`，微秒级；第 7 步接 TJS 后若实测 `OpenGame` 耗时超过一帧预算（>50 ms @60fps），就必须把启动挪到 worker 线程 —— 判据是实测数据，不是接口形状 |
| 11 | 启动期日志在 `Drain` 时可能已被 tick 日志挤出 256 行环（`KRKR_LOG_LEVEL=debug` + 跑很多帧后才 drain） | 真宿主是启动结束立刻 drain，不会发生；如果将来要保底，把启动期日志单独存一份 |
| 12 | 日志文件没有 fsync，只 flush 到 libc 缓冲 | 进程被 kill 时可能丢最后几行。第 11 步与 `engine_set_log_file_path` 一起评估 |
| 13 | **`Storage::Open` 放行 `..`**（本步拍板的决定：完全跟随系统路径语义） | 后果：接上 TJS 后脚本可以用 `../` 读到游戏目录外面。若要收紧，在 `Storage::Open` 里加一句“解析后必须仍在 root 内”的检查即可（+3 行），调用方不用改。兼容性需届时用真游戏测 |
| 14 | `Storage::Open` 传入**绝对路径**时，`root / name` 会整体替换 root | 与 #13 同一个决定的另一面。测试台没覆盖；若要拒绝，同样在 `NormalizeName`/`Open` 里加一条检查 |
| 15 | `Storage` 不加锁 | 现在只有 tick 线程用它。真出现多线程访问再加（届时先量一把锁的代价，别直接上 `shared_mutex`） |
| 16 | 每次 `Open` 都重新 `stat` + `file_size`，没有缓存 | 第 6c 刀做 XP3 段缓存时一并考虑目录后端的缓存；现在每帧只开一次文件，不对性能负责 |
| 17 | 名字大小写敏感 | `A.tjs` ≠ `a.tjs`。与 XP3 一致；若真机出现“目录能跑、包不能跑”的游戏，先查名字大小写 |
| 18 | XP3 **索引只支持 raw**（`flag & 0x07 == 0`）；压缩索引直接报错 | 本测试的 `data.xp3` / `patch@r484.xp3` 都是 raw。若真遇到 zlib 索引的包，6c 时顺便把这条补上（那时已经有 inflate） |
| 19 | XP3 **数据段全部是 zlib**（`data.xp3` 里 20953/21123 条） | 这是 6c 的核心工作；没有 inflate 就只能读 170 个未压缩条目 |
| 20 | 段表只校验了 `start <= indexOffset` 与累加不溢出，**没校验 `start + arcSize <= indexOffset`** | 6c 真读数据时补（读之前必然要知道边界，那里一并做） |
| 21 | Windows 上 `main` 的 argv 是 **ANSI 代码页**（中文系统 = GBK），带日文的路径传不进来 | 测试台靠 `--path-file <file>` 从文件读 UTF-8 绕开。**Android 走 JNI、引擎侧走 `const char*` UTF-8，不受影响**；但它反向证明了 6b 修的那个非法 UTF-8 abort 是真问题 |
| 22 | 归档重名只保留第一条（`Find` 返回先到先得） | 真实包里 `duplicates = 0`，暂不处理；若某个游戏靠后出现的重名覆盖前面的，先查这里 |
| 23 | `IStream` 没有错误文本：解压失败时上层只知“Read 返回 -1”，不知原因 | 6c 已把原因写在局部变量里但丢掉了。后续给 `IStream` 加 `GetLastErrorText()`（接口加法，不破调用方） |
| 24 | `Xp3Stream` 按段**整段解压到内存**；`profile_main.pimg` 这类 48 MB 的段会直接占 48 MB | 真实游戏暂时能跑（包内最大条目 ~48MB）。等渲染/音频开始拉大文件时再做“分段流式 + LRU 缓存”，与老树的段缓存对齐 |
| 25 | **多段条目没有被真实数据覆盖**：`data.xp3` / `patch@r484.xp3` 都是 `multi-segment = 0` | 代码已按多段写（`offsetInFile` 累加、跨段 Read），但没被真包验证；需要 OggVorbis 共享码本的那类包才能验 |
| 26 | 每个 `Xp3Stream` 自己开一个 `ifstream`（一个 fd） | 21k 条目同时打开会耗尽 fd。等真的出现“同时开很多文件”时改成共享一个文件句柄 + 锁（或 pread） |
| 27 | **目录模式不会自动挂载盘上的 `*.xp3`** | 这就是 6d。真实游戏目录里只有 `data.xp3`，所以现在把**目录**传给引擎会 `-4 no such file: .../startup.tjs`；传 **`data.xp3` 本身** 则正常（已实测读出了 `startup.tjs` 13022 字节） |
| 28 | `puff` 的速度约为 zlib 的 1/2–1/4，且是**整段一次性**解压 | 6c 只要“解对”。真要提速度只改 `Inflate.cpp`（换成 zlib 的 inflate 或 miniz），调用方不动 |
| 29 | auto-path **只扫根目录一层**，不递归 | 老层同行为。若遇到包在子目录里的游戏（如 `game/bgm/*.xp3`），再评估是否递归 |
| 30 | 包内目录条目（`xxx.xp3>dir/` 形式的 auto-path 前缀）**未实现** | 这个语义属于 TJS `Storages` 的 `>` 分隔符，第 7 步以后才需要；目前 `ArchiveRootOrder` / `PatchPriorityEnd` 两个策略开关也只是文档级别 |
| 31 | 目录挂载点每次 `Open` 都做一次 `exists()`，未缓存命中结果 | 每次开文件多一次 stat；现在每帧只开一两个文件，不管。等有目录枚举/大量小文件需求时再加缓存 |
| 32 | `storage_probe` 的编译命令**必须带 `Logger.cpp`** | `Storage.cpp` 从 6d 起会打日志，漏了就是一片 `undefined reference to krkr::log::Logger::Instance()` |
| 33 | TJS2 未引入 oniguruma，`RegExp` 不可用 | 访问 `RegExp` 时得到"未定义"错误而非崩溃。真实脚本对正则依赖较低；需要时引入 oniguruma 并移除 `TJS_NO_REGEXP` |
| 34 | `Storages.addAutoPath` 等接受但不生效 | 挂载集合在打开游戏时确定；脚本运行期动态挂载尚未支持 |
| 35 | `TjsEngine` 的脚本执行发生在调用线程（当前为测试台主线程） | 第 8 步之后若接入 tick 线程需重新评估取消与超时 |
| 36 | tjs2 以 `-fpermissive -w` 编译 | 上游为 MSVC 编写，GCC 下存在大量无害告警；收敛告警属独立任务 |
| 37 | 大小写不敏感回退只作用于 XP3，目录挂载仍严格区分大小写 | 目录模式命中失败时未做枚举比对；若游戏目录混用大小写会暴露 |
| 38 | 尚未建立 CMake 目标，仅以脚本检查层间依赖 | ✅ 已修：`cpp_new/CMakeLists.txt` + 预设，目标与分层一一对应；手写构建脚本已删除 |
| 39 | `platform/` 仅有空目录 | 首个落地项为平台信息与剪贴板；渲染表面接线属第 8 步 |
| 40 | 脚本错误消息可能为空串 | ✅ 已修：消息表由 `tools/gen_tjs_message_map.py` 从上游参照实现生成到 shim，异常文本带上下文/块名/行号/trace（见 §4.16） |
| 41 | `Debug.addLoggingHandler` / `removeLoggingHandler` 未实现 | 需要 `core/log` 到脚本层的回调契约（且要防重入：处理函数里再写日志）。当前显式返回 `TJS_E_NOTIMPL` 并记录 |
| 42 | `Debug.startLogToFile(clear)` 不支持清空；`logToFileOnError` / `clearLogFileOnError` 只保存取值 | 日志文件由宿主设定（ABI `engine_set_log_file_path`）；错误触发的重定向/清空需要与第 11 步的宿主管辖一起设计 |
| 43 | `Plugins` 只支持静态注册表 | 动态模块加载（`.dll`/`.so`）、`linkZ`（`bres://` 资源）、`bootStrap`、`CanLoadPlugin` 均未实现；未命中名字显式失败并带名字 |
| 44 | `System` 只覆盖已出现过的成员 | ✅ 已补 `Initialize.tjs` 需要的路径/平台/参数成员；`screenWidth`、`createAppLock`、`system(...)`、DLL 目录相关成员尚未实现（未命中时显式报错） |
| 45 | 字节码只验证了真游戏的 `system/Initialize.tjs` | `LoadByteCode` 的返回值未用于区分「字节码」与「二进制字典数组」；目前只在 `IsTJS2ByteCode` 命中时调用，不会把文本误当字节码 |
| 46 | `Window` / `Layer` 只覆盖 `Initialize.tjs` 需要的形状 | ✅ 7g-1 已落地几何/可见性/opacity/type/clip/loadImages/层级移动（§4.27）；`Bitmap`、事件派发、`children`、字体绘制仍未做（#64） |
| 47 | `System.personalPath` / `savedGamesPath` 需要宿主注入 | `EngineRuntime` 提供了 `SetPersonalDirectory` / `SetSavedGamesDirectory`，Android 侧由第 11 步的 JNI 填；未注入时脚本读取显式失败 |
| 48 | `CompatProfile.preludeScript` 为空 | 参照实现会注入 `CompoundStorageMedia` 一类脚本级 stub；按"有证据才加"的原则留空，加之前先确认触发它的游戏与行为 |
| 49 | CTest 的 `real_startup` 处于 DISABLED | `Window` 已注册，**下一个缺口未测量**（本机没有游戏数据）。在有 `build/real_root.txt` 的机器上先跑 `tjs_probe build/py_startup.utf8.tjs` 得到新缺口，再决定启用时机 |
| 57 | GL 后端只覆盖 5 种算子 | `ltPs*` 家族 16 种按规格需软件兜底（离屏合成后再上传）；在此之前这些图层会被跳过并计数 |
| 58 | 纹理缓存以 `Texture*` 为键，无 LRU、无句柄复用 | 8b 只验证正确性；等接入真实素材（8d/图片解码）时改成"纹理对象 + 引用计数 + LRU" |
| 59 | Android 的 EGL 路径只做过编译验证 | 缺 JNI 接线（`surfaceCreated/Changed/Destroyed` → `krkr_android_surface_*`）与真机一帧；`IGLContext` 已在桌面被完整验证 |
| 60 | `AndroidTarget`（像素路径）在 Android 上仍是显式失败 | Android 主路径走 GL；软件兜底（`ltPs*`）需要"离屏 Surface → 纹理"的桥，等 8d |
| 61 | EGL surface 生命周期没有跨线程保护 | `AndroidSurface` 用互斥锁保护 attach/detach，但渲染线程与 UI 线程的时序需在 JNI 层按 Android 约定串行化 |
| 54 | 8a 只支持轴对齐 + 最近邻；旋转/斜切项被跳过 | 双线性与仿射属 8b/8d；跳过会记入 `RenderStats::itemsSkipped`，不会画错 |
| 55 | Android 呈现目标未接 ERenderTarget 到 ANativeWindow/EGL | 8c；当前 `Present` 返回 false 并记录一次原因 |
| 62 | TLG5/TLG6 未实现 | KiriKiri 自有格式，命中时 `DecodeImage` 显式返回 "not implemented"；参照 `LoadTLG.cpp` 另做 |
| 63 | 解码缓存与尺寸上限未做 | 每次解码都整文件读入内存；真机立绘需要"名字+尺寸"为键的 LRU，等接 `Window/Layer` 时一并做 |
| 56 | 没有图像解码与字体 | 8a 用程序生成的纹理验证管线；PNG 需引入 stb_image（vendored），TLG 参照 `LoadTLG.cpp` |
| 51 | 调试器只有观察平面（事件流），没有断点/单步 | `ENABLE_DEBUGGER` 钩子已验证可编译（规格 §8），但暂停语义需要与 tick 模型一起设计；当前定位问题靠事件 + trace |
| 52 | `krkr_debug` 只支持 `--summary` / `--filter`，无 `--follow` | 事件文件在进程退出后才有完整内容；实时跟随等 D4 的会话层 |
| 53 | 事件未带线程标识 | 目前单线程（脚本在调用线程执行）；第 8/9 步引入 worker 后补 `thread` 字段 |
| 50 | CTest 的断言以仓库根为工作目录 | 断言脚本里的相对路径（`build/debug_probe.log`）依赖这一点，改工作目录会破坏 `t_debug` |
| 64 | `Window` / `Layer` 只有部分成员 | 已落地：几何、`visible`、`opacity`、`type`、`clip`、`loadImages`、层级移动、`primaryLayer`；未做：事件派发（`onResize`/`onClick` 能被脚本赋值，但引擎不会调用）、`children`、`face`、`hitType`、`drawText`、滤镜/转场、`Bitmap` |
| 65 | 窗口与图层互相持有 TJS 引用（`layer.window` + `window.add`），会话内不回收 | 参照实现的窗口同样活到进程结束；若要支持关闭窗口并释放，需把钉住改成弱引用并在失效时显式拆子树 |
| 66 | 只呈现"主窗口"（第一个 `new Window()`） | `GetPrimaryScene()` 取登记表首项；`showModal`、子窗口、多窗口布局未实现 |
| 67 | 场景只覆盖 5 种算子、轴对齐、无脏矩形差分 | 与 8a/8b 的边界一致；`SceneBuildStats` 已区分 hidden/unsupported/clipped，8d/8e 在此之上补 |
| 68 | `Layer.loadImages` 每次都整文件解码，无缓存 | 与 #63 同一件事；真素材上量时按"存储名 + 尺寸"做 LRU |
| 69 | 断言脚本必须 `throw new Exception("...")` | 纯字符串抛出到宿主只剩 `script exception`，`-ok` 会丢；原因见 §4.27 第 2 点 |
| 70 | 老树 `cpp/**` 已决定废弃，但仍在仓库里与 `cpp_new` 并存 | 冻结：不新增功能、不修非阻塞 bug；R1-5 删除整树（`docs/legacy-gap.md` §6）。期间唯一用途是"行为参考" |
| 71 | 第 9/10 步（输入/音频）尚无契约 | 按 `docs/legacy-gap.md` §3 与 `ARCHITECTURE` §6：先加 `core/contracts` 小接口，再写 Android 后端；不抄老树 `sound`/`WindowImpl` |
| 72 | 消息表白手抄风险（83 条 vendor 需要的消息常量） | 用 `tools/gen_legacy_msg.py` 从上游 `msg/MsgIntfInc.h` 生成 `.inc`（与 `gen_tjs_message_map.py` 同一模式），禁止手抄 |



### 技术债 #10 的触发条件（写下来，免得以后变成"当年为什么没人想过"）

```
如果 OpenGame() 在 tick 线程上耗时 > 50ms（真机实测，不是估计）
→ 把 RunStartupWork() 挪到 worker 线程：
   OpenGame 里：SetState(RUNNING); worker = std::thread([this]{ RunStartupWork(); });
   Tick 里的 StartupPending 路径立刻变成真实路径（它已经写好了）
   Destroy 里：cancel → join → delete（顺序不能反）
```

---

## 6. 铁律（踩了要回滚）

1. **不 include 老树任何头**（`cpp/core/**`）。一 include `tjsCommHead.h` 就被 337 个 extern 全局绑架。
2. **不新增全局符号**。一切进 `namespace krkr::<模块>`；辅助函数进匿名 namespace。
3. **不使用 `using namespace std;`**。一律 `std::`。
4. **入口层不抛异常**。跨 JNI / `extern "C"` 抛异常是未定义行为。全部用返回码 + `GetLastError()`。
5. **返回码粗、消息细**。码跟 `engine_api.h` 对齐（`0/-1/-2/-3/-4/-5/-6/-7`），人话写进 `lastError_`。
6. **内部 `std::string`，边界 `const char*`**。UTF-8 进、UTF-8 出，中途不转宽字符。
7. **`new` 必须配 `Destroy`**。没有 GC。
8. **公开函数第一行检查参数**（`nullptr` / 空串），JNI 那边真的会传空指针进来。

---

## 7. 与老树的关系

替换时**只换实现，不换签名**。契约是这三个文件，改动会被 Kotlin 侧立刻感知：

- `bridge/engine_api/include/engine_api.h`（C ABI，是需求书，**不许改**）
- `bridge/engine_api/src/engine_api_android_jni.cpp`（JNI 薄壳，保持不动）
- `app/app/src/main/kotlin/org/dpdns/clevebitr/core/NativeEngine.kt`（宿主，保持不动）

老树里值得抄的参考点：

| 参考 | 位置 | 抄什么 |
|---|---|---|
| 宿主路径规范化 | `bridge/engine_api/src/engine_api_lifecycle.cpp` `OpenGameCore` | 尾斜杠约定、包类型判断（**但大小写处理有 bug**） |
| 启动状态机 | `engine_api.h` 的 `engine_startup_state_t` / `ENGINE_RESULT_STARTUP_PENDING` | 第 5 步已对齐（0/1/2/3 与 -7） |
| 生命周期 | `cpp/core/environ/app/Application.cpp` | 启动顺序（**不要抄 1062 行的结构**） |

### 7.1 从 ABI 里扒出来、会影响设计的硬事实

| 事实 | 出处 | 对 `cpp_new` 的要求 |
|---|---|---|
| 日志轮转是 **4 MiB × 保留 3 个** | `engine_set_log_file_path` 注释 | `Logger` 的常量，不是实现自由 |
| 启动日志要能按行吐给宿主 | `engine_drain_startup_logs` 注释 | 一行一条、UTF-8、`\n` 结尾；已做（内存环 + `Drain`） |
| 宿主用的是 **async** 入口 | `engine_open_game_async`“Returns immediately” + `EngineSession.kt:525` 实际调用 | `OpenGame` 必须尽快返回；同步实现下靠“启动工作量小”成立，触发条件见 §5 #10 |
| 宿主用**一条 HandlerThread** 同时跑 `openGame` 和 tick | `EngineSession.kt:19 / 442 / 984` | 在调用线程上阻塞 = 阻塞 tick。这是“不能阻塞”的真实原因，不是接口洁癖 |
| 宿主每帧轮询启动状态 | `engine_get_startup_state` + `pollStartupState()` | 引擎只需一个可查询的字段，**不需要回调/事件管道** |
| 启动期 tick 返回 `-7` 会被宿主忽略而不算失败 | `EngineSession.kt:369` | `StartupPending` 必须是 `-7`，不能用 `InvalidState` |

## 12. R1 路线：渲染底层改由 krkrz 权威实现（2025-11 决定）

完整计划见 **`cpp_new/docs/visual-vendor.md`**。要点：

- 权威源 = `krkrz/krkrz` `1.4.0r2`（本地 `~/AppProjects/krkrz`），`cpp_new/tjs/` 与它同 tag（仅 CRLF 差异），
  因此 `visual/` 可以直接编在这份 tjs 上。
- 取：`visual/tvpgl.c`（6,658 行纯 C，自带 `TVPInitTVPGL()`）、`drawable.h`（29 种算子）、
  `LayerBitmapIntf` / `LayerIntf` / `LayerManager` / `TransIntf` / `ImageFunction`、字体（FreeType）、
  `LoadPNG/LoadJPEG/LoadTLG`、`msg/base/utils` 的接口头。
- 不取：`visual/win32/`、`visual/IA32/`、`visual/gl/`、`visual/glgen/`、`tjs2/`（已对齐）；
- 也**不从本仓库 `cpp/core/visual` 取任何文件**（Kirikiroid2 后裔已漂移：`tvpgl.cpp` 14,139 行、
  `LayerBitmapIntf.cpp` 与上游 diff 9,025 行、多出 5,278 行自研 GL 渲染）。
- 我们只写 seam 与 porting：`iTVPDrawDevice`（无 HWND 子集）、`iTVPWindow`/`iTVPLayerTreeOwner`、
  `iTVPStorageMedia`（接 `krkr::io::Storage`）、`iTVPSysFont`（FreeType）、`BitmapBitsAlloc`（~80 行）、
  tick/event/msg 适配。

分片（每片可编、可贴输出、可回滚）：

| 片 | 内容 | 验收 |
|---|---|---|
| R1-0 | 清单与 sha256（`vendor/krkrz/VENDOR.md`）、`tools/verify_vendor.py` | ✅ 完成：9 个上游文件 + 2 个适配文件，校验通过 |
| R1-1 | `tvpgl.c` + `blend_function.cpp` + `pixel/` 抽象层；`Blend.cpp` 改成映射 | ✅ 完成：`blend_matrix` 300 项断言，22/22 CTest |
| R1-2 | 位图 + 字体 + 解码（`BitmapBitsAlloc`/`SysFont`/`StorageMedia` 重写） | `loadImages` → `saveLayerImage` 与 Windows 逐字节相同 |
| R1-3 | 图层模型 + 转场；删掉 `visual/Scene` 与自建图层树 | 29 type 全矩阵 + 淡入淡出与 Windows 逐像素一致 |
| R1-4 | Android `VisualDrawDevice`（BGRA 上传 + 脏矩形 + 一 quad + Y 翻转） | 真机一帧；与软件落盘图 ±1 |
| R1-5 | 收口：删 `ImageDecoder/stb`、`GLBackend`；`check_layer_deps` 加 vendor 规则 | `ctest` 全绿，文档同步 |

铁律调整（已写入 `docs/ARCHITECTURE.md` §3）：`vendor/` 是唯一允许包含老树头的地方；
其余层仍然禁止。**不允许保留第二条合成路径。**

### 12.0 R1 进度

- **R1-0 / R1-1 已完成**（详见 `docs/visual-vendor.md` §7.1）：`vendor/krkrz` 落地 `tvpgl.c` +
  `blend_function.cpp`（29 种算子）与两个适配头；新增抽象层 `pixel/`（`LayerOp` 词汇表 +
  `BlendPixel`）；`host/BlendMatrixProbe.cpp` 做 29 × 5 alpha × 5 opacity 的语义断言（300 项）。
- 过程中发现并修复了**上游便携路径的真 bug**：`screen_blend_func` 少了 `~`（Windows 走 SSE2
  掩盖了它），已登记为 `vendor/krkrz/VENDOR.md` 补丁 #2。
- 定点舍入带来的 ±1～±2 属于上游行为：`render_surface` / `window_scene` 的期望值已按
  `pixel` 的实际输出更新并注明原因（不再用手算的 ÷255 值）。
- R1-2 已开工：**消息 seam 完成**（`tools/gen_legacy_messages.py` 从上游 `Messages.xlsx` 生成
  344 条中/日/英文案 → `vendor/krkrz/msg/generated/messages.inc`，`legacy_message` 断言通过）；
  `toolchain_compat.h` 补齐 `BITMAPINFO`/`HRGN`/`MAX_PATH`/`OutputDebugString`；
  补丁 #3（`class TJS::...` 前置声明）。**编译驱动盘点**：`ComplexRect`/`FontSystem`/
  `PrerenderedFont` 0 错误，`LayerBitmapIntf`/`LayerBitmapImpl`/`GraphicsLoaderIntf`/
  `LoadTLG`/`SaveTLG5` 各差 1 个 porting 头，`LoadPNG` 差 libpng、`FreeType` 差 freetype。
  剩余估计见 `docs/visual-vendor.md` §7.1（R1-2a/b + R1-3/4/5 合计 19–30 人日；
  “单帧出图”7–11 人日）。

### 12.1 老树（`cpp/core/**`）零复用

完整盘点见 **`docs/legacy-gap.md`**。三条结论：

1. **从 `cpp/` 复用代码 = 0 行**。老树只作行为参考、兼容补丁需求清单、ABI 需求书。
   老树规模：`visual` 104k 行、`utils` 71k、`movie` 22k、`sound` 17.6k、`base` 17.3k、`environ` 9.7k、`io` 9.2k。
2. **seam 预算实测**（对 19 个 vendor 候选文件做符号扫描）：引用 634 个 `TVP*` 符号 →
   108 个来自 `base/utils/msg` → 其中 **20 个函数 + 83 条消息常量**；消息表由
   `msg/MsgIntfInc.h`（X-macro，152 条）生成，不手抄。另加 4 个接口类
   （`iTVPStorageMedia` / `iTVPSysFont` / `iTVPDrawDevice` / `iTVPLayerTreeOwner`+`iTVPWindow`）。
3. **缺的功能按 `ARCHITECTURE` §6 新写**：先 `core/contracts` 契约 → 再 `platform` 实现 →
   再 `shim` 桥接 → `host` 断言 → 文档同步。明确不做的：视频（`movie` 22k 行）、
   动态插件/Susie/ncbind、PhaseVocoder 循环平滑。

删除节奏：现在起冻结 `cpp/**`（不新增功能）；R1-5 删除整棵老树与草稿
`cpp/core/extension/krkrMsgImpl.cpp`（技术债 #6），保留 `bridge/engine_api`、`app/`。

## 11. 交接：新对话起点

### 当前可运行状态

```bash
bash cpp_new/tools/run_probes.sh                        # CMake + ctest，期望 21/21 通过
python cpp_new/tools/check_layer_deps.py cpp_new        # 期望 0 违规

# 真游戏（两条链路共用同一装配）
build/cmake/debug/bin/krkr_next.exe --path-file build/real_root.txt
build/cmake/debug/bin/tjs_probe.exe build/py_startup.utf8.tjs build/real_root.txt

# 渲染 / 解码 / 调试事件
build/cmake/debug/bin/image_probe  build/real_root.txt title_bg.png build/title_bg.ppm
build/cmake/debug/bin/image_window build/real_root.txt title_bg.png 120
KRKR_DEBUG_EVENTS=build/events.jsonl build/cmake/debug/bin/krkr_next.exe --path-file build/real_root.txt

# 场景合成（不需要游戏包，用夹具）
build/cmake/debug/bin/window_probe host/fixtures/demo/t_window.tjs \
    build/fixtures_root.txt build/window_scene.ppm
```

| CTest | 覆盖 |
|---|---|
| `storage_*`、`xp3_*` | 目录/包挂载、XP3 索引与解压字节一致 |
| `script_*`（9 项） | Storages / Scripts / System / Debug / Plugins / 别名调用 / 退出 |
| `startup_state` | 启动状态机 |
| `render_surface`、`render_gl` | 软件合成断言；GL 与软件逐像素 ±1（无 GL 环境 skip） |
| `window_scene` | 脚本建窗口/图层 → 合成 → 手算像素断言（7g-1） |
| `image_decode` | 真包 `title_bg.png` 解码 + 合成（无游戏包时 skip） |
| `debug_*`（5 项） | 调试事件录制与筛选 |
| `real_startup` | **DISABLED**（见下） |

### 已知失败点

`Window` / `Layer` 已注册（§4.27），所以下面这条旧缺口已过期：

```
[error] Scripts.execStorage("system/Initialize.tjs"): Member "Window" does not exist   ← 已越过
```

**新缺口尚未测量**：当前开发机没有游戏数据（`build/real_root.txt` 不存在），
`real_startup` 仍 DISABLED（#49）。下次在有游戏数据的环境里先跑

```bash
build/cmake/debug/bin/tjs_probe build/py_startup.utf8.tjs build/real_root.txt
```

取到新的 `Member "..." does not exist` 或语法/求值错误，再决定补哪个成员——
不要凭猜测补 `Initialize.tjs` 的成员（#48 的理由同样适用）。

### 下一步（按顺序，行 7g / 8d）

1. **R1-1（下一步）**：把 `krkrz/visual/tvpgl.c` 取进来，`Blend.cpp` 改成薄适配，
   建 `blend_matrix` 算子矩阵（29 type × opacity × alpha）。同期做 R1-0 的清单与 sha256。
   理由见 §12 与 `docs/visual-vendor.md`：这一片不依赖 vendor 全量，一周内就能消掉画面差异的主因。
2. **`Window` / `Layer` 桥接（7g-1，已完成，见 §4.27）**：R1-3 时其合成职责退场，
   `Window` 改为 `iTVPWindow` / `iTVPLayerTreeOwner` 的实现。
3. **量下一个缺口**：在有游戏数据的开发机上跑 `real_startup`，按实际错误补成员（#64）。
4. **字体**（R1-2 一并解决）：`Font` 需要字形位图（FreeType 或系统字体），是 KAG 文本的硬依赖；
   先定"字形位图从哪来"，再决定 `Layer.drawText` 的最小面。
5. **8c 真机**（R1-4 一并做）：`bridge/engine_api` 接 `krkr_android_surface_attach/resize/detach`，
   按 Android 约定串行化 UI 线程与渲染线程；`render-spec.md` §14.2 记录了两条约束。
6. **解码补齐**（R1-2 一并解决）：TLG5/TLG6 由 legacy `LoadTLG.cpp` 提供；LRU 缓存仍按 #63、#68 单列。
7. **Storages 挂载面**：`addAutoPath` / `addArchive` / `commitSavedata` 等（当前接受并记录，
   不改变挂载表）；顺序放在 `Window` 之后，因为它们要等脚本能跑到对应分支。

### 纪律提醒

- 修改前先读 `cpp_new/docs/ARCHITECTURE.md`、`docs/render-spec.md`、`docs/debugger-spec.md`；
  先补下层契约，再写桥接。
- 不修改 `tjs/`；新增补丁登记到 `tjs/VENDOR.md`（已登记 3 条）。
- 注释遵循 `AGENTS.md` §11。
- 每步结束跑 `tools/run_probes.sh`、`check_layer_deps.py`，以及 `cmake --preset android-debug`（只编库）。
