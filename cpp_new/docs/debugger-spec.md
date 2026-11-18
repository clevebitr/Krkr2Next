# cpp_new 调试器（子包）规格

本文件规划 `cpp_new/debugger/` 子包：一个可独立构建、默认不进入引擎产物的调试器。
约束与 `ARCHITECTURE.md` 同源；新增约束以本文件为准。

## 1. 目标

在不改变现有运行时语义的前提下，回答三类问题：

| 问题 | 例子 | 需要的能力 |
|---|---|---|
| 为什么失败 | 脚本第 1 行 `Member "Window" does not exist` | 异常上下文、脚本调用栈、最近日志 |
| 卡在哪/走了哪条路 | 为什么这个游戏加载了 patch 包却没生效 | 存储事件、挂载表、脚本装载事件 |
| 状态是什么 | 断点处的局部变量、对象字段、表达式值 | 断点/单步、作用域读取、求值 |

明确不做：图形界面、内存编辑器、性能剖析（可后续作为独立消费者接入同一事件流）。

## 2. 可行性结论

**可以做到"零逻辑改动"，但做不到"零改动"。** 精确边界：

| 对象 | 改动 | 量级 |
|---|---|---|
| `tjs/` | **0 行源码改动**（已验证，见 §8） | 0 |
| `core/contracts/DebugServices.h` | 新增文件 | ~120 行 |
| `core/api/EngineRuntime.*` | 新增 `AttachDebugSink` + 5 处事件发送（空指针检查） | ~25 行 |
| `core/io/Storage.cpp` | `Open` 出口发送 1 条事件；新增 `DescribeMounts()` | ~15 行 |
| `core/log/Logger.cpp` | `Write` 后转发 1 行（可选，见 §7.5） | ~5 行 |
| `shim/engine/ScriptLoader.cpp` | 执行前后各 1 条事件 + 失败 1 条 | ~12 行 |
| `shim/tjs_host/TjsDebugHost.cpp` | **新增**：栈追踪、`TJSDebuggerHook`、`IsDebuggerPresent` | ~250 行 |
| `shim/tjs_host/debugger.h` | **新增**：上游 `#include "debugger.h"` 的落点（常量与声明） | ~40 行 |
| `shim/TjsBridge.h` | `ScriptContext` 增加 1 个 `IDebugController *` 字段 | 1 行 |
| `engine/ScriptRuntime.cpp` | 装配时把 controller 写入 context | ~3 行 |

合计：现有文件约 **60 行**新增，且全部是"指针为空则跳过"，不改变任何既有分支。
其余全部落在 `debugger/` 与新头文件里。若连这 60 行也不接受，唯一替代是"日志尾部 +
事后分析"（§10），拿不到断点与求值。

## 3. 分层与包结构

```
core ← platform        core ← shim ← engine ← host
                                  ↑
                              debugger（最上层，被宿主注入）
```

- `debugger/` 与 `host/` 同级：允许引用所有层；**任何层都不得引用 `debugger/`**。
- 引擎产物（`krkr_engine`）不链接 `krkr_debugger`；调试器由宿主在需要时注入。
- 依赖方向用 CMake 表达：`krkr_debugger` 链接 `krkr_engine` + `krkr_debug_protocol`。

```
cpp_new/debugger/
├── protocol/            纯数据层：消息定义与 JSON 编解码（不依赖 engine/core）
│   ├── Messages.h       事件/命令的结构体与枚举
│   ├── Messages.cpp     编解码（自写最小 JSON，不引入第三方库）
│   └── Json.h/.cpp
├── agent/               实现段：IDebugSink / IDebugController
│   ├── DebugAgent.*     事件队列、会话状态机、暂停/继续
│   ├── BreakpointTable.* 断点表（文件+行、条件、命中策略）
│   ├── Control.*        控制命令处理（step/continue/eval/frame）
│   └── Record.*         .jsonl 录制与回放
├── transport/           传输：stdio / TCP（Android 走 adb forward）
│   ├── Session.*        一行一条消息的会话循环
│   └── TcpTransport.*
├── dap/                 （可选）Debug Adapter Protocol 适配
└── cli/                 krkr_debug：REPL + 事件打印（无 UI 依赖）
```

