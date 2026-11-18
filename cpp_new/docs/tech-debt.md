# 技术债与已知问题

> 编号（#1–#80）在历史文档与提交信息中被引用，**不得重排或复用**；已解决的条目保留并标注。
> 新增条目追加到表尾，编号继续递增。问题与决策的叙述在 `DECISIONS.md`，本文件只保留可追踪的条目。
>
> 来源：原 `cpp_new/todo.md` §5（2025-11 整理时迁出）。
> 写作规范（`AGENTS.md` §2）确立于迁出之后；存量条目的文风在相关代码被修改时一并规范化。

| # | 问题 | 处理 |
|---|---|---|
| 1 | `main.cpp:25` 少一个右括号，编译失败 | ✅ 已修 |
| 2 | 头文件里 `#include <cstdio>` 是无用的 | ✅ 已删 |
| 3 | 函数定义后多余的 `;` | ✅ 已清 |
| 4 | `EngineRuntime.h` 缺 `#include <cstdint>`，靠 `<string>` 传递包含 | ✅ 已补 |
| 5 | 目录 `core/env/app/` 与 namespace `krkr::environ` 不一致 | ✅ 定为 `krkr::env`（理由见 §2） |
| 6 | `core/base/core/src/krkrMsgImpl.cpp` 是老树消息系统的搬运草稿，include 了 `tjsCommHead.h` 等**老树头**，在 `cpp_new` 里编不过 | 保留，**不要接进任何构建**，第 7 步处理 |
| 7 | `tests/` 里没有 `cpp_new` 的 CI | ✅ 已解决：`cpp_new` 自带 CMake + CTest（23 项，`tools/run_probes.sh`）；老树根目录 `tests/` 已删除（ABI 守卫迁入 `bridge` 并改为 `static_assert`） |
| 8 | `krkr::environ` 在 Windows/MinGW 上编不过（`environ` 是 `stdlib.h` 的宏） | ✅ 已改 `krkr::env`；以后任何 namespace/类型名都不许用 C 运行时标识符 |
| 9 | 方法名 `GetLastError` 与 `windows.h` 的宏同名 | ✅ 已改 `GetLastErrorText` |
| 10 | **启动仍在 tick 线程上同步执行** | 见下方「触发条件」。现在只做一次 `exists()`，微秒级；第 7 步接 TJS 后若实测 `OpenGame` 耗时超过一帧预算（>50 ms @60fps），就必须把启动挪到 worker 线程 —— 判据是实测数据，不是接口形状 |
| 11 | 启动期日志在 `Drain` 时可能已被 tick 日志挤出 256 行环（`KRKR_LOG_LEVEL=debug` + 跑很多帧后才 drain） | 真宿主是启动结束立刻 drain，不会发生；如果将来要保底，把启动期日志单独存一份 |
| 12 | 日志文件没有 fsync，只 flush 到 libc 缓冲 | 进程被 kill 时可能丢最后几行。第 11 步与 `engine_set_log_file_path` 一起评估 |
| 13 | **`Storage::Open` 放行 `..`**（本阶段的决定：完全遵循系统路径语义） | 后果：接上 TJS 后脚本可以用 `../` 读到游戏目录外面。若要收紧，在 `Storage::Open` 里加一句“解析后必须仍在 root 内”的检查即可（+3 行），调用方不用改。兼容性需届时用真游戏测 |
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
| 40 | 脚本错误消息可能为空串 | ✅ 已修：消息表由 `tools/gen_tjs_message_map.py` 从上游参照实现生成到 shim，异常文本带上下文/块名/行号/trace（见 `archive/todo-2025-11.md` §4.16） |
| 41 | `Debug.addLoggingHandler` / `removeLoggingHandler` 未实现 | 需要 `core/log` 到脚本层的回调契约（且要防重入：处理函数里再写日志）。当前显式返回 `TJS_E_NOTIMPL` 并记录 |
| 42 | `Debug.startLogToFile(clear)` 不支持清空；`logToFileOnError` / `clearLogFileOnError` 只保存取值 | 日志文件由宿主设定（ABI `engine_set_log_file_path`）；错误触发的重定向/清空需要与第 11 步的宿主管辖一起设计 |
| 43 | `Plugins` 只支持静态注册表 | 动态模块加载（`.dll`/`.so`）、`linkZ`（`bres://` 资源）、`bootStrap`、`CanLoadPlugin` 均未实现；未命中名字显式失败并带名字 |
| 44 | `System` 只覆盖已出现过的成员 | ✅ 已补 `Initialize.tjs` 需要的路径/平台/参数成员；`screenWidth`、`createAppLock`、`system(...)`、DLL 目录相关成员尚未实现（未命中时显式报错） |
| 45 | 字节码只验证了真游戏的 `system/Initialize.tjs` | `LoadByteCode` 的返回值未用于区分「字节码」与「二进制字典数组」；目前只在 `IsTJS2ByteCode` 命中时调用，不会把文本误当字节码 |
| 46 | `Window` / `Layer` 只覆盖 `Initialize.tjs` 需要的形状 | ✅ 7g-1 已落地几何/可见性/opacity/type/clip/loadImages/层级移动（`archive/todo-2025-11.md` §4.27）；`Bitmap`、事件派发、`children`、字体绘制仍未做（#64） |
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
| 69 | 断言脚本必须 `throw new Exception("...")` | 纯字符串抛出到宿主只剩 `script exception`，`-ok` 会丢；原因见 `archive/todo-2025-11.md` §4.27 第 2 点 |
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
