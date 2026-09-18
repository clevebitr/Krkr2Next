#include "engine_api.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <memory>
#include <vector>

#include <csignal>
#include <cstdlib>
#include <sys/stat.h>
#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
// Defined in engine_api_android_jni.cpp inside `extern "C" { ... }`, so the
// declarations MUST also be `extern "C"` (C linkage). Declaring them with
// default C++ linkage produces a mangled symbol (_Z20krkr_GetNativeWindowv)
// that cannot resolve against the unmangled C symbol the JNI file exports →
// ld.lld undefined symbol at link time.
extern "C" ANativeWindow *krkr_GetNativeWindow();
extern "C" void krkr_GetSurfaceDimensions(uint32_t *, uint32_t *);
#endif
#if !defined(__ANDROID__)
#include <execinfo.h>
#else
// Android/bionic native crash backtrace. We must read the interrupted thread's
// registers from the ucontext passed to a SA_SIGINFO handler: _Unwind_Backtrace
// called from a plain handler only unwinds the signal-delivery frame (handler
// -> sigreturn), which shows nothing useful. ucontext.uc_mcontext gives the
// real PC/LR of the crashing thread; dladdr symbolizes each address.
// Android/bionic 原生崩溃栈：必须从 SA_SIGINFO 处理器收到的 ucontext
// 读取被中断线程 的 PC/LR；用普通处理器调 _Unwind_Backtrace
// 只会展开信号投递帧（handler->sigreturn）， 看不到实际崩溃点。dladdr
// 负责符号化。
#include <ucontext.h>
#include <dlfcn.h>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "environ/Application.h"
#include "environ/Platform.h"
#include "environ/EngineBootstrap.h"

// 日志去重/限频门闩。经 krkr2core 导出的 cpp/core 包含路径引入
// （见 cpp/core/CMakeLists.txt 的 INTERFACE include）。
#include "utils/LogUtil.h"
#include "utils/StallWatchdog.h"
#include "environ/EngineLoop.h"
#include "environ/MainScene.h"
#include "base/StorageIntf.h"
#include "base/SysInitIntf.h"
#include "base/EventIntf.h"
#include "base/ScriptMgnIntf.h"
#include "base/impl/SysInitImpl.h"
#include "base/impl/StorageImpl.h"
#include "extension/Extension.h"
#include "visual/GraphicsLoaderIntf.h"
#include "visual/ogl/ogl_common.h"
#include "visual/ogl/krkr_egl_context.h"
#include "visual/impl/WindowImpl.h"
#include "visual/RenderManager.h"
#include "visual/WindowIntf.h"
#include "visual/TransIntf.h"
#include "visual/FontImpl.h"
#include "visual/FreeTypeFontRasterizer.h"
#include "visual/impl/LayerBitmapImpl.h"
#include "visual/impl/BitmapBitsAlloc.h"
#include "plugin/PluginImpl.h"
#include "psbfile/PSBMedia.h"
#include "engine_options.h"

int TVPDrawSceneOnce(int interval);

// overlay 电影是否有待呈现的帧（ui_stubs.cpp 导出：本地 extern 声明，与
// TVPHostSubmitVideoOverlayFrame 同一约定）。
bool TVPHostVideoOverlayFrameActive();

// 模态对话框期间由 core 每帧回调的输入泵（定义见文件后半段，engine_create 里注册）。
static void PumpModalInputOnTickThread();

extern "C" void TVPRegisterKrkrGLESPluginAnchor();
#ifdef KRKR2_LIVE2D
// krkrlive2d.cpp 仅在 cubism SDK
// 存在时编译；缺失时不应引用其锚点，否则链接期未定义。
extern "C" void TVPRegisterKrkrLive2DPluginAnchor();
#endif

struct engine_handle_s {
    std::recursive_mutex mutex;
    std::string last_error;
    int state = 0;
    std::thread::id owner_thread;
    bool runtime_owner = false;
    uint64_t tick_count = 0;

    // Frame state — readback buffer and tracking
    struct FrameState {
        uint32_t surface_width = 1280;
        uint32_t surface_height = 720;
        uint64_t serial = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride_bytes = 0;
        std::vector<uint8_t> rgba;
        bool ready = false;
        bool rendered_this_tick = false;
    } frame;

    // Frame rate limiting (0 = unlimited / follow vsync)
    struct FpsLimitState {
        uint32_t limit = 0;
        uint64_t interval_us = 0;
        std::chrono::steady_clock::time_point last_render_time{};
        bool initialized = false;
    } fps;

    // 每帧耗时统计（引擎侧）。壳侧的 perf 只能看到 engine_tick 总耗时，
    // 这里把一帧拆成 update（Application::Run）与 post（呈现/回收/交付）两段，
    // 按 5s 窗口汇总一行，用于回答"卡在引擎内部还是宿主交付"。
    // 只在 tick 线程访问，无需加锁。
    struct FramePerfState {
        std::chrono::steady_clock::time_point window_start{};
        uint64_t frames = 0;
        // 本窗口内的累计耗时，单位微秒；结算后清零。
        uint64_t update_us = 0;
        uint64_t post_us = 0;
        // 尖峰：单帧 update/post 的最大值，用于暴露"偶发一帧巨慢"。
        uint64_t max_update_us = 0;
        uint64_t max_post_us = 0;
        // 本帧 update 段耗时，供 post 段结算"整帧是否超预算"。
        uint64_t last_update_us = 0;
        // 整帧（update+post）超过 33ms（≈30fps 下限）的帧数，反映掉帧规模。
        uint64_t slow_frames = 0;
        std::chrono::steady_clock::time_point prev_tick_end{};
        uint64_t long_gap_frames = 0; // 两次 tick 间隔 > 100ms 的次数
    } perf;

    // 引擎侧帧耗时统计总开关。默认开启：它只在 5s 边界拼一条日志，
    // 对每帧路径只加两次 steady_clock::now()，开销可忽略，而"没有引擎侧数据"
    // 会让卡顿排查只能靠猜（原状）。可用选项 frame_perf=off 关闭。
    bool perf_enabled = true;

    // Input event queue
    struct InputState {
        std::deque<engine_input_event_t> pending_events;
        std::unordered_set<intptr_t> active_pointer_ids;
        bool native_mouse_callbacks_disabled = false;
    } input;

    // Render target state
    struct RenderTargetState {
        bool iosurface_attached = false;
        bool native_window_attached = false;
    } render;

    struct StartupState {
        std::mutex mutex;
        uint32_t state = ENGINE_STARTUP_STATE_IDLE;
        std::deque<std::string> logs;
        std::thread worker;
        bool worker_running = false;
    } startup;

    struct MemoryOptionState {
        int psb_cache_mb = 0;
        int psb_cache_entries = 0;
    } memory_options;
};

namespace {

#if defined(__ANDROID__)
    void AndroidInfoLog(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        __android_log_vprint(ANDROID_LOG_INFO, "krkr2", fmt, args);
        va_end(args);
    }
#endif

    enum class EngineState {
        kCreated = 0,
        kOpened,
        kPaused,
        kDestroyed,
    };

    inline int ToStateValue(EngineState state) {
        return static_cast<int>(state);
    }

    std::recursive_mutex g_registry_mutex;
    std::unordered_set<engine_handle_t> g_live_handles;
    thread_local std::string g_thread_error;
    engine_handle_t g_runtime_owner = nullptr;
    bool g_runtime_active = false;
    bool g_runtime_started_once = false;
    bool g_engine_bootstrapped = false;
    bool g_runtime_startup_active = false;
    engine_handle_t g_runtime_startup_owner = nullptr;
    std::once_flag g_loggers_init_once;
    std::shared_ptr<spdlog::sinks::sink> g_startup_log_sink;
    constexpr size_t kMaxStartupLogs = 4000;

    // Optional rotating file sink (set via engine_set_log_file_path on mobile).
    std::mutex g_logfile_mutex;
    std::string g_log_file_path;
    std::shared_ptr<spdlog::sinks::sink> g_file_sink;

