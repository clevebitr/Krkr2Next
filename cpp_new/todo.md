# cpp_new — 引擎入口重写（交接 / TODO）

> 目标：在 `cpp_new/` 里从零写一个干净的引擎核心，不 include 老树（`cpp/core/**`）的任何头，
> 长到能跑之后一次性替换 `bridge/engine_api/` 里的实现。
> 老树是参考物和需求书，**不是**依赖。

---

## 0. 一句话现状

入口容器 + 路径校验 + 日志 + 启动状态 + 目录存储 + XP3 索引 + **XP3 读取**都立起来了：
能从 4.7 GB 真包里把 `startup.tjs` 读出来（字节与 zlib 参照实现一致）。
还没有：目录模式自动挂载盘上的 `.xp3`（6d）、线程、脚本、渲染。

---

## 1. 现在能跑什么

```bash
cd /storage/emulated/0/Download/AppProjects/Krkr2Next

# 归档后端（6b/6c）的源文件。从 6c 起主程序也必须带它们，
# 否则启 .xp3 包时链接不上。puff.c 是 zlib 官方参考实现（zlib license）。
ARCH="cpp_new/core/io/archive/Xp3Index.cpp cpp_new/core/io/archive/Xp3Stream.cpp \
      cpp_new/core/io/archive/Inflate.cpp cpp_new/core/io/archive/third_party/puff.c"

clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/main.cpp \
  cpp_new/core/env/app/EngineRuntime.cpp \
  cpp_new/core/log/Logger.cpp \
  cpp_new/core/io/PathUtil.cpp cpp_new/core/io/IoName.cpp \
  cpp_new/core/io/DirectoryBackend.cpp cpp_new/core/io/Storage.cpp $ARCH \
  -o $TMPDIR/krkr_next

$TMPDIR/krkr_next <game_dir|game.xp3> [log_file]
$TMPDIR/krkr_next --path-file <file> [log_file]   # 日文/中文路径在 Windows 上走这个
# 环境变量：KRKR_LOG_LEVEL=debug|info|warn|error|off
#           KRKR_LOG_MAX_BYTES=<数字>   （仅测轮转用，生产固定 4 MiB）

# io 的独立测试台（目录模式 18 条检查 + 归档模式 --extract）
clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/StorageProbe.cpp \
  cpp_new/core/io/PathUtil.cpp cpp_new/core/io/IoName.cpp \
  cpp_new/core/io/DirectoryBackend.cpp cpp_new/core/io/Storage.cpp $ARCH \
  -o $TMPDIR/storage_probe
$TMPDIR/storage_probe <game_dir>
$TMPDIR/storage_probe --path-file <file> --extract <name> <out>

# XP3 索引测试台（6b）
clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/Xp3Probe.cpp \
  cpp_new/core/io/archive/Xp3Index.cpp cpp_new/core/io/IoName.cpp cpp_new/core/io/PathUtil.cpp \
  -o $TMPDIR/xp3_probe
$TMPDIR/xp3_probe <archive> [--list | --find <name>] [--path-file <file>]
```

只用 Termux 里的 `clang++`，**不需要 NDK / cmake / Gradle / 手机**。反馈时间 < 10 秒。

已验证用例（2024-11 本轮）：

| # | 输入 | 结果 | 判定 |
|---|---|---|---|
| 1 | `games/demo`（目录 + `startup.tjs`） | `root=".../games/demo/" kind=1 startup=2` rc=0 | ✅ |
| 2 | `games/demo/` | 与 1 **逐字相同** | ✅ |
| 3 | `games/demo/../demo` | 与 1 **逐字相同**（`..` 被吃掉） | ✅ |
| 4 | `games/nope`（不存在） | `-4 does not exist`，`startup=0` | ✅ |
| 5 | `GAME.XP3`（0 字节假包） | `-4 file is too short to be an XP3 archive`、`startup=3` | ✅ 6c 后的正确行为 |
| 6 | `readme.txt` | `-4 既不是目录也不是已知包` | ✅ |
| 7 | `""` | `-1 game root is empty` rc=1 | ✅ |
| 8 | 无参数 | usage rc=2 | ✅ |
| 9 | 180 帧后 | `frames=180` | ✅ |
| 10 | 目录缺 `startup.tjs` | `-4 startup script not found`、`startup=3` | ✅ |