测试台：`host/DebugProbe.cpp`（无网卡直连，脚本化会话）与
`host/fixtures/debug/session.tjs`（被调试脚本），注册进 CTest。

## 4. 契约：`core/contracts/DebugServices.h`

core 只定义接口，不知道实现，也不引入 JSON/网络依赖。

```cpp
namespace krkr::debug {

  enum class EventKind {
    EngineState,     // IDLE/RUNNING/SUCCEEDED/FAILED
    EngineFailure,   // 结构化失败报告
    ScriptBegin, ScriptEnd, ScriptFailure,
    ScriptBreak,     // 命中断点/单步/异常暂停（带帧）
    StorageMount, StorageOpen,
    LogLine,
    ExitRequested,
  };

  struct FrameInfo {
    std::string script;   // 块名
    int line = 0;
    int codeOffset = 0;
    std::string function;
    std::string class_;
  };

  struct DebugEvent {
    EventKind kind;
    std::uint64_t sequence = 0;
    std::uint64_t frame = 0;        // 引擎帧号
    std::string channel;            // io / script / log / engine
    std::string text;               // 人类可读摘要（日志转发也用它）
    // 结构化字段按 kind 取用；未使用的保持默认值。
    std::string name;               // 脚本名 / 存储名 / 失败子系统
    int line = 0;
    std::int64_t size = -1;
    bool ok = true;
    std::string detail;             // 错误文本
    std::vector<FrameInfo> frames;  // ScriptBreak 时有效
  };

  // 引擎 → 调试器（观察平面）
  class IDebugSink {
    public:
      virtual ~IDebugSink() = default;
      virtual void OnEvent(const DebugEvent &event) = 0;
  };

  // 调试器 → 引擎（控制平面）。只有实现 ENABLE_DEBUGGER 的构建才会被调用。
  class IDebugController {
    public:
      virtual ~IDebugController() = default;
      // 每次 VM 换行/调用/返回/异常/断点时调用；返回 true 表示在此暂停等待。
      virtual bool OnScriptLine(const FrameInfo &frame, EventKind reason) = 0;
      // 求值/作用域读取由 controller 通过引擎回调取得（见 §5）。
      virtual bool IsAttached() const = 0;
  };
}
```

宿主（`EngineRuntime`、测试台）持有 sink/controller 指针并交给 `engine/ScriptRuntime`；
`shim` 通过 `ScriptContext` 读取 controller。

## 5. 控制平面如何落地（不改 `tjs/`）

上游 tjs2 自带调试钩子，位于 `ENABLE_DEBUGGER` 宏之后（`tjsInterCodeExec.cpp`）。
已实测：**只要不修改源码**，提供两件事即可编译并启用：

1. `shim/tjs_host/debugger.h`：上游 `#include "debugger.h"` 的落点，提供
   `DBGHOOK_PREV_{CALL,EXE_LINE,RETURN,EXCEPT,BREAK}` 与相关声明
   （该文件本就不在 vendored 树内，属垫层职责）。
2. `shim/tjs_host/IsDebuggerPresent()`：VM 用它判断"是否接调试器"。

钩子点：`DBGHOOK_PREV_EXE_LINE`（每次换行）、`CALL` / `RETURN`（栈）、`EXCEPT`、
`BREAK`。`TJSDebuggerHook` 的实现在 shim（`TjsDebugHost.cpp`）里转发给
`IDebugController::OnScriptLine`。断点/单步逻辑全在 `debugger/agent`，shim 只做转发。

上游 `tjsDebug.cpp` 的 `ENABLE_DEBUGGER` 分支是 Win32 专用（`COPYDATASTRUCT`/`HWND`/
`WM_COPYDATA`），**不编译它**；栈追踪与 `TJSGetStackTraceString` 由 shim 自己实现
（`TJSStackTracerPush/Pop/SetCodePointer` 本来就是无条件调用的，替换现有空实现即可）。

两个实现细节：

- `tTJSStackTracer` 的类定义只在未编译的 `tjsDebug.cpp` 里，shim 只能拿到不完整类型。
  全局 `TJSStackTracer` 指针在 shim 内指向自己的追踪器（`reinterpret_cast`），
  因为 tjs 源码只对它做空指针判断。