    void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg &msg);

    // ── 游戏兼容档（compat profile）─────────────────────────────────────
    // 把"老 KiriKiri2 系"与"krkrz / AetherKiri 系"的差异收敛成具名 + 带版本号的
    // 档，再映射到**既有选项**（目前是 ogldrawdevice_compat）。插件与核心照旧只认
    // 选项，不需要知道"档"这个概念，更不允许在核心里写"某个游戏要怎么办"。
    //
    // 自动判档只看**血脉标记**（游戏目录里有哪些插件/provider），不看游戏名字。
    // 档的口径一旦变化必须同时 +1 版本号，便于真机日志区分。
    struct CompatProfile {
        const char *name;
        int version;
        const char *ogldrawdevice;
    };

    const CompatProfile kCompatProfiles[] = {
        {ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2, 1,
         ENGINE_OGLDRAWDEVICE_COMPAT_OFF},
        {ENGINE_GAME_COMPAT_PROFILE_KRKRZ_GPU, 1,
         ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS},
        {ENGINE_GAME_COMPAT_PROFILE_KRKRZ_KAG, 1,
         ENGINE_OGLDRAWDEVICE_COMPAT_KAG},
        {ENGINE_GAME_COMPAT_PROFILE_KRKRZ_OGL, 1,
         ENGINE_OGLDRAWDEVICE_COMPAT_OGL},
    };

    std::mutex g_compat_mutex;
    std::string g_compat_request = ENGINE_GAME_COMPAT_PROFILE_AUTO;
    std::string g_compat_game_root;
    bool g_ogldrawdevice_explicit = false;
    // 上一次真正记录过的判档结论（见 ApplyCompatProfileLocked 的幂等判断）。
    std::string s_last_resolved_key;
    // 解析结果，供 engine_get_compat_profile 读给壳显示（性能叠加层用）。
    std::string g_compat_resolved_name;
    std::string g_compat_resolved_mode;

    const CompatProfile *FindCompatProfile(const std::string &name) {
        for(const auto &p : kCompatProfiles) {
            if(name == p.name)
                return &p;
        }
        return nullptr;
    }

    bool CompatFileExists(const std::string &path) {
        struct stat st;
        return ::stat(path.c_str(), &st) == 0 &&
               (st.st_mode & S_IFMT) == S_IFREG;
    }

    // 血统标记 → 档。顺序即优先级：带 krkrgles/Live2D 的必是 krkrz GPU 层系；
    // 带 E-mote(motionplayer) 的走 KAG 窗口绘制设备工厂；其余按老 KiriKiri2 处理。
    const char *DetectCompatProfileByMarkers(const std::string &root) {
        static const char *kGpuMarkers[] = { "krkrgles.dll", "krkrlive2d.dll" };
        static const char *kKagMarkers[] = { "motionplayer.dll",
                                            "motionplayer_nod3d.dll" };
        for(const char *m : kGpuMarkers) {
            if(CompatFileExists(root + "/plugin/" + m))
                return ENGINE_GAME_COMPAT_PROFILE_KRKRZ_GPU;
        }
        for(const char *m : kKagMarkers) {
            if(CompatFileExists(root + "/plugin/" + m))
                return ENGINE_GAME_COMPAT_PROFILE_KRKRZ_KAG;
        }
        return ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2;
    }

    // 调用方必须持有 g_compat_mutex。
    void ApplyCompatProfileLocked() {
        // ── `auto` 档必须等 game_root 到齐，否则**不要写任何值** ───────────────
        //
        // 壳是分两条选项下发的（`game_compat_profile` 与 `game_compat_game_root`），
        // 而 `auto` 判档只能靠 game_root 里的插件标记。若在 game_root 还没到的第一
        // 条就解析，就会得到默认档（kirikiri2-classic → off）并把 off 写进命令行。
        //
        // 致命之处在于 `TVPSetCommandLine` 是**先写先赢**（同名的后一条不生效；
        // 真机日志里那句 "earlier item has more priority" 就是它）：等 game_root
        // 到了再写 `kag`，插件读到的仍然是那个更早的 `off`。真机症状正是如此——
        // 兼容档日志明明判成 `krkrz-kag v1 -> ogldrawdevice_compat=kag (auto)`，
        // 却完全没有 `krkrgles: ogldrawdevice_compat=kag 已启用` 那一行，于是
        // GPU 闸门从未打开、千恋万花(默认 auto 档)黑屏进不去；而 G1 恰好被判成
        // classic/off，掩盖了这个 bug。
        //
        // 所以：`auto` 且 game_root 为空时直接返回，等第二条选项到了再一次性解析。
        // 显式具名档不依赖 game_root，可以立即生效。
        if(g_compat_request == ENGINE_GAME_COMPAT_PROFILE_AUTO &&
           g_compat_game_root.empty()) {
            static bool s_defer_logged = false;
            if(!s_defer_logged) {
                s_defer_logged = true;
                spdlog::info("compat profile: auto 档等待 game_root（不写 "
                             "ogldrawdevice_compat，避免先写先赢把档位锁死）");
            }
            return;
        }

        const CompatProfile *prof = nullptr;
        const char *source = nullptr;
        if(g_compat_request != ENGINE_GAME_COMPAT_PROFILE_AUTO) {
            prof = FindCompatProfile(g_compat_request);
            source = "explicit";
            if(!prof) {
                spdlog::warn("compat profile: 未知档位 '{}'，按默认档处理",
                             g_compat_request);
            }
        } else if(!g_compat_game_root.empty()) {
            const char *detected = DetectCompatProfileByMarkers(g_compat_game_root);
            prof = FindCompatProfile(detected);
            source = "auto";
        }
        if(!prof) {
            prof = FindCompatProfile(ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2);
            if(!source)
                source = "default";
        }
        if(!prof)
            return;

        g_compat_resolved_name = prof->name;
        g_compat_resolved_mode = prof->ogldrawdevice;
        // 幂等：profile 与 game_root 是分两条选项下发的，第二条到达时会把同一份
        // 结论再解析一遍。真机日志里因此出现过**逐字重复**的两行
        // "compat profile: krkrz-kag v1 -> ..."。这里只对**日志**去重。
        //
        // ⚠️ 绝不能因此跳过下面的 TVPSetCommandLine：该选项是"先写先赢"，而且
        // 这些静态变量在同一进程的多次开游戏之间**不会重置**（跨 engine_destroy）。
        // 若按"同键就提前返回"，第二次开同类档游戏时就不会再写选项，一旦命令行
        // 配置在重启时被清过，插件读到的就是空 → 兼容档失效 → 正是注释里记载的
        // "千恋万花黑屏进不去"。写操作必须每次都执行，只有日志允许省。
        const std::string resolved_key =
            std::string(prof->name) + "|" + prof->ogldrawdevice + "|" +
            source + "|" + (g_ogldrawdevice_explicit ? "1" : "0") + "|" +
            g_compat_game_root;
        const bool same_as_last = (resolved_key == s_last_resolved_key);
        s_last_resolved_key = resolved_key;
        // 显式传过 ogldrawdevice_compat 时以它为准：这里只记录，不覆盖。
        if(!same_as_last) {
            spdlog::info(
                "compat profile: {} v{} -> ogldrawdevice_compat={} ({}{})",
                prof->name, prof->version, prof->ogldrawdevice, source,
                g_ogldrawdevice_explicit ? ", 但显式选项优先" : "");
        }
        if(g_ogldrawdevice_explicit)
            return;
        if(!g_compat_game_root.empty() &&
           g_compat_request == ENGINE_GAME_COMPAT_PROFILE_AUTO) {
            if(!same_as_last) {
                spdlog::info("compat profile: 判档依据 game_root={}",
                             g_compat_game_root);
            }
        }
        TVPSetCommandLine(TJS_W("ogldrawdevice_compat"),
                          ttstr(prof->ogldrawdevice).c_str());
    }

    void ApplyCompatProfile() {
        std::lock_guard<std::mutex> lock(g_compat_mutex);
        ApplyCompatProfileLocked();
    }

    class StartupLogSink final : public spdlog::sinks::sink {
    public:
        void log(const spdlog::details::log_msg &msg) override {
            PushRuntimeSpdlogToStartupQueue(msg);
        }

        void flush() override {}
        void set_pattern(const std::string &) override {}
        void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
    };

    std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char *name) {
        if(auto logger = spdlog::get(name); logger != nullptr) {
            return logger;
        }
        return spdlog::stdout_color_mt(name);
    }

    // Dumps the crashing thread's native stack to the log.
    // - Non-Android: glibc backtrace() + backtrace_symbols().
    // - Android (bionic): read PC/LR from the interrupted context handed to a
    //   SA_SIGINFO handler (ucontext.uc_mcontext), then dladdr for symbols;
    //   plus a best-effort AArch64 frame-pointer walk capped by safe bounds so
    //   a corrupted FP cannot send us into an unmapped address and
    //   double-fault.
    // 打印崩溃线程原生调用栈到日志：
    // - 非 Android：glibc backtrace + backtrace_symbols；
    // - Android(bionic)：从 ucontext.uc_mcontext 读被中断线程的 PC/LR，dladdr
    // 符号化，
    //   并尽力按 AArch64 帧指针逐帧回溯（用安全边界约束，避免坏 FP 二次崩溃）。
    void DumpNativeBacktrace(void *ucontext) {
#if !defined(__ANDROID__)
        void *frames[32];
        int count = static_cast<int>(backtrace(frames, 32));
        char **symbols = backtrace_symbols(frames, count);
        if(symbols) {
            for(int i = 0; i < count; ++i) {
                spdlog::critical("  [{}] {}", i, symbols[i]);
            }
            free(symbols);
        }
#else
        auto *uc = static_cast<ucontext_t *>(ucontext);
        if(!uc) {
            spdlog::critical("  (no ucontext captured)");
            return;
        }
        auto dumpSym = [](int i, uintptr_t addr) {
            const char *sym = "<unknown>";
            const char *obj = "<unknown>";
            Dl_info info;
            if(dladdr(reinterpret_cast<void *>(addr), &info) != 0) {
                if(info.dli_sname)
                    sym = info.dli_sname;
                if(info.dli_fname)
                    obj = info.dli_fname;
            }
            spdlog::critical("  [{}] {:#016x} {} (in {})", i, addr, sym, obj);
        };

#if defined(__aarch64__)
        const uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
        const uintptr_t lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
        uintptr_t fp = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
        dumpSym(0, pc);
        dumpSym(1, lr);
        // Best-effort frame-pointer walk: [fp] = saved fp, [fp+8] = saved lr.
        // Bounds are tight so a corrupt fp just stops the walk instead of
        // faulting.
        const uintptr_t kLow = 0x1000;
        const uintptr_t kHigh = 0x400000000000;
        for(int i = 2; i < 48 && fp > kLow && fp < kHigh; ++i) {
            uintptr_t saved_lr = 0;
            saved_lr = *reinterpret_cast<volatile uintptr_t *>(fp + 8);
            uintptr_t next_fp = *reinterpret_cast<volatile uintptr_t *>(fp);
            dumpSym(i, saved_lr);
            fp = next_fp;
        }
#elif defined(__arm__)
        dumpSym(0, static_cast<uintptr_t>(uc->uc_mcontext.arm_pc));
        dumpSym(1, static_cast<uintptr_t>(uc->uc_mcontext.arm_lr));
#else
        dumpSym(0, 0);
#endif
#endif
    }

    void CrashSignalHandler(int sig, siginfo_t * /*info*/, void *ucontext) {
        spdlog::critical("FATAL SIGNAL {} received!", sig);

        // Print a mini backtrace on every supported platform.
        // Android de-optimizes far enough that a native stack is essential to
        // locate the crashing call site; see DumpNativeBacktrace above.
        // 在所有平台打印简易原生栈；Android 上必须靠它定位崩溃点。
        DumpNativeBacktrace(ucontext);

        spdlog::default_logger()->flush();
        // Re-raise so the OS generates a proper crash report
        signal(sig, SIG_DFL);
        raise(sig);
    }

    void InstallCrashSignalHandlers() {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = CrashSignalHandler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        // SA_SIGINFO passes the interrupted thread's ucontext so we can dump
        // its real PC/LR backtrace (a plain signal() handler cannot).
        // SA_SIGINFO 让处理器拿到被中断线程的 ucontext，从而打出真实 PC/LR
        // 崩溃栈。
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGFPE, &sa, nullptr);
    }

    void EnsureInternalPluginAnchorsLinked() {
        TVPRegisterKrkrGLESPluginAnchor();
#ifdef KRKR2_LIVE2D
        TVPRegisterKrkrLive2DPluginAnchor();
#endif
    }

    void EnsureRuntimeLoggersInitialized() {
        std::call_once(g_loggers_init_once, []() {
        // 日志级别选择优先级：
        //   1. 编译期显式指定 KRKR_LOG_LEVEL_NUM（由 CMake 的 KRKR_LOG_LEVEL
        //   传入，
        //      CI/命令行可独立于 debug/release 单独选级别，见
        //      CMakeLists.txt）。
        //   2. 否则按构建类型自动：Release→info，Debug→debug。
        // Release 默认 info：避免大量 [debug] 日志（如 storage
        // 路径探测）刷爆日志文件； Debug 构建保持 verbose，方便开发排查。
#if defined(KRKR_LOG_LEVEL_NUM)
            const auto krkr_level =
                static_cast<spdlog::level::level_enum>(KRKR_LOG_LEVEL_NUM);
            spdlog::set_level(krkr_level);
            // 关键日志即时落盘；off(=6) 时 flush_on 亦为
            // off，日志仍会正常写入（由 sink 落盘）
            spdlog::flush_on(krkr_level);
#elif defined(NDEBUG)
    spdlog::set_level(spdlog::level::info);
    // 重要日志即时落盘
    spdlog::flush_on(spdlog::level::info);
#else
    spdlog::set_level(spdlog::level::debug);
    // Flush every log message so crash logs are never lost
    spdlog::flush_on(spdlog::level::debug);
#endif
            auto core_logger = EnsureNamedLogger("core");
            auto tjs2_logger = EnsureNamedLogger("tjs2");
            auto plugin_logger = EnsureNamedLogger("plugin");
            g_startup_log_sink = std::make_shared<StartupLogSink>();
            auto attach_sink =
                [](const std::shared_ptr<spdlog::logger> &logger) {
                    if(logger == nullptr || g_startup_log_sink == nullptr) {
                        return;
                    }
                    auto &sinks = logger->sinks();
                    const auto already_attached = std::any_of(
                        sinks.begin(), sinks.end(),
                        [](const std::shared_ptr<spdlog::sinks::sink> &sink) {
                            return sink.get() == g_startup_log_sink.get();
                        });
                    if(!already_attached) {
                        sinks.push_back(g_startup_log_sink);
                    }
                };
            attach_sink(core_logger);
            attach_sink(tjs2_logger);
            attach_sink(plugin_logger);
            if(core_logger != nullptr) {
                spdlog::set_default_logger(core_logger);
            }
            InstallCrashSignalHandlers();
        });
    }

    // ── 引擎侧每帧耗时统计 ────────────────────────────────────────────────
    // 只在 tick 线程调用。5 秒结算一次，输出一行与壳侧 "perf:" 同口径的汇总，
    // 用于把"卡顿"定位到引擎更新段还是呈现/交付段。
    // 热路径上不做任何字符串构造，只有结算那一帧才拼一次日志。
    void FramePerfAccumulate(
        engine_handle_s *impl,
        const std::chrono::steady_clock::time_point &update_begin,
        const std::chrono::steady_clock::time_point &update_end) {
        if(impl == nullptr || !impl->perf_enabled)
            return;
        auto &p = impl->perf;
        const uint64_t update_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(update_end -
                                                                 update_begin)
                .count());
        p.frames++;
        p.update_us += update_us;
        if(update_us > p.max_update_us)
            p.max_update_us = update_us;
        // post 段在本帧稍后才计时（见 FramePerfAccumulatePost），所以这里先把
        // update 段存下来，由那边按"update+post"判定整帧是否超预算。
        p.last_update_us = update_us;

        // 两次 tick 的间隔：暴露宿主长时间没来 tick，或引擎内部把线程阻塞了。
        if(p.prev_tick_end.time_since_epoch().count() != 0) {
            const uint64_t gap_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    update_begin - p.prev_tick_end)
                    .count());
            if(gap_ms > 100)
                p.long_gap_frames++;
        }
        p.prev_tick_end = update_end;

        if(p.window_start.time_since_epoch().count() == 0)
            p.window_start = update_begin;
        const uint64_t elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                update_begin - p.window_start)
                .count());
        if(elapsed_ms < 5000 || p.frames == 0)
            return;

        const double frames = static_cast<double>(p.frames);
        const double fps = frames * 1000.0 / static_cast<double>(elapsed_ms);
        const double avg_update =
            static_cast<double>(p.update_us) / frames / 1000.0;
        const double avg_post =
            static_cast<double>(p.post_us) / frames / 1000.0;
        spdlog::info(
            "frame_perf: fps={:.1f} update_avg={:.2f}ms post_avg={:.2f}ms "
            "update_max={:.2f}ms post_max={:.2f}ms frames={} slow(>33ms)={} "
            "tick_gap(>100ms)={}",
            fps, avg_update, avg_post,
            static_cast<double>(p.max_update_us) / 1000.0,
            static_cast<double>(p.max_post_us) / 1000.0, p.frames,
            p.slow_frames, p.long_gap_frames);

        p.window_start = update_begin;
        p.frames = 0;
        p.update_us = 0;
        p.post_us = 0;
        p.max_update_us = 0;
        p.max_post_us = 0;
        p.slow_frames = 0;
        p.long_gap_frames = 0;
    }

    // 记录一帧的 post 段（呈现 / 纹理回收 / 帧交付）耗时。
    void FramePerfAccumulatePost(
        engine_handle_s *impl,
        const std::chrono::steady_clock::time_point &begin,
        const std::chrono::steady_clock::time_point &end) {
        if(impl == nullptr || !impl->perf_enabled)
            return;
        const uint64_t post_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count());
        impl->perf.post_us += post_us;
        if(post_us > impl->perf.max_post_us)
            impl->perf.max_post_us = post_us;
        // 整帧 = update + post。两段分两次计时，只有在这里才能得到整帧口径，
        // 因此"掉帧数"在本函数里结算。33ms ≈ 30fps 下限。
        if(impl->perf.last_update_us + post_us > 33000)
            impl->perf.slow_frames++;
    }

    void SetThreadError(const char *message) {
        g_thread_error = (message != nullptr) ? message : "";
    }

    engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                            const char *message) {
        SetThreadError(message);
        return result;
    }

    bool IsHandleLiveLocked(engine_handle_t handle) {
        return g_live_handles.find(handle) != g_live_handles.end();
    }

    engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                         engine_handle_s **out_impl) {
        // A handle is valid only while present in the live registry; a non-null
        // pointer alone is not sufficient after engine_destroy.
        // handle 只有仍在 live registry 中才有效；engine_destroy
        // 后非空指针也不能使用。
        if(handle == nullptr) {
            return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                           "engine handle is null");
        }
        if(!IsHandleLiveLocked(handle)) {
            return SetThreadErrorAndReturn(
                ENGINE_RESULT_INVALID_ARGUMENT,
                "engine handle is invalid or already destroyed");
        }
        *out_impl = reinterpret_cast<engine_handle_s *>(handle);
        return ENGINE_RESULT_OK;
    }

    void SetHandleErrorLocked(engine_handle_s *impl, const char *message) {
        impl->last_error = (message != nullptr) ? message : "";
    }

    engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s *impl,
                                                  engine_result_t result,
                                                  const char *message) {
        SetHandleErrorLocked(impl, message);
        return result;
    }

    engine_result_t ValidateHandleThreadLocked(engine_handle_s *impl) {
        if(impl->owner_thread != std::this_thread::get_id()) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "engine handle must be used on the thread where engine_create "
                "was called");
        }
        return ENGINE_RESULT_OK;
    }

    void ClearHandleErrorLocked(engine_handle_s *impl) {
        impl->last_error.clear();
    }

    void PushStartupLog(engine_handle_s *impl, const std::string &message) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        impl->startup.logs.push_back(message);
        while(impl->startup.logs.size() > kMaxStartupLogs) {
            impl->startup.logs.pop_front();
        }
    }

    void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg &msg) {
        // 锁必须覆盖到 PushStartupLog 为止。engine_destroy 在同一把锁内把
        // handle 从 g_live_handles 摘除（此后才 delete impl），所以只要持锁
        // 期间 IsHandleLiveLocked 为真，impl 就不会被释放。反之若先解锁再推送，
        // 取到 target 到真正写入之间就存在窗口，日志会落在已 delete 的对象上。
        // 该 sink 挂在 core / tjs2 / plugin 三个 logger 上，任意线程都能触发，
        // 关停或重开游戏时即为间歇性崩溃。锁是 recursive 的：从已持锁线程
        // （例如整帧持锁的 engine_tick）重入是安全的。
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        if(!g_runtime_startup_active || g_runtime_startup_owner == nullptr) {
            return;
        }
        if(!IsHandleLiveLocked(g_runtime_startup_owner)) {
            return;
        }
        engine_handle_s *target =
            reinterpret_cast<engine_handle_s *>(g_runtime_startup_owner);

        const auto level_sv = spdlog::level::to_string_view(msg.level);
        const std::string level(level_sv.data(), level_sv.size());
        std::string logger(msg.logger_name.data(), msg.logger_name.size());
        if(logger.empty()) {
            logger = "core";
        }
        const std::string payload(msg.payload.data(), msg.payload.size());

        std::string line;
        line.reserve(logger.size() + level.size() + payload.size() + 8);
        line.append("[");
        line.append(logger);
        line.append("] [");
        line.append(level);
        line.append("] ");
        line.append(payload);
        PushStartupLog(target, line);
    }

    void SetStartupState(engine_handle_s *impl, uint32_t state) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        impl->startup.state = state;
    }

    uint32_t GetStartupState(engine_handle_s *impl) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        return impl->startup.state;
    }

    void ResetStartupState(engine_handle_s *impl) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        impl->startup.state = ENGINE_STARTUP_STATE_IDLE;
        impl->startup.logs.clear();
    }

    void MarkStartupWorkerRunning(engine_handle_s *impl, bool running) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        impl->startup.worker_running = running;
    }

    std::thread DetachStartupWorker(engine_handle_s *impl) {
        std::lock_guard<std::mutex> guard(impl->startup.mutex);
        if(!impl->startup.worker.joinable()) {
            return std::thread();
        }
        return std::move(impl->startup.worker);
    }

    bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height) {
        if(g_engine_bootstrapped) {
            return true;
        }
        if(!TVPEngineBootstrap::Initialize(width, height)) {
            return false;
        }
        g_engine_bootstrapped = true;
        return true;
    }

    struct FrameReadbackLayout {
        int32_t read_x = 0;
        int32_t read_y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride_bytes = 0;
    };

    FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s *impl) {
        FrameReadbackLayout layout;
        layout.width = impl->frame.surface_width;
        layout.height = impl->frame.surface_height;

        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        if(glGetError() == GL_NO_ERROR && viewport[2] > 0 && viewport[3] > 0) {
            layout.read_x = viewport[0];
            layout.read_y = viewport[1];
            layout.width = static_cast<uint32_t>(viewport[2]);
            layout.height = static_cast<uint32_t>(viewport[3]);
        } else {
            // Fallback: use the EGL surface dimensions
            auto &egl = krkr::GetEngineEGLContext();
            if(egl.IsValid()) {
                const uint32_t egl_w = egl.GetWidth();
                const uint32_t egl_h = egl.GetHeight();
                if(egl_w > 0 && egl_h > 0) {
                    layout.width = egl_w;
                    layout.height = egl_h;
                }
            }
        }

        if(layout.width == 0) {
            layout.width = 1;
        }
        if(layout.height == 0) {
            layout.height = 1;
        }
        layout.stride_bytes = layout.width * 4u;
        return layout;
    }

    bool ReadCurrentFrameRgba(const FrameReadbackLayout &layout,
                              void *out_pixels) {
        if(layout.width == 0 || layout.height == 0 || out_pixels == nullptr) {
            return false;
        }

        glFinish();
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(static_cast<GLint>(layout.read_x),
                     static_cast<GLint>(layout.read_y),
                     static_cast<GLsizei>(layout.width),
                     static_cast<GLsizei>(layout.height), GL_RGBA,
                     GL_UNSIGNED_BYTE, out_pixels);
        const GLenum read_pixels_error = glGetError();

        if(read_pixels_error != GL_NO_ERROR) {
            return false;
        }

        auto *bytes = static_cast<uint8_t *>(out_pixels);
        const size_t row_bytes = static_cast<size_t>(layout.stride_bytes);
        std::vector<uint8_t> row_buffer(row_bytes);
        const uint32_t half_rows = layout.height / 2u;
        for(uint32_t y = 0; y < half_rows; ++y) {
            uint8_t *row_top = bytes + static_cast<size_t>(y) * row_bytes;
            uint8_t *row_bottom =
                bytes + static_cast<size_t>(layout.height - 1u - y) * row_bytes;
            std::memcpy(row_buffer.data(), row_top, row_bytes);
            std::memcpy(row_top, row_bottom, row_bytes);
            std::memcpy(row_bottom, row_buffer.data(), row_bytes);
        }

        return true;
    }

    bool IsFinitePointerValue(double value) { return std::isfinite(value); }

    engine_result_t DispatchInputEventNow(engine_handle_s *impl,
                                          const engine_input_event_t &event,
                                          const char **out_error_message) {
        auto *loop = EngineLoop::GetInstance();
        if(loop == nullptr) {
            if(out_error_message != nullptr) {
                *out_error_message = "engine loop is unavailable";
            }
            return ENGINE_RESULT_INVALID_STATE;
        }

        // Convert engine_input_event_t → EngineInputEvent (bridge → core)
        EngineInputEvent core_event;
        core_event.type = event.type;
        core_event.x = event.x;
        core_event.y = event.y;
        core_event.delta_x = event.delta_x;
        core_event.delta_y = event.delta_y;
        core_event.pointer_id = event.pointer_id;
        core_event.button = event.button;
        core_event.key_code = event.key_code;
        core_event.modifiers = event.modifiers;
        core_event.unicode_codepoint = event.unicode_codepoint;

        if(!loop->HandleInputEvent(core_event)) {
            if(out_error_message != nullptr) {
                *out_error_message =
                    "input event dispatch failed (no active window?)";
            }
            return ENGINE_RESULT_INVALID_STATE;
        }

        if(out_error_message != nullptr) {
            *out_error_message = nullptr;
        }
        return ENGINE_RESULT_OK;
    }

    // ── 宿主输入的跨线程入口队列 ──────────────────────────────────────────
    // **绝不碰 impl->mutex / g_registry_mutex**：engine_tick 把这两个锁按整帧持有，
    // 模态对话框（HostWindowLayer::ShowWindowAsModal 的嵌套循环）期间可能持续几十
    // 秒。宿主 UI 线程若在这里等锁，就是真机上的"弹窗点不动 + 卡死 + ANR"
    // （app.log 实证：上次未正常退出 kind=ANR，模态循环 13:48:31 进入、13:48:38 被
    // ANR 干掉）。所以入队只走这把独立互斥锁，派发一律在引擎线程做。
    std::mutex g_input_queue_mutex;
    std::deque<engine_input_event_t> g_input_queue;
    constexpr size_t kMaxQueuedInputs = 512;
    /// 当前可用作输入目标的句柄（原子，供跨线程比对；只为拒绝明显非法的句柄）。
    std::atomic<engine_handle_t> g_input_target_handle{ nullptr };

    void EnqueueInputEvent(const engine_input_event_t &event) {
        std::lock_guard<std::mutex> lk(g_input_queue_mutex);
        g_input_queue.push_back(event);
        if(g_input_queue.size() > kMaxQueuedInputs)
            g_input_queue.pop_front();
    }

    std::deque<engine_input_event_t> TakeQueuedInputEvents() {
        std::deque<engine_input_event_t> out;
        std::lock_guard<std::mutex> lk(g_input_queue_mutex);
        out.swap(g_input_queue);
        return out;
    }

    void DropQueuedInputEvents() { TakeQueuedInputEvents(); }

    /// 引擎线程侧派发（tick 开头、或模态循环里的泵）。返回失败时把原因写进
    /// out_error_message（可为空）。
    engine_result_t DrainQueuedInputEvents(const char **out_error_message) {
        std::deque<engine_input_event_t> queued = TakeQueuedInputEvents();
        for(const engine_input_event_t &queued_event : queued) {
            const char *dispatch_error = nullptr;
            const engine_result_t rc =
                DispatchInputEventNow(nullptr, queued_event, &dispatch_error);
            if(rc != ENGINE_RESULT_OK) {
                if(out_error_message != nullptr)
                    *out_error_message = dispatch_error;
                return rc;
            }
        }
        if(out_error_message != nullptr)
            *out_error_message = nullptr;
        return ENGINE_RESULT_OK;
    }

    engine_result_t OpenGameCore(engine_handle_t handle, engine_handle_s *impl,
                                 const char *game_root_path_utf8) {
        if(game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
            return ENGINE_RESULT_INVALID_ARGUMENT;
        }

        if(!EnsureEngineRuntimeInitialized(impl->frame.surface_width,
                                           impl->frame.surface_height)) {
            std::lock_guard<std::recursive_mutex> guard(impl->mutex);
            SetHandleErrorLocked(
                impl, "failed to initialize engine runtime for host mode");
            return ENGINE_RESULT_INTERNAL_ERROR;
        }

        EnsureRuntimeLoggersInitialized();
        EnsureInternalPluginAnchorsLinked();
        // Cache options set via engine_set_option() are already stored in
        // TVPEarlySetOptions and will be merged during TVPSystemInit().
        // Do NOT call TVPGetCommandLine() here — it triggers
        // TVPInitProgramArgumentsAndDataPath() which caches TVPGetAppPath()
        // as empty (TVPProjectDir is not set yet), corrupting path resolution.

        std::string normalized_game_root_path(game_root_path_utf8);
        // Only append trailing slash for directory paths.  Archive files
        // (.xp3, .zip, etc.) must keep their original extension so the
        // storage system can recognise them as archives.
        auto ends_with = [](const std::string &s, const std::string &suffix) {
            return s.size() >= suffix.size() &&
                s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        const bool looks_like_archive =
            ends_with(normalized_game_root_path, ".xp3") ||
            ends_with(normalized_game_root_path, ".XP3") ||
            ends_with(normalized_game_root_path, ".zip") ||
            ends_with(normalized_game_root_path, ".ZIP") ||
            ends_with(normalized_game_root_path, ".7z") ||
            ends_with(normalized_game_root_path, ".tar");
        if(!looks_like_archive && !normalized_game_root_path.empty() &&
           normalized_game_root_path.back() != '/' &&
           normalized_game_root_path.back() != '\\') {
            normalized_game_root_path.push_back('/');
        }

        spdlog::info("engine_open_game: runtime initialized, starting "
                     "application with path: {} (normalized: {})",
                     game_root_path_utf8, normalized_game_root_path);
        // 新一局开始：清空"只记一次/限频"日志门闩。这两张表是进程级的，
        // 不清的话换游戏后同类告警（如 PSB 解析失败）会被上一局的状态吞掉，
        // 诊断信息凭空消失。
        krkr::ResetLogGuards();
#if defined(__ANDROID__)
        AndroidInfoLog("engine_open_game: input='%s' normalized='%s'",
                       game_root_path_utf8, normalized_game_root_path.c_str());
#endif
        spdlog::default_logger()->flush();

        try {
            spdlog::debug(
                "engine_open_game: calling Application->StartApplication...");
#if defined(__ANDROID__)
            AndroidInfoLog("engine_open_game: calling StartApplication('%s')",
                           normalized_game_root_path.c_str());
#endif
            spdlog::default_logger()->flush();
            Application->StartApplication(
                ttstr(normalized_game_root_path.c_str()));
            spdlog::info(
                "engine_open_game: StartApplication returned successfully");
#if defined(__ANDROID__)
            AndroidInfoLog(
                "engine_open_game: StartApplication returned successfully");
#endif
        } catch(const std::exception &e) {
            spdlog::error(
                "engine_open_game: StartApplication threw std::exception: {}",
                e.what());
            std::lock_guard<std::recursive_mutex> guard(impl->mutex);
            SetHandleErrorLocked(impl, "StartApplication threw an exception");
            return ENGINE_RESULT_INTERNAL_ERROR;
        } catch(...) {
            spdlog::error(
                "engine_open_game: StartApplication threw unknown exception");
            std::lock_guard<std::recursive_mutex> guard(impl->mutex);
            SetHandleErrorLocked(impl, "StartApplication threw an exception");
            return ENGINE_RESULT_INTERNAL_ERROR;
        }

        if(TVPTerminated) {
            std::lock_guard<std::recursive_mutex> guard(impl->mutex);
            SetHandleErrorLocked(
                impl, "runtime requested termination during startup");
            return ENGINE_RESULT_INVALID_STATE;
        }

        EngineLoop::CreateInstance();
        if(auto *loop = EngineLoop::GetInstance(); loop != nullptr) {
            loop->Start();
        }

        if(auto *scene = TVPMainScene::GetInstance(); scene != nullptr) {
            scene->scheduleUpdate();
        }

        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        if(!IsHandleLiveLocked(handle)) {
            return ENGINE_RESULT_INVALID_STATE;
        }

        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        if(impl->state == ToStateValue(EngineState::kDestroyed)) {
            return ENGINE_RESULT_INVALID_STATE;
        }

        if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
        }
        g_runtime_active = true;
        g_runtime_owner = handle;
        g_runtime_started_once = true;
        // 跨线程输入的句柄闸门（只做原子比对，绝不取锁，见 g_input_queue_mutex）。
        g_input_target_handle.store(handle, std::memory_order_release);
        DropQueuedInputEvents(); // 上一局的残留输入不能带进这一局

        impl->runtime_owner = true;
        impl->input.native_mouse_callbacks_disabled = true;
        impl->frame.width = 0;
        impl->frame.height = 0;
        impl->frame.stride_bytes = 0;
        impl->frame.rgba.clear();
        impl->frame.ready = false;
        impl->input.active_pointer_ids.clear();
        DropQueuedInputEvents();
        impl->state = ToStateValue(EngineState::kOpened);
        ClearHandleErrorLocked(impl);
        return ENGINE_RESULT_OK;
    }

    void RunOpenGameAsync(engine_handle_t handle, engine_handle_s *impl,
                          std::string game_root_path_utf8) {
        // The worker owns startup only until it releases the EGL context; the
        // owner thread must make the context current again before the next
        // engine_tick. worker 只负责启动阶段；释放 EGL context 后由 owner
        // 线程在下一次 engine_tick 前重新
        // MakeCurrent，避免跨线程继续使用图形上下文。
        PushStartupLog(impl, "engine_open_game_async: worker started");
        TVPTerminated = false;
        TVPTerminateCode = 0;
        TVPSystemUninitCalled = false;
        TVPTerminateOnWindowClose = false;
        TVPTerminateOnNoWindowStartup = false;
        TVPHostSuppressProcessExit = true;

        const engine_result_t open_result =
            OpenGameCore(handle, impl, game_root_path_utf8.c_str());

        // Startup runs on a worker thread; release current GL context here so
        // the owner thread can safely make it current before ticking/rendering.
        auto &egl = krkr::GetEngineEGLContext();
        if(egl.IsValid()) {
            egl.ReleaseCurrent();
        }

        if(open_result == ENGINE_RESULT_OK) {
            PushStartupLog(impl, "engine_open_game => OK");
            SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
        } else {
            std::string error_text;
            {
                std::lock_guard<std::recursive_mutex> guard(impl->mutex);
                error_text = impl->last_error;
            }
            if(error_text.empty()) {
                error_text = "unknown startup error";
            }
            PushStartupLog(impl, "ERROR: " + error_text);
            SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
            std::lock_guard<std::recursive_mutex> registry_guard(
                g_registry_mutex);
            if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
                g_runtime_startup_active = false;
                g_runtime_startup_owner = nullptr;
            }
        }
        MarkStartupWorkerRunning(impl, false);
    }

} // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t *out_api_version) {
    if(out_api_version == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_api_version is null");
    }
    *out_api_version = ENGINE_API_VERSION;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t *desc,
                              engine_handle_t *out_handle) {
    if(desc == nullptr || out_handle == nullptr) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_create requires non-null desc and out_handle");
    }

    if(desc->struct_size < sizeof(engine_create_desc_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_create_desc_t.struct_size is too small");
    }

    const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
    const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
    if(caller_major != expected_major) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                       "unsupported engine API major version");
    }

    EnsureRuntimeLoggersInitialized();
    EnsureInternalPluginAnchorsLinked();
    TVPHostSuppressProcessExit = true;
    // 宿主模式下"游戏关窗"不再直接终止：挂起后由壳弹确认框，再用
    // engine_resolve_window_close() 答复（见 krkr::host 的说明）。
    krkr::host::SetDeferWindowClose(true);
    // 模态对话框期间 core 的嵌套循环会占住 tick 线程，宿主改从 UI 线程投递输入；
    // core 每帧回调这里派发（见 PumpModalInputOnTickThread 的说明）。
    krkr::host::SetModalInputPump(&PumpModalInputOnTickThread);

    auto *impl = new(std::nothrow) engine_handle_s();
    if(impl == nullptr) {
        *out_handle = nullptr;
        return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                       "failed to allocate engine handle");
    }

    impl->state = ToStateValue(EngineState::kCreated);
    impl->owner_thread = std::this_thread::get_id();
    impl->runtime_owner = false;

    // TVPMainThreadID 在 Application.cpp 里以 dlopen 时的线程做初值——在本移植中
    // 那是宿主 UI 线程，而引擎实际跑在调用 engine_create 的线程（渲染线程）上，
    // 两者不同。movie 子系统的 IsCurrentThread() 与 OOM 回调都拿它做判断，留在
    // 错值上会让这些分支永远走错边。engine_create 的调用线程正是后续
    // engine_tick 所在的线程（ValidateHandleThreadLocked
    // 强制），在这里改写即为正确值。
    TVPMainThreadID = impl->owner_thread;

    auto handle = reinterpret_cast<engine_handle_t>(impl);
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        g_live_handles.insert(handle);
    }

    *out_handle = handle;
    SetThreadError(nullptr);
    // 卡死探针：看门狗线程只在"渲染线程 ≥4s 没有推进"时打一条 warn（状态边沿），
    // 用于定位那些"停住且不再产生任何日志"的问题（真机上出过两次 ANR）。
    krkr::stall::MarkStage("engine_create 完成");
    krkr::stall::Start();
    return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
    if(handle == nullptr) {
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    engine_handle_s *impl = nullptr;
    bool owned_runtime = false;
    std::thread startup_worker;

    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto result = ValidateHandleLocked(handle, &impl);
        if(result != ENGINE_RESULT_OK) {
            return result;
        }

        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        result = ValidateHandleThreadLocked(impl);
        if(result != ENGINE_RESULT_OK) {
            return result;
        }

        owned_runtime = (g_runtime_active && g_runtime_owner == handle);
        if(owned_runtime) {
            g_runtime_active = false;
            g_runtime_owner = nullptr;
            impl->runtime_owner = false;
        }
        // 销毁后宿主再投递输入会被句柄闸门挡掉（也不会再排进队列里）。
        engine_handle_t expected_input_handle = handle;
        g_input_target_handle.compare_exchange_strong(
            expected_input_handle, nullptr, std::memory_order_acq_rel);
        DropQueuedInputEvents();
        if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
        }
        startup_worker = DetachStartupWorker(impl);
        SetStartupState(impl, ENGINE_STARTUP_STATE_IDLE);

        impl->state = ToStateValue(EngineState::kDestroyed);
        ClearHandleErrorLocked(impl);
        g_live_handles.erase(handle);
    }

    // Remove the handle from the live registry before joining. The worker can
    // finish without publishing logs into a handle that is being destroyed.
    // join 前先从 live registry 摘除 handle，避免销毁过程继续接收启动日志。
    if(startup_worker.joinable()) {
        startup_worker.join();
    }

    if(owned_runtime) {
        spdlog::info(
            "engine_destroy: entering runtime teardown (owned_runtime=1)");
        try {
            Application->OnDeactivate();
            spdlog::info("engine_destroy: Application::OnDeactivate done");
        } catch(...) {
            spdlog::error("engine_destroy: Application::OnDeactivate threw");
        }
        Application->FilterUserMessage(
            [](std::vector<std::tuple<void *, int, tTVPApplication::tMsg>>
                   &queue) { queue.clear(); });
        spdlog::info("engine_destroy: FilterUserMessage done");

        // Avoid triggering platform exit() path in the host process.
        TVPTerminated = false;
        TVPTerminateCode = 0;

        // Runtime teardown is ordered so script callbacks stop before the VM
        // and graphics resources are released. The reset chain makes the
        // process reusable for a later engine_open_game call.
        // 运行时销毁必须先停止脚本回调，再释放脚本 VM
        // 和图形资源；随后通过复位链 清理进程级状态，使同一进程可以再次调用
        // engine_open_game。

        // 1. Unregister internal plugins before destroying the script engine;
        // this prevents a later startup from appending duplicate registrations.
        // 1. 脚本引擎销毁前注销内部插件，避免下一次启动重复追加注册器。
        try {
            spdlog::info(
                "engine_destroy: TVPUnregisterInternalPluginsForRestart begin");
            TVPUnregisterInternalPluginsForRestart();
            spdlog::info(
                "engine_destroy: TVPUnregisterInternalPluginsForRestart end");
        } catch(...) {
            spdlog::error(
                "engine_destroy: TVPUnregisterInternalPluginsForRestart threw");
        }

        // 2. 安全卸载脚本引擎：OnExit → TVPUninitScriptEngine + delete
        // TVPSystemControl。
        try {
            spdlog::info("engine_destroy: Application->OnExit begin");
            Application->OnExit();
            spdlog::info("engine_destroy: Application->OnExit end");
        } catch(...) {
            spdlog::error("engine_destroy: Application->OnExit threw");
        }

        // 3. TVPSystemUninit → TVPUninitTVPGL + TVPCauseAtExit(at-exit
        // handlers)；
        //    其内部 TVPUninitScriptEngine 已被步骤 2 调过（守卫标志）→ no-op。
        try {
            spdlog::info("engine_destroy: TVPSystemUninit begin");
            TVPSystemUninit();
            spdlog::info("engine_destroy: TVPSystemUninit end");
        } catch(...) {
            spdlog::error("engine_destroy: TVPSystemUninit threw");
        }

        // 4. 销毁 EngineLoop / MainScene 单例。
        if(auto *scene = TVPMainScene::GetInstance()) {
            spdlog::info("engine_destroy: deleting TVPMainScene...");
            delete scene;
            spdlog::info("engine_destroy: TVPMainScene deleted");
        }
        if(auto *loop = EngineLoop::GetInstance()) {
            spdlog::info("engine_destroy: deleting EngineLoop...");
            delete loop;
            spdlog::info("engine_destroy: EngineLoop deleted");
        }

        // 5. Reset subsystem flags and caches after their owners are destroyed.
        // 5. 在所有者销毁后复位各子系统静态标志位与缓存，确保下次初始化干净。
        try {
            TVPResetRuntimeForRestart();
            TVPResetScriptEngineForRestart();
            TVPResetSysInitImplForRestart();
            TVPResetApplicationForRestart();
            TVPResetStorageImplForRestart();
            TVPResetRenderManagerForRestart();
            TVPResetWindowListForRestart();
            TVPResetLayerBitmapImplForRestart();
            TVPResetFontImplForRestart();
            TVPResetTransIntfForRestart();
            TVPResetDrawSceneOnceTimingForRestart();
            tTVPBitmapBitsAlloc::ResetForRestart();
            TVPResetExtensionClassInstallStateForRestart();
            TVPResetPluginSystemForRestart();
            spdlog::info("engine_destroy: reset-for-restart done");
        } catch(...) {
            spdlog::error("engine_destroy: reset-for-restart threw");
        }

        // 6. Shut down the bootstrap layer after subsystem reset.
        // 6. 子系统复位后关闭 EngineBootstrap。
        if(g_engine_bootstrapped) {
            spdlog::info("engine_destroy: TVPEngineBootstrap::Shutdown...");
            TVPEngineBootstrap::Shutdown();
            g_engine_bootstrapped = false;
            spdlog::info("engine_destroy: TVPEngineBootstrap::Shutdown done");
        }

        // 7. 允许下一次 engine_open_game 再次启动。
        g_runtime_started_once = false;

        spdlog::info("engine_destroy: runtime teardown complete (restartable)");
        spdlog::default_logger()->flush();
    }

    // 停掉卡死探针的看门狗线程。放在 registry 锁之外：看门狗写日志时可能要取
    // g_registry_mutex，持锁 join 会锁死自己。
    krkr::stall::Stop();
    delete impl;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char *game_root_path_utf8,
                                 const char *startup_script_utf8) {
    (void)startup_script_utf8;

    if(game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "game_root_path_utf8 is null or empty");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is already destroyed");
    }
    if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine startup is already running");
    }

    if(g_runtime_active) {
        if(g_runtime_owner != handle) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "runtime is already active on another engine handle");
        }

        impl->state = ToStateValue(EngineState::kOpened);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if(g_runtime_started_once) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_NOT_SUPPORTED,
            "runtime restart is not supported yet; restart process to open "
            "another game");
    }
    TVPTerminated = false;
    TVPTerminateCode = 0;
    TVPSystemUninitCalled = false;
    TVPTerminateOnWindowClose = false;
    TVPTerminateOnNoWindowStartup = false;
    TVPHostSuppressProcessExit = true;
    g_runtime_startup_active = true;
    g_runtime_startup_owner = handle;
    ResetStartupState(impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
    PushStartupLog(impl, "engine_open_game: starting");
    ClearHandleErrorLocked(impl);

    const engine_result_t open_result =
        OpenGameCore(handle, impl, game_root_path_utf8);
    if(open_result == ENGINE_RESULT_OK) {
        SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
        PushStartupLog(impl, "engine_open_game => OK");
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if(open_result == ENGINE_RESULT_INTERNAL_ERROR) {
        SetHandleErrorLocked(impl, "StartApplication threw an exception");
    } else if(open_result == ENGINE_RESULT_INVALID_STATE && TVPTerminated) {
        SetHandleErrorLocked(impl,
                             "runtime requested termination during startup");
    } else {
        SetHandleErrorLocked(impl, "engine_open_game failed");
    }
    g_runtime_startup_active = false;
    g_runtime_startup_owner = nullptr;
    SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
    PushStartupLog(impl, std::string("ERROR: ") + impl->last_error);
    return open_result;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char *game_root_path_utf8,
                                       const char *startup_script_utf8) {
    (void)startup_script_utf8;

    if(game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "game_root_path_utf8 is null or empty");
    }

    std::thread stale_worker;
    std::string game_root_copy(game_root_path_utf8);
    engine_handle_s *impl = nullptr;
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto result = ValidateHandleLocked(handle, &impl);
        if(result != ENGINE_RESULT_OK) {
            return result;
        }

        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        result = ValidateHandleThreadLocked(impl);
        if(result != ENGINE_RESULT_OK) {
            return result;
        }

        if(impl->state == ToStateValue(EngineState::kDestroyed)) {
            return SetHandleErrorAndReturnLocked(impl,
                                                 ENGINE_RESULT_INVALID_STATE,
                                                 "engine is already destroyed");
        }
        if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "engine startup is already running");
        }
        if(g_runtime_active && g_runtime_owner == handle) {
            SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
            ClearHandleErrorLocked(impl);
            SetThreadError(nullptr);
            return ENGINE_RESULT_OK;
        }
        if(g_runtime_active && g_runtime_owner != handle) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "runtime is already active on another engine handle");
        }
        if(g_runtime_started_once) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_NOT_SUPPORTED,
                "runtime restart is not supported yet; restart process to open "
                "another game");
        }

        stale_worker = DetachStartupWorker(impl);
        ResetStartupState(impl);
        SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
        PushStartupLog(impl, "engine_open_game_async: queued startup");
        MarkStartupWorkerRunning(impl, true);
        g_runtime_startup_active = true;
        g_runtime_startup_owner = handle;

        try {
            impl->startup.worker =
                std::thread([handle, impl, game_root_copy]() mutable {
                    RunOpenGameAsync(handle, impl, std::move(game_root_copy));
                });
        } catch(...) {
            MarkStartupWorkerRunning(impl, false);
            SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INTERNAL_ERROR,
                "failed to create startup thread");
        }

        ClearHandleErrorLocked(impl);
    }
    if(stale_worker.joinable()) {
        stale_worker.join();
    }
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t *out_state) {
    if(out_state == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_state is null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    *out_state = GetStartupState(impl);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char *out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t *out_bytes_written) {
    if(out_bytes_written == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_bytes_written is null");
    }
    if(out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "out_buffer is null or buffer_size is 0");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    uint32_t written = 0;
    {
        std::lock_guard<std::mutex> startup_guard(impl->startup.mutex);
        while(!impl->startup.logs.empty()) {
            const std::string line = impl->startup.logs.front();
            const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
            if(written + needed > buffer_size) {
                break;
            }
            std::memcpy(out_buffer + written, line.data(), line.size());
            written += static_cast<uint32_t>(line.size());
            out_buffer[written++] = '\n';
            impl->startup.logs.pop_front();
        }
    }
    if(written < buffer_size) {
        out_buffer[written] = '\0';
    } else {
        out_buffer[buffer_size - 1] = '\0';
    }
    *out_bytes_written = written;
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

// Attach the shared file sink to every engine logger if not already attached.
static void
AttachFileSinkToLoggers(const std::shared_ptr<spdlog::logger> &core,
                        const std::shared_ptr<spdlog::logger> &tjs2,
                        const std::shared_ptr<spdlog::logger> &plugin) {
    if(!g_file_sink) {
        return;
    }
    auto attach = [](const std::shared_ptr<spdlog::logger> &logger) {
        if(!logger) {
            return;
        }
        for(const auto &sink : logger->sinks()) {
            if(sink.get() == g_file_sink.get()) {
                return; // already attached
            }
        }
        logger->sinks().push_back(g_file_sink);
    };
    attach(core);
    attach(tjs2);
    attach(plugin);
    // The "core" logger is set as the default by
    // EnsureRuntimeLoggersInitialized; attach there too in case the default
    // points to a different logger.
    if(auto def = spdlog::default_logger(); def && def != core) {
        attach(def);
    }
}

// runtime-restart 会二次调用 engine_set_log_file_path：spdlog logger
// 是进程级全局 单例、不在 engine teardown 销毁，若不清旧 sink 就 push 新
// sink，会得到旧+新两个 文件 sink → 重启后每行日志写两遍（真机日志
// duplicated，见 2026-09-08 复现）。 重新设路径前用 old_sink 把旧 sink 从各
// logger 移除。
static void DetachFileSinkFromLoggers(
    const std::shared_ptr<spdlog::logger> &core,
    const std::shared_ptr<spdlog::logger> &tjs2,
    const std::shared_ptr<spdlog::logger> &plugin,
    const std::shared_ptr<spdlog::sinks::sink> &old_sink) {
    if(!old_sink) {
        return;
    }
    auto detach = [&](const std::shared_ptr<spdlog::logger> &logger) {
        if(!logger) {
            return;
        }
        auto &sinks = logger->sinks();
        for(auto it = sinks.begin(); it != sinks.end();) {
            if(it->get() == old_sink.get()) {
                it = sinks.erase(it);
            } else {
                ++it;
            }
        }
    };
    detach(core);
    detach(tjs2);
    detach(plugin);
    if(auto def = spdlog::default_logger(); def && def != core) {
        detach(def);
    }
}

engine_result_t engine_set_log_file_path(const char *path) {
    // ⚠️ 本函数是 JNI 入口，**绝不能把 C++ 异常放出去**：spdlog 建文件 sink 会抛
    // spdlog_ex（目录不存在/不可写等），异常穿过 JNI 边界会直接 std::terminate
    // 掉整个进程。真机实测：重装后 logs/engine.log 建不出来，每次启动都在
    // Application.onCreate 里 abort（128 次"无限重启"），界面根本进不去。
    // 日志落盘失败必须可降级 —— 下面整个函数体自己兜住异常。
    try {
        if(path == nullptr || path[0] == '\0') {
            std::lock_guard<std::mutex> lock(g_logfile_mutex);
            g_log_file_path.clear();
            // 清库：通知各 logger 移除旧文件 sink，避免残留
            DetachFileSinkFromLoggers(EnsureNamedLogger("core"),
                                      EnsureNamedLogger("tjs2"),
                                      EnsureNamedLogger("plugin"), g_file_sink);
            g_file_sink.reset();
            return ENGINE_RESULT_OK;
        }

        // Ensure the default loggers exist, then (re)create the rotating file
        // sink.
        EnsureRuntimeLoggersInitialized();

        std::lock_guard<std::mutex> lock(g_logfile_mutex);
        auto old_sink = g_file_sink;
        // 先造 sink 再改全局状态：构造抛异常时 g_file_sink / g_log_file_path
        // 都保持原样，旧 sink（若存在）继续可用 —— 这是最安全的降级。
        auto new_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path, 4u * 1024u * 1024u, 3);
        g_file_sink = new_sink;
        g_log_file_path = path;
        // runtime-restart 二次调用时先移除旧 sink，再挂新 sink，防日志每行重复
        DetachFileSinkFromLoggers(EnsureNamedLogger("core"),
                                  EnsureNamedLogger("tjs2"),
                                  EnsureNamedLogger("plugin"), old_sink);
        AttachFileSinkToLoggers(EnsureNamedLogger("core"),
                                EnsureNamedLogger("tjs2"),
                                EnsureNamedLogger("plugin"));
        spdlog::info("engine_set_log_file_path: engine log -> {}", path);
        // 卡死转储路径（<path>.stall）：日志锁被卡住时也能留下阶段信息。
        krkr::stall::SetDumpPath(path);
        spdlog::default_logger()->flush();
        return ENGINE_RESULT_OK;
    } catch(const std::exception &e) {
        // catch 里只做不会抛的事（fprintf/返回码）：再抛一次就是 terminate。
        std::fprintf(stderr,
                     "[engine_api] engine_set_log_file_path('%s') failed: %s "
                     "-- continuing without a file log sink\n",
                     path ? path : "(null)", e.what());
        return ENGINE_RESULT_IO_ERROR;
    } catch(...) {
        std::fprintf(stderr,
                     "[engine_api] engine_set_log_file_path failed: unknown "
                     "exception -- continuing without a file log sink\n");
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
}

engine_result_t engine_get_compat_profile(char *out_buffer,
                                          uint32_t buffer_size) {
    if(out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';
    std::lock_guard<std::mutex> lock(g_compat_mutex);
    if(g_compat_resolved_name.empty()) {
        return ENGINE_RESULT_OK; // 还没解析过：空串表示"未定档"
    }
    std::string text = g_compat_resolved_name;
    if(!g_compat_resolved_mode.empty()) {
        text += ' ';
        text += g_compat_resolved_mode;
    }
    if(text.size() >= buffer_size) {
        text.resize(buffer_size - 1);
    }
    std::memcpy(out_buffer, text.c_str(), text.size() + 1);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
    // A tick consumes input, restores the owner-thread EGL context, runs the
    // engine, then publishes either a native-surface frame or RGBA readback.
    // delta_ms is retained for ABI compatibility; the current loop uses its own
    // clock. 每次 tick 依次处理输入、恢复 owner 线程 EGL
    // context、运行引擎，并发布 native surface 帧或 RGBA 回读；delta_ms
    // 暂保留兼容性，当前循环使用内部时钟。
    (void)delta_ms;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    // 卡死探针：先落一个"本帧已开始"的阶段。放在所有状态门之前 —— 启动期
    // （engine startup is still running）与未打开状态的 tick 也会走到这里，
    // 否则启动/暂停那些"合法地不渲染"的阶段会被看门狗误报成卡死。
    krkr::stall::MarkStage("engine_tick: 开始");

    // 终止检查必须放在所有状态门之前：游戏可能在**启动期**就退出（启动脚本里的
    // `System.exit()`），此时 state 不是 kOpened、g_runtime_active 也可能已复位，
    // 若先撞上下面那些门就会返回 INVALID_STATE，宿主又把它当普通错误累计
    // —— 正是"点了退出游戏卡住 + 叠加层错误数每帧 +1"的另一条来路。
    // 只要运行期已终止，一律回答"游戏自己退了"，让宿主去收尾。
    if(TVPTerminated) {
        // 关窗退出与脚本退出分开报：前者窗口已没，宿主不能再提供"继续游戏"。
        if(TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl,
                                             ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime has been terminated");
    }

    // "游戏请求关窗"挂起：引擎没终止，窗口也没拆（见 krkr::host 的说明）。
    // 这一帧停在这里不跑，等宿主的确认框答复 —— 模态框压住游戏是想要的效果；
    // 用户选"继续"后 engine_resolve_window_close(allow=0) 清掉请求，下一帧照常跑。
    // 同样**不是错误**（壳不计数），否则叠加层的错误数又会每帧 +1。
    if(krkr::host::WindowClosePending()) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_WINDOW_CLOSE_REQUESTED;
    }

    if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
        // 启动期（StartApplication 还在 worker 线程里跑）**不是错误**：壳每帧都会
        // 调 tick，真机上每次开游戏都会因此记下 300+ 次"失败"
        // （err=engine startup is still running，见 app.log 的 x301/x304/x373），
        // 叠加层的错误数就是这么涨起来的。给它一个独立的结果码，让壳不当错误。
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_STARTUP_PENDING,
                                             "engine startup is still running");
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_tick");
    }

    if(impl->state == ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is paused");
    }

    if(impl->state != ToStateValue(EngineState::kOpened)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is not in opened state");
    }
    impl->tick_count += 1;
    krkr::stall::MarkStage("engine_tick: 输入派发");

    // 派发宿主投递的输入（跨线程队列，见 DrainQueuedInputEvents 的说明）。
    {
        const char *dispatch_error = nullptr;
        const engine_result_t dispatch_result =
            DrainQueuedInputEvents(&dispatch_error);
        if(dispatch_result != ENGINE_RESULT_OK) {
            return SetHandleErrorAndReturnLocked(impl, dispatch_result,
                                                 dispatch_error != nullptr
                                                     ? dispatch_error
                                                     : "input dispatch failed");
        }
    }

#if defined(__ANDROID__)
    // Auto-attach pending ANativeWindow from JNI bridge.
    // The Kotlin plugin calls nativeSetSurface() which stores the
    // ANativeWindow in a global variable. Here we detect it and
    // attach it as the EGL WindowSurface render target so that
    // eglSwapBuffers delivers frames to the host's SurfaceTexture.
    if(!impl->render.native_window_attached) {
        ANativeWindow *pending_window = krkr_GetNativeWindow();
        if(pending_window) {
            uint32_t win_w = 0, win_h = 0;
            krkr_GetSurfaceDimensions(&win_w, &win_h);
            auto &egl = krkr::GetEngineEGLContext();
            if(win_w > 0 && win_h > 0) {
                bool attached = false;
                if(!egl.IsValid()) {
                    // EGL context not yet initialized — use
                    // InitializeWithWindow to create EGL display + context +
                    // WindowSurface in one step, bypassing Pbuffer which may
                    // not be supported on this device.
                    AndroidInfoLog("engine_tick: EGL not valid, "
                                   "InitializeWithWindow %ux%u",
                                   win_w, win_h);
                    if(egl.InitializeWithWindow(pending_window, win_w, win_h)) {
                        attached = true;
                        AndroidInfoLog(
                            "engine_tick: InitializeWithWindow success");
                    } else {
                        AndroidInfoLog(
                            "engine_tick: InitializeWithWindow failed");
                    }
                } else {
                    // EGL already initialized (Pbuffer) — attach WindowSurface
                    AndroidInfoLog(
                        "engine_tick: EGL valid, AttachNativeWindow %ux%u",
                        win_w, win_h);
                    if(egl.AttachNativeWindow(pending_window, win_w, win_h)) {
                        attached = true;
                        AndroidInfoLog(
                            "engine_tick: AttachNativeWindow success");
                    } else {
                        AndroidInfoLog(
                            "engine_tick: AttachNativeWindow failed");
                    }
                }
                if(attached) {
                    impl->render.native_window_attached = true;
                    spdlog::info(
                        "engine_tick: auto-attached ANativeWindow {}x{}", win_w,
                        win_h);
                    AndroidInfoLog(
                        "engine_tick: auto-attached ANativeWindow %ux%u", win_w,
                        win_h);
                    // Update window size for the draw device
                    if(TVPMainWindow) {
                        auto *dd = TVPMainWindow->GetDrawDevice();
                        if(dd) {
                            dd->SetWindowSize(static_cast<tjs_int>(win_w),
                                              static_cast<tjs_int>(win_h));
                        }
                    }
                }
            } else if(impl->tick_count % 120 == 0) {
                AndroidInfoLog(
                    "engine_tick: pending ANativeWindow but size is 0 (%ux%u)",
                    win_w, win_h);
            }
            // Release the ref acquired by krkr_GetNativeWindow()
            ANativeWindow_release(pending_window);
        } else if(impl->tick_count % 180 == 0) {
            AndroidInfoLog("engine_tick: waiting for ANativeWindow (tick=%llu)",
                           static_cast<unsigned long long>(impl->tick_count));
        }
    } else {
        // Already attached — check if the JNI side has detached the window.
        ANativeWindow *current_window = krkr_GetNativeWindow();
        if(current_window) {
            ANativeWindow_release(current_window);
        } else {
            // Window was detached from JNI side — revert to Pbuffer
            auto &egl = krkr::GetEngineEGLContext();
            egl.DetachNativeWindow();
            impl->render.native_window_attached = false;
            spdlog::info("engine_tick: ANativeWindow detached, reverted to "
                         "Pbuffer mode");
            AndroidInfoLog("engine_tick: ANativeWindow detached -> Pbuffer");
        }
    }
#endif

    if(TVPTerminated) {
        // 本帧的输入派发里脚本调了 System.exit()（顶部那次检查在本帧之前跑过），
        // 或者本帧里游戏关掉了自己的窗口。两者都不是 tick 失败，但宿主处理不同
        // （关窗的那条不能"继续游戏"）。
        if(TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl,
                                             ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime has been terminated");
    }

    // Frame rate limiting: when fps_limit > 0, skip rendering if not enough
    // time has elapsed since the last rendered frame. Input events above are
    // always processed regardless of the limit.
    if(impl->fps.limit > 0) {
        const auto now = std::chrono::steady_clock::now();
        if(impl->fps.initialized) {
            const auto elapsed_us =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - impl->fps.last_render_time)
                    .count();
            if(static_cast<uint64_t>(elapsed_us) < impl->fps.interval_us) {
                // Not yet time for next frame — skip rendering
                impl->frame.rendered_this_tick = false;
                ClearHandleErrorLocked(impl);
                SetThreadError(nullptr);
                return ENGINE_RESULT_OK;
            }
            // Advance the deadline by exactly one frame interval instead of
            // snapping to `now`. This eliminates the cumulative drift that
            // occurs when vsync intervals don't evenly divide the target
            // frame interval (e.g. 60 Hz vsync vs 30 fps target: 16.6 ms
            // does not divide 33.3 ms evenly, causing every other frame to
            // wait an extra vsync and dropping to ~20-24 fps).
            //
            // If we've fallen behind by more than one full interval (e.g.
            // the app was suspended), snap to `now` to avoid a burst of
            // catch-up renders.
            const auto ideal_next = impl->fps.last_render_time +
                std::chrono::microseconds(impl->fps.interval_us);
            if(now - ideal_next >
               std::chrono::microseconds(impl->fps.interval_us)) {
                // Fallen too far behind — reset to now
                impl->fps.last_render_time = now;
            } else {
                impl->fps.last_render_time = ideal_next;
            }
        } else {
            impl->fps.last_render_time = now;
            impl->fps.initialized = true;
        }
    }

    // Async startup may have made the EGL context current on a worker thread.
    // Ensure the owner/tick thread has a current context before any GL work.
    {
        auto &egl = krkr::GetEngineEGLContext();
        if(egl.IsValid() && !egl.MakeCurrent()) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "failed to make EGL context current before engine_tick");
        }
    }

    // Drive one full frame (scene update + render + swap). In host mode
    // we must call Application->Run() which processes messages, triggers
    // scene composition, and invokes BasicDrawDevice::Show() →
    // form->UpdateDrawBuffer() — the actual rendering path.
    // TVPDrawSceneOnce() only restores GL state and calls SwapBuffer,
    // which is insufficient.
    if(::Application) {
        // ── 帧驱动探针（仅 KRKR_RENDER_PROBE）：区分二次打开"无帧"是 host
        // 不再调 engine_tick，还是 engine_tick 被调了但 Application->Run()
        // 没触发合成/blit。 若能看到 enter/return 却没有 UpdateDrawBuffer ->
        // Run() 走不下去（主窗口/绘制未恢复）； 若连 enter 都没有 -> host
        // 停止调用 engine_tick。
#if defined(KRKR_RENDER_PROBE)
        if(impl->tick_count % 15 == 0) {
            spdlog::info("engine_tick: tick={} Application::Run enter",
                         impl->tick_count);
            spdlog::default_logger()->flush();
        }
#endif
        // 引擎侧每帧耗时统计。壳侧的 "perf:" 只能看到 engine_tick 的总耗时，
        // 分不出"引擎内部更新慢"还是"宿主/post 慢"；这里把一帧拆成
        // update（Application::Run：脚本+场景合成+绘制）与 post
        // （TVPDrawSceneOnce + 纹理回收 + 帧交付）两段，按固定间隔汇总一行。
        // 与壳侧的 5s 采样对齐，便于两份日志并排看。
        const auto update_start = std::chrono::steady_clock::now();
        // ── overlay 电影必须**每帧刷新主窗口** ──────────────────────────────
        // 视频叠画挂在宿主窗口层的 PostBlit 里（HostWindowLayer::UpdateDrawBuffer
        // → DrawVideoOverlay），只有窗口重绘时才会执行。而 KAG 播片时脚本正阻塞在
        // 等片源上、窗口自己不会再请求重绘 —— 真机 2026-09-18 15:23:06 的探针实证：
        // 视频开始后 DrawVideoOverlay 一共只被调用了 3 次（约 40ms）就再没有过，
        // 画面停在最后一帧（用户看到"视频不播放"）。
        // 所以只要还有待呈现的 overlay 帧，就主动给主窗口排一次重绘。
        if(TVPMainWindow && TVPHostVideoOverlayFrameActive())
            TVPPostWindowUpdate(TVPMainWindow);
        krkr::stall::MarkStage("engine_tick: Application::Run（脚本+合成+绘制）");
        ::Application->Run();
        const auto update_end = std::chrono::steady_clock::now();
        FramePerfAccumulate(impl, update_start, update_end);
#if defined(KRKR_RENDER_PROBE)
        if(impl->tick_count % 15 == 0) {
            spdlog::info("engine_tick: tick={} Application::Run return",
                         impl->tick_count);
            spdlog::default_logger()->flush();
        }
#endif
    }
    // post 段：呈现 + 延迟纹理回收。与 update 段分开计时，才能区分
    // "场景/脚本慢" 与 "呈现/交付慢"。
    const auto post_start = std::chrono::steady_clock::now();
    krkr::stall::MarkStage("engine_tick: 呈现（DrawSceneOnce/回收/交付）");
    ::TVPDrawSceneOnce(0);

    // Process deferred texture deletions. iTVPTexture2D::Release() uses
    // delayed deletion — textures are queued in _toDeleteTextures and only
    // freed when RecycleProcess() is called. Without this, every texture
    // released during the frame (via Independ/SetSize/Recreate) accumulates
    // indefinitely, causing a memory leak — especially visible in OpenGL
    // mode where each texture also holds GPU resources.
    iTVPTexture2D::RecycleProcess();
    FramePerfAccumulatePost(impl, post_start,
                            std::chrono::steady_clock::now());

    if(TVPTerminated) {
        // 本帧的 Application::Run() 里脚本要求退出（EAbort 退栈后置位）——**游戏内
        // 菜单的"退出游戏"就是这条**（KAG 的 @close → Window.close →
        // HostWindowLayer::Close → TVPTerminateAsync）。这一条以前漏了关窗判断，
        // 于是宿主收到 GAME_TERMINATED、弹了"是否继续"的确认框，用户点继续时
        // engine_cancel_termination 才发现"窗口已关、不能取消"而拒绝 —— 用户就
        // 被留在已经死掉的画面上（真机 01:59:13 完全吻合）。
        if(TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl,
                                             ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime requested termination");
    }

    // Mark that a frame was rendered this tick (for IOSurface mode
    // notification)
    impl->frame.rendered_this_tick = true;

    // In IOSurface mode, the engine renders directly to the shared IOSurface
    // via the FBO — no need for glReadPixels. Skip the expensive readback.
    if(impl->render.native_window_attached) {
        // Android WindowSurface mode — TVPForceSwapBuffer() (called by
        // TVPDrawSceneOnce above) already performed eglSwapBuffers to deliver
        // the frame to the host's SurfaceTexture. Just update frame tracking.
        impl->frame.serial += 1;
        impl->frame.ready = true;
    } else if(!impl->render.iosurface_attached) {
        // Legacy Pbuffer readback path (slow, for backward compatibility)
        const FrameReadbackLayout layout = GetFrameReadbackLayoutLocked(impl);
        const size_t required_size = static_cast<size_t>(layout.stride_bytes) *
            static_cast<size_t>(layout.height);
        if(impl->frame.rgba.size() != required_size) {
            impl->frame.rgba.assign(required_size, 0);
        }

        if(required_size > 0 &&
           ReadCurrentFrameRgba(layout, impl->frame.rgba.data())) {
            impl->frame.width = layout.width;
            impl->frame.height = layout.height;
            impl->frame.stride_bytes = layout.stride_bytes;
            impl->frame.ready = true;
            impl->frame.serial += 1;
        } else if(!impl->frame.ready && required_size > 0) {
            std::fill(impl->frame.rgba.begin(), impl->frame.rgba.end(), 0);
            impl->frame.width = layout.width;
            impl->frame.height = layout.height;
            impl->frame.stride_bytes = layout.stride_bytes;
            impl->frame.ready = true;
            impl->frame.serial += 1;
        }
    } else {
        // IOSurface mode — just increment frame serial, no readback needed.
        // The render output is already in the shared IOSurface.
        // 低频采样 IOSurface，区分黑屏两端归属：
        //   采样非黑 -> 引擎已把画面写进共享目标，黑屏在宿主读/显示侧；
        //   采样全黑 -> 引擎 blit 到 IOSurface 的链路本身有问题。
        // 频率提到每 5
        // tick：高密度帧间序列可区分「稳定黑屏」与「单帧瞬时闪黑」， 同时对照
        // UI_stubs 的 SourceSample(每5帧) 分辨源纹理 vs IOSurface 落地。
        // 该采样每次 tick 都 glReadPixels + 打一条 info，属诊断探针，仅在
        // 启用 KRKR_RENDER_PROBE 时才编译/运行。
#if defined(KRKR_RENDER_PROBE)
        if(impl->tick_count % 5 == 0) {
            auto &iosurf_egl = krkr::GetEngineEGLContext();
            if(iosurf_egl.HasIOSurface()) {
                const int sampW =
                    static_cast<int>(iosurf_egl.GetIOSurfaceWidth());
                const int sampH =
                    static_cast<int>(iosurf_egl.GetIOSurfaceHeight());
                if(sampW > 0 && sampH > 0) {
                    GLint prevFbo = 0;
                    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
                    iosurf_egl.BindRenderTarget();
                    glFinish(); // 确保先前的 blit 命令已完成，再读回 IOSurface
                    uint64_t sR = 0, sG = 0, sB = 0, sA = 0;
                    int nonBlack = 0;
                    unsigned char px[4];
                    for(int gy = 0; gy < 5; ++gy) {
                        for(int gx = 0; gx < 5; ++gx) {
                            const int x = static_cast<int>(
                                (static_cast<float>(gx) + 0.5f) *
                                static_cast<float>(sampW) / 5.0f);
                            const int y = static_cast<int>(
                                (static_cast<float>(gy) + 0.5f) *
                                static_cast<float>(sampH) / 5.0f);
                            px[0] = px[1] = px[2] = px[3] = 0;
                            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                         px);
                            sR += px[0];
                            sG += px[1];
                            sB += px[2];
                            sA += px[3];
                            if(px[0] > 8 || px[1] > 8 || px[2] > 8)
                                ++nonBlack;
                        }
                    }
                    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                    spdlog::info(
                        "IOSurfacePixelSample: tick={} size={}x{} "
                        "nonBlack={}/25 avg=({},{},{},{})",
                        static_cast<unsigned long long>(impl->tick_count),
                        sampW, sampH, nonBlack, static_cast<int>(sR / 25),
                        static_cast<int>(sG / 25), static_cast<int>(sB / 25),
                        static_cast<int>(sA / 25));
                }
            }
        }
#endif // KRKR_RENDER_PROBE
        glFlush(); // Ensure GPU commands are submitted
        impl->frame.serial += 1;
        impl->frame.ready = true;
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
#if defined(__ANDROID__)
    if(impl->tick_count % 120 == 0) {
        AndroidInfoLog("engine_tick: tick=%llu rendered=%d serial=%llu "
                       "native_window=%d iosurface=%d frame_ready=%d",
                       static_cast<unsigned long long>(impl->tick_count),
                       impl->frame.rendered_this_tick ? 1 : 0,
                       static_cast<unsigned long long>(impl->frame.serial),
                       impl->render.native_window_attached ? 1 : 0,
                       impl->render.iosurface_attached ? 1 : 0,
                       impl->frame.ready ? 1 : 0);
    }
#endif
    return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_pause");
    }

    if(impl->state == ToStateValue(EngineState::kPaused)) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if(impl->state != ToStateValue(EngineState::kOpened)) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_pause requires opened state");
    }

    Application->OnDeactivate();
    impl->input.active_pointer_ids.clear();
    DropQueuedInputEvents();
    impl->state = ToStateValue(EngineState::kPaused);
    // 主动暂停期间没有 tick 是正常的：别让卡死探针误报。
    krkr::stall::SetPaused(true);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_resume");
    }

    if(impl->state == ToStateValue(EngineState::kOpened)) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if(impl->state != ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_resume requires paused state");
    }

    Application->OnActivate();
    impl->state = ToStateValue(EngineState::kOpened);
    krkr::stall::SetPaused(false);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_cancel_termination(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    // 幂等：没有待处理的终止时什么都不做。
    if(!TVPTerminated) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    // 是"游戏关掉了自己的窗口"才终止的：窗口已经没了，撤销只会让引擎在空场景上
    // 继续跑（真机实测就是画面彻底不动）。诚实拒绝，宿主应当直接退出。
    if(TVPTerminateWindowClosed) {
        SetHandleErrorLocked(impl,
                             "cannot cancel: the game closed its window");
        return ENGINE_RESULT_INVALID_STATE;
    }

    // 终止挂起期间引擎并没有拆掉任何东西（宿主模式下 TVPExitApplication 不结束
    // 进程，engine_tick 一见到 TVPTerminated 就提前返回），所以"取消"只需清标志，
    // 下一帧 Run() 就会照常处理消息与绘制。
    TVPTerminated = false;
    TVPTerminateCode = 0;
    if(::Application)
        ::Application->Unterminate();
    krkr::stall::SetPaused(false);
    krkr::stall::MarkStage("engine_tick: 已取消退出，恢复运行");
    spdlog::info("engine_cancel_termination: 宿主选择继续游戏，已撤销终止标志");
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resolve_window_close(engine_handle_t handle,
                                            int32_t allow_close) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(allow_close) {
        // 宿主确认退出：执行真正的关窗终止。此后 engine_tick 走既有的
        // TVPTerminated + TVPTerminateWindowClosed 分支报 WINDOW_CLOSED。
        krkr::host::ConfirmWindowClose();
        krkr::stall::SetPaused(false);
        spdlog::info("engine_resolve_window_close: 宿主确认退出，已执行关窗终止");
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    // 宿主选"继续游戏"。若已经真的终止了（例如兜底超时已自行关窗），没什么可
    // 撤销的：诚实拒绝，宿主应当直接离开游戏界面。
    if(TVPTerminated) {
        SetHandleErrorLocked(
            impl, "cannot keep playing: the runtime already terminated");
        return ENGINE_RESULT_INVALID_STATE;
    }

    krkr::host::CancelWindowClose();
    krkr::stall::SetPaused(false);
    krkr::stall::MarkStage("engine_tick: 已取消关窗请求，恢复运行");
    spdlog::info("engine_resolve_window_close: 宿主选择继续游戏，已取消关窗请求");
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t *option) {
    if(option == nullptr || option->key_utf8 == nullptr ||
       option->key_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "option and option->key_utf8 must be non-null/non-empty");
    }
    if(option->value_utf8 == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "option->value_utf8 must be non-null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    // Handle fps_limit option: controls C++ side frame rate throttling
    const std::string key(option->key_utf8);
    if(key == ENGINE_OPTION_FPS_LIMIT) {
        const int fps = std::atoi(option->value_utf8);
        impl->fps.limit = fps > 0 ? static_cast<uint32_t>(fps) : 0;
        impl->fps.interval_us =
            fps > 0 ? (1000000u / static_cast<uint32_t>(fps)) : 0;
        // Reset timing so the next tick renders immediately
        impl->fps.initialized = false;
        spdlog::info("engine_set_option: fps_limit={} (interval={}us)",
                     impl->fps.limit, impl->fps.interval_us);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    // angle_backend 选项：ANGLE 已移除，渲染走平台原生 EGL/GLES。
    // 保留为兼容空操作——旧设置文件里仍会有这个键，必须接受以免初始化失败。
    if(key == ENGINE_OPTION_ANGLE_BACKEND) {
        spdlog::info("engine_set_option: angle_backend='{}' ignored "
                     "(ANGLE removed; using native EGL)",
                     option->value_utf8);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    // 字体回退策略：两套字体解析实现都保留，由设置页在运行时选。
    // TVPSetCommandLine 已把值写进配置（下面的通用分支）；这里再做一件**立即**
    // 生效的事：写进程内的策略变量。字体初始化晚于选项下发，两者取哪个都行，
    // 但显式设一次更直观，也不依赖"配置在字体初始化前一定可读"这个隐含前提。
    if(key == ENGINE_OPTION_FONT_FALLBACK_MODE) {
        TVPSetFontFallbackModeFromString(option->value_utf8);
        spdlog::info("engine_set_option: font_fallback_mode={}",
                     option->value_utf8);
    }

    // 引擎侧帧耗时采样开关（默认开）。默认值见 engine_handle_s::perf_enabled：
    // 它只在 5s 边界拼一行日志，代价可忽略；留个关掉的开关是为了做 A/B 对照，
    // 确认采样本身没有引入可测量的开销。
    if(key == "frame_perf") {
        const std::string v(option->value_utf8);
        impl->perf_enabled = !(v == "off" || v == "0" || v == "false");
        spdlog::info("engine_set_option: frame_perf={}",
                     impl->perf_enabled ? "on" : "off");
    }

    // krkrz 的 OGLDrawDevice 兼容层：真正生效在 krkrgles 插件的 post-regist 里
    // （见 cpp/plugins/krkrgles.cpp）。这里只校验收到的取值并打一行日志；取值非法
    // 时按 `off` 处理而不报错——旧设置文件里带个没见过的值不该让开游戏失败。
    if(key == ENGINE_OPTION_GAME_COMPAT_PROFILE) {
        std::string requested(option->value_utf8);
        if(requested.empty())
            requested = ENGINE_GAME_COMPAT_PROFILE_AUTO;
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_compat_request = requested;
        }
        // 两个选项（profile / game_root）到齐的那一刻就解析完；
        // 插件是在 post-regist 读 ogldrawdevice_compat 的，不能再晚。
        ApplyCompatProfile();
    } else if(key == ENGINE_OPTION_GAME_COMPAT_GAME_ROOT) {
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_compat_game_root = option->value_utf8;
        }
        ApplyCompatProfile();
    } else if(key == ENGINE_OPTION_OGLDRAWDEVICE_COMPAT) {
        const std::string mode(option->value_utf8);
        const bool known = mode == ENGINE_OGLDRAWDEVICE_COMPAT_OFF ||
                           mode == ENGINE_OGLDRAWDEVICE_COMPAT_OGL ||
                           mode == ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS ||
                           mode == ENGINE_OGLDRAWDEVICE_COMPAT_KAG;
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_ogldrawdevice_explicit = true;
        }
        spdlog::info("engine_set_option: ogldrawdevice_compat={}{}", mode,
                     known ? "" : " (unknown, treated as off)");
    }

    TVPSetCommandLine(ttstr(option->key_utf8).c_str(),
                      ttstr(option->value_utf8));

    if(key == ENGINE_OPTION_ARCHIVE_CACHE_COUNT) {
        const int count = std::atoi(option->value_utf8);
        if(g_engine_bootstrapped && count > 0) {
            TVPSetArchiveCacheCount(static_cast<tjs_uint>(count));
        }
    } else if(key == ENGINE_OPTION_AUTOPATH_CACHE_COUNT) {
        const int count = std::atoi(option->value_utf8);
        if(g_engine_bootstrapped && count > 0) {
            TVPSetAutoPathCacheMaxCount(static_cast<tjs_uint>(count));
        }
    } else if(key == ENGINE_OPTION_PSB_CACHE_MB) {
        const int cache_mb = std::atoi(option->value_utf8);
        if(cache_mb > 0) {
            impl->memory_options.psb_cache_mb = cache_mb;
        }
        if(g_engine_bootstrapped &&
           impl->memory_options.psb_cache_entries > 0 &&
           impl->memory_options.psb_cache_mb > 0) {
            PSB::SetPSBMediaCacheBudget(
                static_cast<size_t>(impl->memory_options.psb_cache_entries),
                static_cast<size_t>(impl->memory_options.psb_cache_mb) *
                    1024ULL * 1024ULL);
        }
    } else if(key == ENGINE_OPTION_PSB_CACHE_ENTRIES) {
        const int entries = std::atoi(option->value_utf8);
        if(entries > 0) {
            impl->memory_options.psb_cache_entries = entries;
        }
        if(g_engine_bootstrapped &&
           impl->memory_options.psb_cache_entries > 0 &&
           impl->memory_options.psb_cache_mb > 0) {
            PSB::SetPSBMediaCacheBudget(
                static_cast<size_t>(impl->memory_options.psb_cache_entries),
                static_cast<size_t>(impl->memory_options.psb_cache_mb) *
                    1024ULL * 1024ULL);
        }
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle, uint32_t width,
                                        uint32_t height) {
    if(width == 0 || height == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "width and height must be > 0");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is already destroyed");
    }

    impl->frame.surface_width = width;
    impl->frame.surface_height = height;
    impl->frame.width = 0;
    impl->frame.height = 0;
    impl->frame.stride_bytes = 0;
    impl->frame.rgba.clear();
    impl->frame.ready = false;

    // Propagate the new surface size to the EGL context and viewport.
    if(g_runtime_active && g_runtime_owner == handle) {
        auto &egl = krkr::GetEngineEGLContext();
        if(egl.IsValid()) {
            if(egl.HasNativeWindow()) {
                // Android WindowSurface mode: setDefaultBufferSize() already
                // changed the SurfaceTexture buffer dimensions. The EGL surface
                // auto-adapts on next eglSwapBuffers. Update our stored
                // dimensions so UpdateDrawBuffer() uses the correct viewport.
                egl.UpdateNativeWindowSize(width, height);
            } else {
                // Pbuffer mode (macOS / iOS): resize the Pbuffer surface.
                const uint32_t cur_w = egl.GetWidth();
                const uint32_t cur_h = egl.GetHeight();
                if(cur_w != width || cur_h != height) {
                    egl.Resize(width, height);
                    glViewport(0, 0, static_cast<GLsizei>(width),
                               static_cast<GLsizei>(height));
                }
            }
        }

        // Only update WindowSize here — DestRect is exclusively managed by
        // UpdateDrawBuffer() which calculates the correct letterbox viewport.
        if(TVPMainWindow) {
            auto *dd = TVPMainWindow->GetDrawDevice();
            if(dd) {
                dd->SetWindowSize(static_cast<tjs_int>(width),
                                  static_cast<tjs_int>(height));
            }
        }
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t *out_frame_desc) {
    if(out_frame_desc == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_frame_desc is null");
    }
    if(out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_frame_desc_t.struct_size is too small");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is already destroyed");
    }

    FrameReadbackLayout layout;
    if(impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
       impl->frame.stride_bytes > 0) {
        layout.width = impl->frame.width;
        layout.height = impl->frame.height;
        layout.stride_bytes = impl->frame.stride_bytes;
    } else {
        layout = GetFrameReadbackLayoutLocked(impl);
    }

    std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
    out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
    out_frame_desc->width = layout.width;
    out_frame_desc->height = layout.height;
    out_frame_desc->stride_bytes = layout.stride_bytes;
    out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
    out_frame_desc->frame_serial = impl->frame.serial;

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle, void *out_pixels,
                                       size_t out_pixels_size) {
    if(out_pixels == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_pixels is null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(impl->state != ToStateValue(EngineState::kOpened) &&
       impl->state != ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_read_frame_rgba");
    }

    FrameReadbackLayout layout;
    if(impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
       impl->frame.stride_bytes > 0) {
        layout.width = impl->frame.width;
        layout.height = impl->frame.height;
        layout.stride_bytes = impl->frame.stride_bytes;
    } else {
        layout = GetFrameReadbackLayoutLocked(impl);
        const size_t required_size = static_cast<size_t>(layout.stride_bytes) *
            static_cast<size_t>(layout.height);
        impl->frame.rgba.assign(required_size, 0);
        impl->frame.width = layout.width;
        impl->frame.height = layout.height;
        impl->frame.stride_bytes = layout.stride_bytes;
        impl->frame.ready = true;
    }

    const size_t required_size = static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    if(out_pixels_size < required_size) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_ARGUMENT,
            "out_pixels_size is smaller than required frame buffer size");
    }

    if(impl->frame.rgba.size() < required_size) {
        impl->frame.rgba.resize(required_size, 0);
    }
    std::memcpy(out_pixels, impl->frame.rgba.data(), required_size);

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void **out_window_handle) {
    if(out_window_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_window_handle is null");
    }
    *out_window_handle = nullptr;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before "
            "engine_get_host_native_window");
    }

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    // No native GLFW window in headless Pbuffer mode.
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "engine_get_host_native_window is not supported in headless mode");
#else
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "engine_get_host_native_window is only supported on macOS runtime");
#endif
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void **out_view_handle) {
    if(out_view_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_view_handle is null");
    }
    *out_view_handle = nullptr;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_get_host_native_view");
    }

    // No native GLFW window in headless Pbuffer mode — native view is
    // unavailable.
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "engine_get_host_native_view is not supported in headless mode");
}

