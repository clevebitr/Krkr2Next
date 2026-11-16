# cpp_new — 引擎入口重写（交接 / TODO）

> 目标：在 `cpp_new/` 里从零写一个干净的引擎核心，不 include 老树（`cpp/core/**`）的任何头，
> 长到能跑之后一次性替换 `bridge/engine_api/` 里的实现。
> 老树是参考物和需求书，**不是**依赖。

---

## 0. 一句话现状

入口容器 + 路径校验 + 日志 + 启动状态都立起来了：**能创建、能校验并规范化游戏路径、能落盘日志、能回答「起来了吗」、能跑帧、能销毁**。
还没有线程、存储、脚本、渲染 —— 第 6 步（存储层）起才碰真内容。

---

## 1. 现在能跑什么

```bash
cd /storage/emulated/0/Download/AppProjects/Krkr2Next

clang++ -std=c++20 -Wall -Wextra -Wpedantic -I cpp_new \
  cpp_new/host/main.cpp \
  cpp_new/core/env/app/EngineRuntime.cpp \
  cpp_new/core/log/Logger.cpp \
  -o $TMPDIR/krkr_next

$TMPDIR/krkr_next <game_dir> [log_file]
# 环境变量：KRKR_LOG_LEVEL=debug|info|warn|error|off
#           KRKR_LOG_MAX_BYTES=<数字>   （仅测轮转用，生产固定 4 MiB）
```

只用 Termux 里的 `clang++`，**不需要 NDK / cmake / Gradle / 手机**。反馈时间 < 10 秒。

已验证用例（2024-11 本轮）：

| # | 输入 | 结果 | 判定 |
|---|---|---|---|
| 1 | `games/demo`（目录 + `startup.tjs`） | `root=".../games/demo/" kind=1 startup=2` rc=0 | ✅ |
| 2 | `games/demo/` | 与 1 **逐字相同** | ✅ |
| 3 | `games/demo/../demo` | 与 1 **逐字相同**（`..` 被吃掉） | ✅ |
| 4 | `games/nope`（不存在） | `-4 does not exist`，`startup=0` | ✅ |
| 5 | `GAME.XP3` | `kind=2`、`startup=2` + 一条 warn（包内文件等第 6 步） | ✅ |
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
    └── log/
        ├── LogLevel.h                级别枚举 + 名字/解析
        ├── Logger.h                  进程内唯一日志器（pimpl，只露接口）
        └── Logger.cpp                文件 sink / 轮转 / 内存环
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
| 6 | **存储层** | 目录读取 + XP3 索引解析（`OpenArchive` / `FindFile`） | ⬅ **下一步**（大） |
| 7 | 脚本虚拟机 | 接 TJS2，跑 `startup.tjs` | 待做（大） |
| 8 | 渲染 | EGL + GLES3 surface，一帧提交给宿主（零拷贝 SurfaceTexture） | 待做（大） |
| 9 | 输入 | 指针 / 按键 / 文本 → 脚本事件 | 待做 |
| 10 | 音频 | 至少 WAVE + OGG | 待做 |
| 11 | 换回去 | 用 `cpp_new` 替换 `bridge/engine_api/` 的实现，签名不变，Kotlin 不动 | 待做 |

**节奏约定**：每次只做一行，做完能编、能跑、能贴出输出，再进下一行。不许并行铺开。

---

## 4. 已完成的步骤记录（第 3–5 步）

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