第 1/2/3 条的输出**必须逐字相同**（绝对路径 + 尾斜杠），用 `sort -u | wc -l` 应得 1。

---

## 2. 文件约定

```
cpp_new/
├── host/main.cpp                     本地测试台 = 未来的 Kotlin 宿主（只用来启动，不进引擎）
└── core/
    ├── env/app/
    │   ├── EngineRuntime.h           入口契约（不透明句柄 + 启动状态机）
    │   └── EngineRuntime.cpp         实现
    ├── io/
    │   ├── IOStream.h                IStream 接口（Read/Seek/GetSize/GetPosition）
    │   ├── DirectoryBackend.h/.cpp   目录后端：一个路径 -> 一个只读流
    │   ├── Storage.h/.cpp            门面：根目录 + 名字 -> 流（名字归一化在这里）
    │   ├── IoName.h/.cpp             名字归一化（目录后端和归档后端共用）
    │   ├── PathUtil.h/.cpp           UTF-8 <-> filesystem::path（全引擎唯一实现）
    │   └── archive/
    │       ├── Xp3Index.h/.cpp       XP3 索引解析（只读目录表）
    │       ├── Xp3Stream.h/.cpp      从包里按条目读字节（段缓存 + 解压）
    │       ├── Inflate.h/.cpp        zlib/deflate 解压封装（以后换实现只改这里）
    │       └── third_party/
    │           └── puff.c/.h         zlib 官方参考实现（840 行，zlib license）
    └── log/
        ├── LogLevel.h                级别枚举 + 名字/解析
        ├── Logger.h                  进程内唯一日志器（pimpl，只露接口）
        └── Logger.cpp                文件 sink / 轮转 / 内存环

host/StorageProbe.cpp                 io 的独立测试台（不进引擎，单独编一个可执行）
host/Xp3Probe.cpp                     XP3 索引测试台（6b）
```

- `host/main.cpp` **不属于引擎**，它扮演的是 `NativeEngine.kt` 的角色。引擎永远是库，不是可执行文件。
- 引擎内部的所有辅助函数写进 **匿名 namespace**（文件私有），不要新增全局符号。
- ✅ **已定：namespace 叫 `krkr::env`，目录保持 `env`**（符合 N1：namespace = 目录名）。
  不能叫 `krkr::environ`：`environ` 是 C 运行时的全局变量名，Windows 的 MinGW 还把它写成宏
  （`stdlib.h:709: #define environ _environ`），宏会把 `krkr::environ` 撕成 `krkr::_environ()`，
  Windows 上**编译直接失败**（`namespace name required before '(' token`）。
- ✅ `GetLastError()` 已改名 `GetLastErrorText()`：`windows.h` 用
  `#define GetLastError GetLastError` 占住了这个名字。C ABI 的 `engine_get_last_error()` 不动。

---

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
| 6d | **存储层·auto-path** | 目录模式自动挂载根目录里的 `*.xp3`（含 `patch*.xp3` 优先级） | ⬅ **下一步** |
| 7 | 脚本虚拟机 | 接 TJS2，跑 `startup.tjs` | 待做（大） |
| 8 | 渲染 | EGL + GLES3 surface，一帧提交给宿主（零拷贝 SurfaceTexture） | 待做（大） |
| 9 | 输入 | 指针 / 按键 / 文本 → 脚本事件 | 待做 |
| 10 | 音频 | 至少 WAVE + OGG | 待做 |
| 11 | 换回去 | 用 `cpp_new` 替换 `bridge/engine_api/` 的实现，签名不变，Kotlin 不动 | 待做 |

**节奏约定**：每次只做一行，做完能编、能跑、能贴出输出，再进下一行。不许并行铺开。

---

## 4. 已完成的步骤记录（第 3–6c 步）

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

---

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