// 模态对话框（KAG 的 Window.showModal → HostWindowLayer::ShowWindowAsModal 的嵌套
// 循环）期间 tick 线程被 core 占用：壳的帧回调排不进来，输入只能从 UI 线程投递。
// core 每帧回调这里把跨线程输入队列派发掉（本线程就是引擎线程，派发是安全的）。
static void PumpModalInputOnTickThread() {
    const char *dispatch_error = nullptr;
    if(DrainQueuedInputEvents(&dispatch_error) != ENGINE_RESULT_OK) {
        // 模态期间派发失败不该打断对话框；记一条便于排查。
        spdlog::warn("PumpModalInput: 输入派发失败 err={}",
                     dispatch_error ? dispatch_error : "(unknown)");
    }
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t *event) {
    if(event == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "event is null");
    }
    if(event->struct_size < sizeof(engine_input_event_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_input_event_t.struct_size is too small");
    }

    // **不取 g_registry_mutex / impl->mutex**（整帧被 engine_tick 持有，模态对话框
    // 期间可能持续几十秒 —— 在那上面等锁就是宿主 UI 线程的 ANR）。句柄只用原子
    // 指针比对挡掉明显非法的调用，状态门（kOpened/kPaused）留给引擎线程在派发时
    // 判断。见 g_input_queue_mutex 的说明。
    if(handle == nullptr ||
       handle != g_input_target_handle.load(std::memory_order_acquire)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_STATE,
            "engine handle is invalid or not the active runtime");
    }

    switch(event->type) {
        case ENGINE_INPUT_EVENT_POINTER_DOWN:
        case ENGINE_INPUT_EVENT_POINTER_MOVE:
        case ENGINE_INPUT_EVENT_POINTER_UP:
        case ENGINE_INPUT_EVENT_POINTER_SCROLL:
        case ENGINE_INPUT_EVENT_KEY_DOWN:
        case ENGINE_INPUT_EVENT_KEY_UP:
        case ENGINE_INPUT_EVENT_TEXT_INPUT:
        case ENGINE_INPUT_EVENT_BACK:
            break;
        default:
            return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                           "unsupported input event type");
    }

    if(event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ||
       event->type == ENGINE_INPUT_EVENT_POINTER_MOVE ||
       event->type == ENGINE_INPUT_EVENT_POINTER_UP ||
       event->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) {
        if(!IsFinitePointerValue(event->x) || !IsFinitePointerValue(event->y) ||
           !IsFinitePointerValue(event->delta_x) ||
           !IsFinitePointerValue(event->delta_y)) {
            return SetThreadErrorAndReturn(
                ENGINE_RESULT_INVALID_ARGUMENT,
                "pointer coordinates contain non-finite values");
        }
    }

    EnqueueInputEvent(*event);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                   uint32_t iosurface_id,
                                                   uint32_t width,
                                                   uint32_t height) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before "
            "engine_set_render_target_iosurface");
    }

    // 本项目仅面向 Android：IOSurface 是 macOS 的零拷贝路径，已随 Apple
    // 平台一并移除。 Android 的等价能力走
    // engine_set_render_target_surface（ANativeWindow）。
    (void)iosurface_id;
    (void)width;
    (void)height;
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "IOSurface render target is not supported (Android uses "
        "ANativeWindow)");
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                 void *native_window,
                                                 uint32_t width,
                                                 uint32_t height) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before "
            "engine_set_render_target_surface");
    }

