#pragma once

// Public C ABI header. Included *outside* the runtime guard on purpose: the stub
// translation unit (engine_api_stub.cpp) is compiled when
// ENGINE_API_USE_KRKR2_RUNTIME is not defined and still needs engine_handle_t /
// engine_result_t / engine_*_t, which live here.
#include "engine_api.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <csignal>
#include <sys/stat.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
extern "C" ANativeWindow *krkr_GetNativeWindow();
extern "C" void krkr_GetSurfaceDimensions(uint32_t *, uint32_t *);
#else
#include <execinfo.h>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <ucontext.h>
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "engine_options.h"

#include "environ/Application.h"
#include "environ/EngineBootstrap.h"
#include "environ/EngineLoop.h"
#include "environ/MainScene.h"
#include "environ/Platform.h"

#include "base/core/EventIntf.h"
#include "base/script/ScriptMgnIntf.h"
#include "base/core/StorageIntf.h"
#include "environ/sysinit/SysInitIntf.h"
#include "base/core/StorageImpl.h"
#include "environ/sysinit/SysInitImpl.h"

#include "compat/CompatLayer.h"
#include "extension/Extension.h"
#include "plugin/PluginImpl.h"
#include "psbfile/PSBMedia.h"
#include "utils/LogUtil.h"
#include "utils/StallWatchdog.h"
#include "visual/FontImpl.h"
#include "visual/FreeTypeFontRasterizer.h"
#include "visual/GraphicsLoaderIntf.h"
#include "visual/RenderManager.h"
#include "visual/TransIntf.h"
#include "visual/WindowIntf.h"
#include "visual/impl/BitmapBitsAlloc.h"
#include "visual/impl/LayerBitmapImpl.h"
#include "visual/impl/MenuItemImpl.h"
#include "visual/impl/WindowImpl.h"
#include "visual/ogl/krkr_egl_context.h"
#include "visual/ogl/ogl_common.h"
#include "ConfigManager/GlobalConfigManager.h"

// TVPDrawSceneOnce / TVPResetDrawSceneOnceTimingForRestart 的声明由上面已
// include 的 environ/MainScene.h 提供（现在在 krkr::environ，并带全局 using）。
bool TVPHostVideoOverlayFrameActive();
extern "C" void TVPRegisterKrkrGLESPluginAnchor();
#ifdef KRKR2_LIVE2D
extern "C" void TVPRegisterKrkrLive2DPluginAnchor();
#endif

struct engine_handle_s {
    std::recursive_mutex mutex;
    std::string last_error;
    int state = 0;
    std::thread::id owner_thread;
    bool runtime_owner = false;
    uint64_t tick_count = 0;

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

    struct FpsLimitState {
        uint32_t limit = 0;
        uint64_t interval_us = 0;
        std::chrono::steady_clock::time_point last_render_time{};
        bool initialized = false;
    } fps;

    struct FramePerfState {
        std::chrono::steady_clock::time_point window_start{};
        uint64_t frames = 0;
        uint64_t update_us = 0;
        uint64_t post_us = 0;
        uint64_t max_update_us = 0;
        uint64_t max_post_us = 0;
        uint64_t last_update_us = 0;
        uint64_t slow_frames = 0;
        std::chrono::steady_clock::time_point prev_tick_end{};
        uint64_t long_gap_frames = 0;
    } perf;

    bool perf_enabled = true;

