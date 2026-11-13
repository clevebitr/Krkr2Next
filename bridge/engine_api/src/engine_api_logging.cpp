#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

std::once_flag g_loggers_init_once;
std::shared_ptr<spdlog::sinks::sink> g_startup_log_sink;
std::mutex g_logfile_mutex;
std::string g_log_file_path;
std::shared_ptr<spdlog::sinks::sink> g_file_sink;

#if defined(__ANDROID__)
// Declared in engine_api_internal.h: engine_api_lifecycle.cpp also logs through
// this while the async startup worker is still bringing spdlog up.
void AndroidInfoLog(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "krkr2", fmt, args);
    va_end(args);
}
#endif

namespace {

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg &msg);

class StartupLogSink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg &msg) override {
        PushRuntimeSpdlogToStartupQueue(msg);
    }
    void flush() override {}
    void set_pattern(const std::string &) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
};

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg &msg) {
    if (!g_runtime_startup_active.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!g_runtime_startup_active.load(std::memory_order_relaxed) ||
        g_runtime_startup_owner == nullptr) {
        return;
    }
    if (!IsHandleLiveLocked(g_runtime_startup_owner)) {
        return;
    }
    engine_handle_s *target = reinterpret_cast<engine_handle_s *>(g_runtime_startup_owner);

    const auto level_sv = spdlog::level::to_string_view(msg.level);
    const std::string level(level_sv.data(), level_sv.size());
    std::string logger(msg.logger_name.data(), msg.logger_name.size());
    if (logger.empty()) logger = "core";
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

void DumpNativeBacktrace(void *ucontext) {
#if !defined(__ANDROID__)
    void *frames[32];
    int count = static_cast<int>(backtrace(frames, 32));
    char **symbols = backtrace_symbols(frames, count);
    if (symbols) {
        for (int i = 0; i < count; ++i) {
            spdlog::critical("  [{}] {}", i, symbols[i]);
        }
        free(symbols);
    }
#else
    auto *uc = static_cast<ucontext_t *>(ucontext);
    if (!uc) {
        spdlog::critical("  (no ucontext captured)");
        return;
    }
    auto dumpSym = [](int i, uintptr_t addr) {
        const char *sym = "<unknown>";
        const char *obj = "<unknown>";
        Dl_info info;
        if (dladdr(reinterpret_cast<void *>(addr), &info) != 0) {
            if (info.dli_sname) sym = info.dli_sname;
            if (info.dli_fname) obj = info.dli_fname;
        }
        spdlog::critical("  [{}] {:#016x} {} (in {})", i, addr, sym, obj);
    };

#if defined(__aarch64__)
    const uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
    const uintptr_t lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
    uintptr_t fp = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
    dumpSym(0, pc);
    dumpSym(1, lr);
    const uintptr_t kLow = 0x1000;
    const uintptr_t kHigh = 0x400000000000;
    for (int i = 2; i < 48 && fp > kLow && fp < kHigh; ++i) {
        uintptr_t saved_lr = *reinterpret_cast<volatile uintptr_t *>(fp + 8);
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
    DumpNativeBacktrace(ucontext);
    spdlog::default_logger()->flush();
    signal(sig, SIG_DFL);
    raise(sig);
}

void InstallCrashSignalHandlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = CrashSignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
}

} // namespace

std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char *name) {
    if (auto logger = spdlog::get(name); logger != nullptr) {
        return logger;
    }
    return spdlog::stdout_color_mt(name);
}

void EnsureInternalPluginAnchorsLinked() {
    TVPRegisterKrkrGLESPluginAnchor();
#ifdef KRKR2_LIVE2D
    TVPRegisterKrkrLive2DPluginAnchor();
#endif
}

void EnsureRuntimeLoggersInitialized() {
    std::call_once(g_loggers_init_once, []() {
#if defined(KRKR_LOG_LEVEL_NUM)
        const auto krkr_level = static_cast<spdlog::level::level_enum>(KRKR_LOG_LEVEL_NUM);
        spdlog::set_level(krkr_level);
        spdlog::flush_on(krkr_level);
#elif defined(NDEBUG)
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
#else
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
#endif
        auto core_logger = EnsureNamedLogger("core");
        auto tjs2_logger = EnsureNamedLogger("tjs2");
        auto plugin_logger = EnsureNamedLogger("plugin");
        g_startup_log_sink = std::make_shared<StartupLogSink>();
        auto attach_sink = [](const std::shared_ptr<spdlog::logger> &logger) {
            if (logger == nullptr || g_startup_log_sink == nullptr) return;
            auto &sinks = logger->sinks();
            const auto already_attached =
                std::any_of(sinks.begin(), sinks.end(),
                            [](const std::shared_ptr<spdlog::sinks::sink> &sink) {
                                return sink.get() == g_startup_log_sink.get();
                            });
            if (!already_attached) sinks.push_back(g_startup_log_sink);
        };
        attach_sink(core_logger);
        attach_sink(tjs2_logger);
        attach_sink(plugin_logger);
        if (core_logger != nullptr) {
            spdlog::set_default_logger(core_logger);
        }
        InstallCrashSignalHandlers();
    });
}