#if defined(__ANDROID__)
    auto &egl = krkr::GetEngineEGLContext();
    if(!egl.IsValid()) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "EGL context not initialized");
    }

    if(native_window == nullptr) {
        // Detach — revert to Pbuffer mode
        egl.DetachNativeWindow();
        impl->render.native_window_attached = false;
        spdlog::info(
            "engine_set_render_target_surface: detached, Pbuffer mode");
    } else {
        if(width == 0 || height == 0) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_ARGUMENT,
                "width and height must be > 0 when setting Surface");
        }
        if(!egl.AttachNativeWindow(native_window, width, height)) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INTERNAL_ERROR,
                "failed to attach Android Surface as render target");
        }
        impl->render.native_window_attached = true;
        spdlog::info("engine_set_render_target_surface: attached {}x{}", width,
                     height);

        // Update window size for the draw device
        if(TVPMainWindow) {
            auto *dd = TVPMainWindow->GetDrawDevice();
            if(dd) {
                dd->SetWindowSize(static_cast<tjs_int>(width),
                                  static_cast<tjs_int>(height));
            }
        }
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
#else
    (void)native_window;
    (void)width;
    (void)height;
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "Surface render target is only supported on Android");
#endif
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                               uint32_t *out_flag) {
    if(out_flag == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_flag is null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        *out_flag = 0;
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    *out_flag = impl->frame.rendered_this_tick ? 1 : 0;
    impl->frame.rendered_this_tick = false;

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char *out_buffer,
                                         uint32_t buffer_size) {
    if(out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    if(!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_get_renderer_info");
    }

    // 异步启动期间 EGL 上下文是 worker 线程 current 的：这里若 MakeCurrent 就会把
    // 上下文从 worker 手里抢走，破坏它的 GL 状态（真机 14:17 表现：KAG 档游戏卡在
    // "正在打开游戏"直到 ANR）。宁可返回"启动期不可用"，让调用方启动完成后再问。
    if(g_runtime_startup_active && g_runtime_startup_owner == handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_STARTUP_PENDING,
            "engine_get_renderer_info is unavailable while the async startup is "
            "still running (EGL context belongs to the startup thread)");
    }

    auto &egl = krkr::GetEngineEGLContext();
    if(!egl.IsValid()) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "EGL context not initialized");
    }
    if(!egl.MakeCurrent()) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "failed to make EGL context current");
    }

    // Query GL renderer and version strings from the active GL context.
    const char *gl_renderer =
        reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *gl_version =
        reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if(!gl_renderer)
        gl_renderer = "(unknown)";
    if(!gl_version)
        gl_version = "(unknown)";

    // Build a combined info string.
    std::string info =
        std::string(gl_renderer) + " | " + std::string(gl_version);
    std::strncpy(out_buffer, info.c_str(), buffer_size - 1);
    out_buffer[buffer_size - 1] = '\0';

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t *out_stats) {
    if(out_stats == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_stats is null");
    }
    if(out_stats->struct_size < sizeof(engine_memory_stats_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_memory_stats_t.struct_size is too small");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->struct_size = sizeof(engine_memory_stats_t);
    TVPMemoryInfo meminfo{};
    TVPGetMemoryInfo(meminfo);
    out_stats->self_used_mb =
        static_cast<uint32_t>(std::max<tjs_int>(0, TVPGetSelfUsedMemory()));
    out_stats->system_free_mb =
        static_cast<uint32_t>(std::max<tjs_int>(0, TVPGetSystemFreeMemory()));
    out_stats->system_total_mb = static_cast<uint32_t>(meminfo.MemTotal / 1024);

    out_stats->graphic_cache_bytes = TVPGetGraphicCacheTotalBytes();
    out_stats->graphic_cache_limit_bytes = TVPGetGraphicCacheLimit();
    out_stats->xp3_segment_cache_bytes = TVPGetXP3SegmentCacheTotalBytes();

    out_stats->archive_cache_entries = TVPGetArchiveCacheCount();
    out_stats->archive_cache_limit = TVPGetArchiveCacheLimit();
    out_stats->autopath_cache_entries = TVPGetAutoPathCacheCount();
    out_stats->autopath_cache_limit = TVPGetAutoPathCacheLimit();
    out_stats->autopath_table_entries = TVPGetAutoPathTableCount();

    PSB::PSBMediaCacheStats psb_stats{};
    if(PSB::GetPSBMediaCacheStats(psb_stats)) {
        out_stats->psb_cache_bytes = psb_stats.bytesInUse;
        out_stats->psb_cache_entries =
            static_cast<uint32_t>(psb_stats.entryCount);
        out_stats->psb_cache_entry_limit =
            static_cast<uint32_t>(psb_stats.entryLimit);
        out_stats->psb_cache_hits = psb_stats.hitCount;
        out_stats->psb_cache_misses = psb_stats.missCount;
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

const char *engine_get_last_error(engine_handle_t handle) {
    if(handle == nullptr) {
        return g_thread_error.c_str();
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if(!IsHandleLiveLocked(handle)) {
        SetThreadError("engine handle is invalid or already destroyed");
        return g_thread_error.c_str();
    }
    auto *impl = reinterpret_cast<engine_handle_s *>(handle);
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    return impl->last_error.c_str();
}

} // extern "C"

#else

#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>

struct engine_handle_s {
    std::recursive_mutex mutex;
    std::string last_error;
    int state = 0;
    uint32_t surface_width = 1280;
    uint32_t surface_height = 720;
    uint64_t frame_serial = 0;
    uint32_t startup_state = ENGINE_STARTUP_STATE_IDLE;
    std::deque<std::string> startup_logs;
};

namespace {

    enum class EngineState {
        kCreated = 0,
        kOpened,
        kPaused,
        kDestroyed,
    };

    inline int ToStateValue(EngineState state) {
        return static_cast<int>(state);
    }

    std::recursive_mutex g_registry_mutex;
    std::unordered_set<engine_handle_t> g_live_handles;
    thread_local std::string g_thread_error;

    void SetThreadError(const char *message) {
        g_thread_error = (message != nullptr) ? message : "";
    }

    engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                            const char *message) {
        SetThreadError(message);
        return result;
    }

    bool IsHandleLiveLocked(engine_handle_t handle) {
        return g_live_handles.find(handle) != g_live_handles.end();
    }

    engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                         engine_handle_s **out_impl) {
        if(handle == nullptr) {
            return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                           "engine handle is null");
        }
        if(!IsHandleLiveLocked(handle)) {
            return SetThreadErrorAndReturn(
                ENGINE_RESULT_INVALID_ARGUMENT,
                "engine handle is invalid or already destroyed");
        }
        *out_impl = reinterpret_cast<engine_handle_s *>(handle);
        return ENGINE_RESULT_OK;
    }

    void SetHandleErrorLocked(engine_handle_s *impl, const char *message) {
        impl->last_error = (message != nullptr) ? message : "";
    }

} // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t *out_api_version) {
    if(out_api_version == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_api_version is null");
    }
    *out_api_version = ENGINE_API_VERSION;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t *desc,
                              engine_handle_t *out_handle) {
    if(desc == nullptr || out_handle == nullptr) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_create requires non-null desc and out_handle");
    }

    if(desc->struct_size < sizeof(engine_create_desc_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_create_desc_t.struct_size is too small");
    }

    const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
    const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
    if(caller_major != expected_major) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                       "unsupported engine API major version");
    }

    auto *impl = new(std::nothrow) engine_handle_s();
    if(impl == nullptr) {
        *out_handle = nullptr;
        return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                       "failed to allocate engine handle");
    }
    impl->state = ToStateValue(EngineState::kCreated);

    auto handle = reinterpret_cast<engine_handle_t>(impl);
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        g_live_handles.insert(handle);
    }

    *out_handle = handle;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
    if(handle == nullptr) {
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    engine_handle_s *impl = nullptr;
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto it = g_live_handles.find(handle);
        if(it == g_live_handles.end()) {
            return SetThreadErrorAndReturn(
                ENGINE_RESULT_INVALID_ARGUMENT,
                "engine handle is invalid or already destroyed");
        }
        impl = reinterpret_cast<engine_handle_s *>(handle);
        g_live_handles.erase(it);
    }

    {
        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        impl->state = ToStateValue(EngineState::kDestroyed);
        impl->last_error.clear();
    }
    delete impl;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char *game_root_path_utf8,
                                 const char *startup_script_utf8) {
    (void)startup_script_utf8;

    if(game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "game_root_path_utf8 is null or empty");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        SetHandleErrorLocked(impl, "engine is already destroyed");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->state = ToStateValue(EngineState::kOpened);
    impl->startup_state = ENGINE_STARTUP_STATE_SUCCEEDED;
    impl->startup_logs.clear();
    impl->startup_logs.push_back("engine_open_game => OK");
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char *game_root_path_utf8,
                                       const char *startup_script_utf8) {
    return engine_open_game(handle, game_root_path_utf8, startup_script_utf8);
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t *out_state) {
    if(out_state == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_state is null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    *out_state = impl->startup_state;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char *out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t *out_bytes_written) {
    if(out_bytes_written == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_bytes_written is null");
    }
    if(out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "out_buffer is null or buffer_size is 0");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    uint32_t written = 0;
    while(!impl->startup_logs.empty()) {
        const std::string line = impl->startup_logs.front();
        const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
        if(written + needed > buffer_size) {
            break;
        }
        std::memcpy(out_buffer + written, line.data(), line.size());
        written += static_cast<uint32_t>(line.size());
        out_buffer[written++] = '\n';
        impl->startup_logs.pop_front();
    }
    if(written < buffer_size) {
        out_buffer[written] = '\0';
    } else {
        out_buffer[buffer_size - 1] = '\0';
    }
    *out_bytes_written = written;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

// Stub build: file logging is not applicable.
engine_result_t engine_set_log_file_path(const char *path) {
    (void)path;
    SetThreadError("engine_set_log_file_path is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
    (void)delta_ms;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine is paused");
        return ENGINE_RESULT_INVALID_STATE;
    }
    if(impl->state != ToStateValue(EngineState::kOpened)) {
        SetHandleErrorLocked(
            impl, "engine_open_game must succeed before engine_tick");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->frame_serial += 1;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kPaused)) {
        impl->last_error.clear();
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if(impl->state != ToStateValue(EngineState::kOpened)) {
        SetHandleErrorLocked(impl, "engine_pause requires opened state");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->state = ToStateValue(EngineState::kPaused);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kOpened)) {
        impl->last_error.clear();
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if(impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine_resume requires paused state");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->state = ToStateValue(EngineState::kOpened);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t *option) {
    if(option == nullptr || option->key_utf8 == nullptr ||
       option->key_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "option and option->key_utf8 must be non-null/non-empty");
    }
    if(option->value_utf8 == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "option->value_utf8 must be non-null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        SetHandleErrorLocked(impl, "engine is already destroyed");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_cancel_termination(engine_handle_t handle) {
    // 无 krkr2 运行时的构建（宿主校验用）：这里根本没有终止状态，幂等返回成功，
    // 与运行时实现保持同一份 ABI 语义。
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resolve_window_close(engine_handle_t handle,
                                            int32_t allow_close) {
    // 无 krkr2 运行时的构建（宿主校验用）：没有窗口状态可言，幂等返回成功，
    // 与运行时实现保持同一份 ABI 语义。allow_close 只为签名一致。
    (void)allow_close;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle, uint32_t width,
                                        uint32_t height) {
    if(width == 0 || height == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "width and height must be > 0");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        SetHandleErrorLocked(impl, "engine is already destroyed");
        return ENGINE_RESULT_INVALID_STATE;
    }

    impl->surface_width = width;
    impl->surface_height = height;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t *out_frame_desc) {
    if(out_frame_desc == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_frame_desc is null");
    }
    if(out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_frame_desc_t.struct_size is too small");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kDestroyed)) {
        SetHandleErrorLocked(impl, "engine is already destroyed");
        return ENGINE_RESULT_INVALID_STATE;
    }

    std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
    out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
    out_frame_desc->width = impl->surface_width;
    out_frame_desc->height = impl->surface_height;
    out_frame_desc->stride_bytes = impl->surface_width * 4u;
    out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
    out_frame_desc->frame_serial = impl->frame_serial;

    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle, void *out_pixels,
                                       size_t out_pixels_size) {
    if(out_pixels == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_pixels is null");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state != ToStateValue(EngineState::kOpened) &&
       impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(
            impl,
            "engine_open_game must succeed before engine_read_frame_rgba");
        return ENGINE_RESULT_INVALID_STATE;
    }

    const size_t required_size = static_cast<size_t>(impl->surface_width) *
        static_cast<size_t>(impl->surface_height) * 4u;
    if(out_pixels_size < required_size) {
        SetHandleErrorLocked(
            impl, "out_pixels_size is smaller than required frame buffer size");
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    std::memset(out_pixels, 0, required_size);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void **out_window_handle) {
    if(out_window_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_window_handle is null");
    }
    *out_window_handle = nullptr;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state != ToStateValue(EngineState::kOpened) &&
       impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl,
                             "engine_open_game must succeed before "
                             "engine_get_host_native_window");
        return ENGINE_RESULT_INVALID_STATE;
    }

    SetHandleErrorLocked(impl,
                         "engine_get_host_native_window is not supported");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void **out_view_handle) {
    if(out_view_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_view_handle is null");
    }
    *out_view_handle = nullptr;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state != ToStateValue(EngineState::kOpened) &&
       impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(
            impl,
            "engine_open_game must succeed before engine_get_host_native_view");
        return ENGINE_RESULT_INVALID_STATE;
    }

    SetHandleErrorLocked(impl, "engine_get_host_native_view is not supported");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t *event) {
    if(event == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "event is null");
    }
    if(event->struct_size < sizeof(engine_input_event_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_input_event_t.struct_size is too small");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if(impl->state == ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine is paused");
        return ENGINE_RESULT_INVALID_STATE;
    }
    if(impl->state != ToStateValue(EngineState::kOpened)) {
        SetHandleErrorLocked(
            impl, "engine_open_game must succeed before engine_send_input");
        return ENGINE_RESULT_INVALID_STATE;
    }

    switch(event->type) {
        case ENGINE_INPUT_EVENT_POINTER_DOWN:
        case ENGINE_INPUT_EVENT_POINTER_MOVE:
        case ENGINE_INPUT_EVENT_POINTER_UP:
        case ENGINE_INPUT_EVENT_POINTER_SCROLL:
        case ENGINE_INPUT_EVENT_KEY_DOWN:
        case ENGINE_INPUT_EVENT_KEY_UP:
        case ENGINE_INPUT_EVENT_TEXT_INPUT:
        case ENGINE_INPUT_EVENT_BACK:
            break;
        default:
            SetHandleErrorLocked(impl, "unsupported input event type");
            return ENGINE_RESULT_NOT_SUPPORTED;
    }

    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                   uint32_t iosurface_id,
                                                   uint32_t width,
                                                   uint32_t height) {
    (void)iosurface_id;
    (void)width;
    (void)height;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    SetHandleErrorLocked(
        impl,
        "engine_set_render_target_iosurface is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                 void *native_window,
                                                 uint32_t width,
                                                 uint32_t height) {
    (void)native_window;
    (void)width;
    (void)height;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    SetHandleErrorLocked(
        impl,
        "engine_set_render_target_surface is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                               uint32_t *out_flag) {
    if(out_flag == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_flag is null");
    }
    *out_flag = 0;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char *out_buffer,
                                         uint32_t buffer_size) {
    (void)handle;
    if(out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';

    // Stub build — return a placeholder string.
    const char *stub_info = "Stub (no runtime)";
    std::strncpy(out_buffer, stub_info, buffer_size - 1);
    out_buffer[buffer_size - 1] = '\0';
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t *out_stats) {
    if(out_stats == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_stats is null");
    }
    if(out_stats->struct_size < sizeof(engine_memory_stats_t)) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_memory_stats_t.struct_size is too small");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if(result != ENGINE_RESULT_OK) {
        return result;
    }

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->struct_size = sizeof(engine_memory_stats_t);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

const char *engine_get_last_error(engine_handle_t handle) {
    if(handle == nullptr) {
        return g_thread_error.c_str();
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if(!IsHandleLiveLocked(handle)) {
        SetThreadError("engine handle is invalid or already destroyed");
        return g_thread_error.c_str();
    }
    auto *impl = reinterpret_cast<engine_handle_s *>(handle);
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    return impl->last_error.c_str();
}

} // extern "C"

#endif