- 栈追踪按上游习惯在 `TJSEnableDebugMode` 打开时启用（`tjs.cpp` 的 init/shutdown 会
  `TJSAddRef/ReleaseStackTracer`）；不接调试器时不付每帧 push/pop 的代价，
  `Scripts.getTraceString()` 在未开启调试模式时返回空串（保持现状）。

`TJSEnableDebugMode` 打开后上游只做两件事：初始化/释放对象哈希表与栈追踪、
异常时把源码行打到 console（`tjs.cpp:123/227`、`tjsError.cpp:137/148`）。两者都是调试想要的，
默认关闭，由会话建立时打开。

**已知限制（必须写进 UI 提示）**：字节码脚本只有粗粒度位置信息（真游戏
`system/Initialize.tjs` 全程 `line=1`），断点只能落在函数入口；文本脚本才有行级精度。

## 6. 事件与命令清单

事件（engine → 会话），一行一条 `{"seq":N,"ev":"...","frame":N,...}`：

| 事件 | 字段 | 来源 |
|---|---|---|
| `engine.state` | `from,to` | `EngineRuntime` 状态机 |
| `engine.failure` | `code,subsystem,script,line,trace,recentLog` | 统一失败报告 |
| `script.begin` / `script.end` | `name,size,bytecode,ms` | `ScriptLoader` |
| `script.fail` | `name,message,line,trace` | `ScriptLoader` 的异常路径 |
| `script.break` | `reason,frames[]` | `TJSDebuggerHook` |
| `storage.mount` | `index,kind,path,entries` | 挂载完成后一次性 |
| `storage.open` | `name,backend,hit,size,error` | `Storage::Open` |
| `log.line` | `frame,level,channel,text` | `Logger` |
| `exit.requested` | `-` | `IAppControl` |

命令（会话 → engine）：

| 命令 | 说明 |
|---|---|
| `session.hello` / `session.configure` | 版本协商、开启通道、`TJSEnableDebugMode` |
| `bp.set` / `bp.clear` / `bp.list` | 文件+行（+可选条件表达式） |
| `exec.continue` / `exec.pause` / `exec.step {in\|over\|out}` | 执行控制 |
| `frame.list` / `frame.select` / `frame.locals` | 栈与作用域（经上游 `TJSDebuggerGetLocalVariableString`） |
| `eval {frameId,expr}` / `eval.storage {name}` | 求值（复用 `ScriptLoader`，不改语义） |
| `storage.mounts` / `log.tail {lines,channels}` | 现场快照 |
| `engine.state` / `engine.failures` / `objects.dump` | 状态与对象表（`tTJS::Dump`） |
| `time.fixed {ms}` | 测试用可控时钟（配合 `IPlatformInfo`） |

## 7. 缝合点（seam）明细

每个缝合点都遵守三条规则：**空指针即跳过**、**不发事件时零分配**、**失败不影响主流程**。

1. **`ScriptLoader::RunScriptBytes`**：进入前 `ScriptBegin`（名字、字节数、是否字节码），
   正常返回后 `ScriptEnd`（耗时），异常路径 `ScriptFailure`（消息/行号/trace）后照常重抛。
2. **`Storage::Open`**：出口发送 `StorageOpen`（名字、命中后端、大小、错误文本）。
   另加 `DescribeMounts()` 供 `storage.mounts` 一次性取挂载表。
3. **`Logger::Write`**：把已格式化行转成 `log.line`；调试器自己打的日志带
   `channel=debugger` 并被过滤，避免自激。
4. **`EngineRuntime`**：`OpenGame` 起止、启动状态迁移、`Tick` 帧号、`RequestExit`、
   失败（`FailStartup` 与 `lastError_` 赋值点收敛成 `ReportFailure()`）。
5. **`shim/TjsBridge.h`**：`ScriptContext` 增加 `IDebugController *debug = nullptr`；
   写入点仍是 `ScriptRuntime::Startup`（唯一装配点）。
6. **`shim/tjs_host/TjsDebugHost.cpp`**：替换现有空实现——栈追踪三函数、
   `TJSGetStackTraceString`、`TJSDebuggerHook`、`IsDebuggerPresent`、
   `TJSEnableDebugMode` 的开关接口。