void FramePerfAccumulate(engine_handle_s *impl,
                         const std::chrono::steady_clock::time_point &update_begin,
                         const std::chrono::steady_clock::time_point &update_end) {
    if (impl == nullptr || !impl->perf_enabled) return;
    auto &p = impl->perf;
    const uint64_t update_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(update_end - update_begin).count());
    p.frames++;
    p.update_us += update_us;
    if (update_us > p.max_update_us) p.max_update_us = update_us;
    p.last_update_us = update_us;

    if (p.prev_tick_end.time_since_epoch().count() != 0) {
        const uint64_t gap_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(update_begin - p.prev_tick_end)
                .count());
        if (gap_ms > 100) p.long_gap_frames++;
    }
    p.prev_tick_end = update_end;

    if (p.window_start.time_since_epoch().count() == 0) p.window_start = update_begin;
    const uint64_t elapsed_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(update_begin - p.window_start)
            .count());
    if (elapsed_ms < 5000 || p.frames == 0) return;

    const double frames = static_cast<double>(p.frames);
    const double fps = frames * 1000.0 / static_cast<double>(elapsed_ms);
    const double avg_update = static_cast<double>(p.update_us) / frames / 1000.0;
    const double avg_post = static_cast<double>(p.post_us) / frames / 1000.0;
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

void FramePerfAccumulatePost(engine_handle_s *impl,
                             const std::chrono::steady_clock::time_point &begin,
                             const std::chrono::steady_clock::time_point &end) {
    if (impl == nullptr || !impl->perf_enabled) return;
    const uint64_t post_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
    impl->perf.post_us += post_us;
    if (post_us > impl->perf.max_post_us) impl->perf.max_post_us = post_us;
    if (impl->perf.last_update_us + post_us > 33000) impl->perf.slow_frames++;
}

void PushStartupLog(engine_handle_s *impl, const std::string &message) {
    std::lock_guard<std::mutex> guard(impl->startup.mutex);
    impl->startup.logs.push_back(message);
    while (impl->startup.logs.size() > kMaxStartupLogs) {
        impl->startup.logs.pop_front();
    }
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
    if (!impl->startup.worker.joinable()) return std::thread();
    return std::move(impl->startup.worker);
}

void AttachFileSinkToLoggers(const std::shared_ptr<spdlog::logger> &core,
                             const std::shared_ptr<spdlog::logger> &tjs2,
                             const std::shared_ptr<spdlog::logger> &plugin) {
    if (!g_file_sink) return;
    auto attach = [](const std::shared_ptr<spdlog::logger> &logger) {
        if (!logger) return;
        for (const auto &sink : logger->sinks()) {
            if (sink.get() == g_file_sink.get()) return;
        }
        logger->sinks().push_back(g_file_sink);
    };
    attach(core);
    attach(tjs2);
    attach(plugin);
    if (auto def = spdlog::default_logger(); def && def != core) attach(def);
}

void DetachFileSinkFromLoggers(const std::shared_ptr<spdlog::logger> &core,
                               const std::shared_ptr<spdlog::logger> &tjs2,
                               const std::shared_ptr<spdlog::logger> &plugin,
                               const std::shared_ptr<spdlog::sinks::sink> &old_sink) {
    if (!old_sink) return;
    auto detach = [&](const std::shared_ptr<spdlog::logger> &logger) {
        if (!logger) return;
        auto &sinks = logger->sinks();
        for (auto it = sinks.begin(); it != sinks.end();) {
            if (it->get() == old_sink.get()) {
                it = sinks.erase(it);
            } else {
                ++it;
            }
        }
    };
    detach(core);
    detach(tjs2);
    detach(plugin);
    if (auto def = spdlog::default_logger(); def && def != core) detach(def);
}

} // namespace engine_api

extern "C" {

engine_result_t engine_set_log_file_path(const char *path) {
    using namespace engine_api;
    try {
        if (path == nullptr || path[0] == '\0') {
            std::lock_guard<std::mutex> lock(g_logfile_mutex);
            g_log_file_path.clear();
            DetachFileSinkFromLoggers(EnsureNamedLogger("core"), EnsureNamedLogger("tjs2"),
                                      EnsureNamedLogger("plugin"), g_file_sink);
            g_file_sink.reset();
            return ENGINE_RESULT_OK;
        }

        EnsureRuntimeLoggersInitialized();

        std::lock_guard<std::mutex> lock(g_logfile_mutex);
        auto old_sink = g_file_sink;
        auto new_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path, 4u * 1024u * 1024u, 3);
        g_file_sink = new_sink;
        g_log_file_path = path;
        DetachFileSinkFromLoggers(EnsureNamedLogger("core"), EnsureNamedLogger("tjs2"),
                                  EnsureNamedLogger("plugin"), old_sink);
        AttachFileSinkToLoggers(EnsureNamedLogger("core"), EnsureNamedLogger("tjs2"),
                                EnsureNamedLogger("plugin"));
        spdlog::info("engine_set_log_file_path: engine log -> {}", path);
        krkr::stall::SetDumpPath(path);
        spdlog::default_logger()->flush();
        return ENGINE_RESULT_OK;
    } catch (const std::exception &e) {
        std::fprintf(stderr,
                     "[engine_api] engine_set_log_file_path('%s') failed: %s "
                     "-- continuing without a file log sink\n",
                     path ? path : "(null)", e.what());
        return ENGINE_RESULT_IO_ERROR;
    } catch (...) {
        std::fprintf(stderr,
                     "[engine_api] engine_set_log_file_path failed: unknown "
                     "exception -- continuing without a file log sink\n");
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
}

} // extern "C"

#endif