    struct InputState {
        std::deque<engine_input_event_t> pending_events;
        std::unordered_set<intptr_t> active_pointer_ids;
        bool native_mouse_callbacks_disabled = false;
    } input;

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

namespace engine_api {

constexpr size_t kMaxStartupLogs = 4000;
constexpr size_t kMaxQueuedInputs = 512;
constexpr size_t kMaxQueuedMenuInvokes = 32;

enum class EngineState { kCreated = 0, kOpened, kPaused, kDestroyed };
inline int ToStateValue(EngineState state) { return static_cast<int>(state); }

extern std::recursive_mutex g_registry_mutex;
extern std::unordered_set<engine_handle_t> g_live_handles;
extern thread_local std::string g_thread_error;
extern engine_handle_t g_runtime_owner;
extern bool g_runtime_active;
extern bool g_runtime_started_once;
extern bool g_engine_bootstrapped;
extern std::atomic<bool> g_runtime_startup_active;
extern engine_handle_t g_runtime_startup_owner;

extern std::once_flag g_loggers_init_once;
extern std::shared_ptr<spdlog::sinks::sink> g_startup_log_sink;
extern std::mutex g_logfile_mutex;
extern std::string g_log_file_path;
extern std::shared_ptr<spdlog::sinks::sink> g_file_sink;

extern std::mutex g_compat_mutex;
extern std::string g_compat_request;
extern std::string g_compat_game_root;
extern bool g_ogldrawdevice_explicit;
extern std::string g_compat_resolved_name;
extern std::string g_compat_resolved_mode;

extern std::mutex g_input_queue_mutex;
extern std::deque<engine_input_event_t> g_input_queue;
extern std::atomic<engine_handle_t> g_input_target_handle;

extern std::mutex g_menu_mutex;
extern std::string g_menu_snapshot;
extern std::mutex g_menu_invoke_mutex;
extern std::deque<std::string> g_menu_invoke_queue;
extern std::atomic<bool> g_menu_snapshot_requested;

void SetThreadError(const char *message);
engine_result_t SetThreadErrorAndReturn(engine_result_t result, const char *message);
bool IsHandleLiveLocked(engine_handle_t handle);
engine_result_t ValidateHandleLocked(engine_handle_t handle, engine_handle_s **out_impl);
void SetHandleErrorLocked(engine_handle_s *impl, const char *message);
engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s *impl, engine_result_t result,
                                              const char *message);
engine_result_t ValidateHandleThreadLocked(engine_handle_s *impl);
void ClearHandleErrorLocked(engine_handle_s *impl);

bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height);

#if defined(__ANDROID__)
// Direct logcat line, shared by the logging and lifecycle modules. spdlog may not
// be initialized yet during early startup, so call sites use this instead.
void AndroidInfoLog(const char *fmt, ...);
#endif

void EnsureRuntimeLoggersInitialized();
void EnsureInternalPluginAnchorsLinked();

void FramePerfAccumulate(engine_handle_s *impl,
                         const std::chrono::steady_clock::time_point &update_begin,
                         const std::chrono::steady_clock::time_point &update_end);
void FramePerfAccumulatePost(engine_handle_s *impl,
                             const std::chrono::steady_clock::time_point &begin,
                             const std::chrono::steady_clock::time_point &end);

void PushStartupLog(engine_handle_s *impl, const std::string &message);
void SetStartupState(engine_handle_s *impl, uint32_t state);
uint32_t GetStartupState(engine_handle_s *impl);
void ResetStartupState(engine_handle_s *impl);
void MarkStartupWorkerRunning(engine_handle_s *impl, bool running);
std::thread DetachStartupWorker(engine_handle_s *impl);

struct FrameReadbackLayout {
    int32_t read_x = 0;
    int32_t read_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_bytes = 0;
};
FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s *impl);
bool ReadCurrentFrameRgba(const FrameReadbackLayout &layout, void *out_pixels);

bool IsFinitePointerValue(double value);
engine_result_t DispatchInputEventNow(engine_handle_s *impl, const engine_input_event_t &event,
                                      const char **out_error_message);

void EnqueueInputEvent(const engine_input_event_t &event);
std::deque<engine_input_event_t> TakeQueuedInputEvents();
void DropQueuedInputEvents();
engine_result_t DrainQueuedInputEvents(const char **out_error_message);

void RefreshWindowMenuSnapshot();
void DrainQueuedMenuInvokes();

void ApplyCompatProfile();

std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char *name);
void AttachFileSinkToLoggers(const std::shared_ptr<spdlog::logger> &core,
                             const std::shared_ptr<spdlog::logger> &tjs2,
                             const std::shared_ptr<spdlog::logger> &plugin);
void DetachFileSinkFromLoggers(const std::shared_ptr<spdlog::logger> &core,
                               const std::shared_ptr<spdlog::logger> &tjs2,
                               const std::shared_ptr<spdlog::logger> &plugin,
                               const std::shared_ptr<spdlog::sinks::sink> &old_sink);

} // namespace engine_api

#endif // ENGINE_API_USE_KRKR2_RUNTIME