## 8. tjs 侧验证记录

| 项 | 结果 |
|---|---|
| `-DENABLE_DEBUGGER` + shim 版 `debugger.h` 编译 `tjsInterCodeExec.cpp` | ✅ 通过（GCC 15.2，无 tjs 改动） |
| 同参数编译 `tjs.cpp` / `tjsError.cpp` / `tjsScriptBlock.cpp` / `tjsInterCodeGen.cpp` / `tjsNative.cpp` / `tjsObject.cpp` | ✅ 通过 |
| `tjsDebug.cpp` 非 DEBUGGER 部分在 GCC 下编译 | ❌ 4 处 `static` 显式特化（MSVC 写法）；因此继续排除该文件，其符号由 shim 提供 |
| `debugger.h` 是否在 vendored 树内 | ❌ 不存在，需由 shim 提供（与 `targetver.h` 同类） |
| Linux/Android 的 `IsDebuggerPresent` | 由 shim 定义（声明放 `tjs_prelude.h` 的非 Windows 分支），返回"是否已接调试器" |

## 9. 阶段计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| **D1 观察平面** | 契约、`Storage`/`ScriptLoader`/`EngineRuntime`/`Logger` 缝合、`DebugAgent` 只写 `.jsonl` 录制 | `debug_probe --record` 跑断言脚本，产出事件流；CTest 校验事件序列与计数 |
| **D2 栈与失败** | shim 栈追踪 + `TJSGetStackTraceString`；`FailureReport`（技术债 #40 的延续） | 真游戏失败时事件里带 `frames[]` 与 `recentLog`；`Scripts.getTraceString()` 非空 |
| **D3 控制平面** | `ENABLE_DEBUGGER` 目标开关、`debugger.h`、`TJSDebuggerHook`、`bp/step/continue` | 新增 CTest：脚本在第 N 行命中断点 → `script.break` → 单步 → `eval` 断言值 → 继续 |
| **D4 会话与 CLI** | TCP/stdio 会话、`krkr_debug` REPL、`log.tail`/`storage.mounts`/`objects.dump` | 人工：连真游戏，断点+求值可用；README 记录命令 |
| **D5 DAP 适配** | `dap/`，把命令/事件映射到 DAP | VSCode（或 Android Studio）attach 后能下断点、看栈与变量 |
| **D6 记录/回放** | `.jsonl` 回放、固定时钟、Android `adb forward` | 崩溃现场可离线复现；CI 用回放做回归 |

每阶段独立可回滚；`KRKR_ENABLE_DEBUGGER=OFF`（默认）时 D3 之后的所有钩子编译为空。

## 10. 替代方案与取舍

| 方案 | 能拿到什么 | 代价 | 结论 |
|---|---|---|---|
| 本方案（seam + 子包） | 断点/单步/求值/事件流/栈 | 现有文件 ~60 行空指针检查 | ✅ 采用 |
| 只读日志文件 + 事后分析 | 时间线、失败文本 | 0 改动 | 作为 D1 的回放输入保留，但无法交互 |
| `LD_PRELOAD` / 符号拦截 | 部分 I/O 事件 | Android 上不可用；拿不到脚本状态 | ❌ |
| 复制一份带 `#ifdef` 的引擎构建 | 想加什么加什么 | 两条代码路径，回归不可信 | ❌ |
| 修改 `tjs/` 加自定义钩子 | 任意粒度 | 违反 §4 约束，需要 VENDOR 证据 | ❌（上游已有钩子，无需修改） |

## 11. 与既有技术债的关系

- 技术债 #40（空消息）已在桥接层修完；本规格的 `FailureReport` 是它的结构化版本。
- 技术债 #41（`Debug.addLoggingHandler` 未实现）：D1 之后可直接实现——日志→脚本回调
  就是同一个 `IDebugSink` 的脚本侧消费者。
- 技术债 #42（`Debug.startLogToFile`）：由会话的 `log.line` 落盘替代脚本侧的开关。
- 技术债 #49（`real_startup` 测试 DISABLED）：D1 的事件流可在 Window 未实现前
  断言"失败点仍是 Window"，比 DISABLED 更有信息量。
