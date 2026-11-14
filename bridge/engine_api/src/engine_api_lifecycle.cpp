#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

namespace {

void PumpModalInputOnTickThreadImpl() {
    const char *dispatch_error = nullptr;
    if (DrainQueuedInputEvents(&dispatch_error) != ENGINE_RESULT_OK) {
        spdlog::warn("PumpModalInput: 输入派发失败 err={}",
                     dispatch_error ? dispatch_error : "(unknown)");
    }
    DrainQueuedMenuInvokes();
}

engine_result_t OpenGameCore(engine_handle_t handle, engine_handle_s *impl,
                             const char *game_root_path_utf8) {
    if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    if (!EnsureEngineRuntimeInitialized(impl->frame.surface_width, impl->frame.surface_height)) {
        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        SetHandleErrorLocked(impl, "failed to initialize engine runtime for host mode");
        return ENGINE_RESULT_INTERNAL_ERROR;
    }

    EnsureRuntimeLoggersInitialized();
    EnsureInternalPluginAnchorsLinked();

    std::string normalized(game_root_path_utf8);
    auto ends_with = [](const std::string &s, const std::string &suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const bool looks_like_archive =
        ends_with(normalized, ".xp3") || ends_with(normalized, ".XP3") ||
        ends_with(normalized, ".zip") || ends_with(normalized, ".ZIP") ||
        ends_with(normalized, ".7z") || ends_with(normalized, ".tar");
    if (!looks_like_archive && !normalized.empty() && normalized.back() != '/' &&
        normalized.back() != '\\') {
        normalized.push_back('/');
    }

    spdlog::info("engine_open_game: runtime initialized, starting "
                 "application with path: {} (normalized: {})",
                 game_root_path_utf8, normalized);
    krkr::ResetLogGuards();
#if defined(__ANDROID__)
    AndroidInfoLog("engine_open_game: input='%s' normalized='%s'", game_root_path_utf8,
                   normalized.c_str());
#endif
    spdlog::default_logger()->flush();

    try {
        spdlog::debug("engine_open_game: calling Application->StartApplication...");
#if defined(__ANDROID__)
        AndroidInfoLog("engine_open_game: calling StartApplication('%s')", normalized.c_str());
#endif
        spdlog::default_logger()->flush();
        Application->StartApplication(ttstr(normalized.c_str()));
        spdlog::info("engine_open_game: StartApplication returned successfully");
    } catch (const std::exception &e) {
        spdlog::error("engine_open_game: StartApplication threw std::exception: {}", e.what());
        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        SetHandleErrorLocked(impl, "StartApplication threw an exception");
        return ENGINE_RESULT_INTERNAL_ERROR;
    } catch (...) {
        spdlog::error("engine_open_game: StartApplication threw unknown exception");
        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        SetHandleErrorLocked(impl, "StartApplication threw an exception");
        return ENGINE_RESULT_INTERNAL_ERROR;
    }

    if (TVPTerminated) {
        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        SetHandleErrorLocked(impl, "runtime requested termination during startup");
        return ENGINE_RESULT_INVALID_STATE;
    }

    EngineLoop::CreateInstance();
    if (auto *loop = EngineLoop::GetInstance(); loop != nullptr) {
        loop->Start();
    }
    if (auto *scene = TVPMainScene::GetInstance(); scene != nullptr) {
        scene->scheduleUpdate();
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!IsHandleLiveLocked(handle)) return ENGINE_RESULT_INVALID_STATE;

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kDestroyed)) return ENGINE_RESULT_INVALID_STATE;

    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
        g_runtime_startup_active = false;
        g_runtime_startup_owner = nullptr;
    }
    g_runtime_active = true;
    g_runtime_owner = handle;
    g_runtime_started_once = true;
    g_input_target_handle.store(handle, std::memory_order_release);
    DropQueuedInputEvents();

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
    PushStartupLog(impl, "engine_open_game_async: worker started");
    TVPTerminated = false;
    TVPTerminateCode = 0;
    TVPSystemUninitCalled = false;
    TVPTerminateOnWindowClose = false;
    TVPTerminateOnNoWindowStartup = false;
    TVPHostSuppressProcessExit = true;

    const engine_result_t open_result =
        OpenGameCore(handle, impl, game_root_path_utf8.c_str());

    auto &egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) egl.ReleaseCurrent();

    if (open_result == ENGINE_RESULT_OK) {
        PushStartupLog(impl, "engine_open_game => OK");
        SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
    } else {
        std::string error_text;
        {
            std::lock_guard<std::recursive_mutex> guard(impl->mutex);
            error_text = impl->last_error;
        }
        if (error_text.empty()) error_text = "unknown startup error";
        PushStartupLog(impl, "ERROR: " + error_text);
        SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
        }
    }
    MarkStartupWorkerRunning(impl, false);
}

} // namespace

} // namespace engine_api

extern "C" {

engine_result_t engine_create(const engine_create_desc_t *desc, engine_handle_t *out_handle) {
    using namespace engine_api;
    if (desc == nullptr || out_handle == nullptr) {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "engine_create requires non-null desc and out_handle");
    }
    if (desc->struct_size < sizeof(engine_create_desc_t)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine_create_desc_t.struct_size is too small");
    }
    const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
    const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
    if (caller_major != expected_major) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                       "unsupported engine API major version");
    }

    EnsureRuntimeLoggersInitialized();
    EnsureInternalPluginAnchorsLinked();
    TVPHostSuppressProcessExit = true;
    krkr::base::SetDeferWindowClose(true);
    krkr::base::SetModalInputPump(&PumpModalInputOnTickThreadImpl);

    auto *impl = new (std::nothrow) engine_handle_s();
    if (impl == nullptr) {
        *out_handle = nullptr;
        return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                       "failed to allocate engine handle");
    }
    impl->state = ToStateValue(EngineState::kCreated);
    impl->owner_thread = std::this_thread::get_id();
    impl->runtime_owner = false;

    TVPMainThreadID = impl->owner_thread;

    auto handle = reinterpret_cast<engine_handle_t>(impl);
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        g_live_handles.insert(handle);
    }
    *out_handle = handle;
    SetThreadError(nullptr);
    krkr::stall::MarkStage("engine_create 完成");
    krkr::stall::Start();
    return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
    using namespace engine_api;
    if (handle == nullptr) {
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    engine_handle_s *impl = nullptr;
    bool owned_runtime = false;
    std::thread startup_worker;

    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto result = ValidateHandleLocked(handle, &impl);
        if (result != ENGINE_RESULT_OK) return result;

        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        result = ValidateHandleThreadLocked(impl);
        if (result != ENGINE_RESULT_OK) return result;

        owned_runtime = (g_runtime_active && g_runtime_owner == handle);
        if (owned_runtime) {
            g_runtime_active = false;
            g_runtime_owner = nullptr;
            impl->runtime_owner = false;
        }
        engine_handle_t expected_input_handle = handle;
        g_input_target_handle.compare_exchange_strong(expected_input_handle, nullptr,
                                                      std::memory_order_acq_rel);
        DropQueuedInputEvents();
        if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
        }
        startup_worker = DetachStartupWorker(impl);
        SetStartupState(impl, ENGINE_STARTUP_STATE_IDLE);

        impl->state = ToStateValue(EngineState::kDestroyed);
        ClearHandleErrorLocked(impl);
        g_live_handles.erase(handle);
    }

    if (startup_worker.joinable()) startup_worker.join();

    if (owned_runtime) {
        spdlog::info("engine_destroy: entering runtime teardown (owned_runtime=1)");
        try {
            Application->OnDeactivate();
            spdlog::info("engine_destroy: Application::OnDeactivate done");
        } catch (...) {
            spdlog::error("engine_destroy: Application::OnDeactivate threw");
        }
        Application->FilterUserMessage(
            [](std::vector<std::tuple<void *, int, tTVPApplication::tMsg>> &queue) {
                queue.clear();
            });
        spdlog::info("engine_destroy: FilterUserMessage done");

        TVPTerminated = false;
        TVPTerminateCode = 0;

        try {
            spdlog::info("engine_destroy: TVPUnregisterInternalPluginsForRestart begin");
            TVPUnregisterInternalPluginsForRestart();
            spdlog::info("engine_destroy: TVPUnregisterInternalPluginsForRestart end");
        } catch (...) {
            spdlog::error("engine_destroy: TVPUnregisterInternalPluginsForRestart threw");
        }

        try {
            spdlog::info("engine_destroy: Application->OnExit begin");
            Application->OnExit();
            spdlog::info("engine_destroy: Application->OnExit end");
        } catch (...) {
            spdlog::error("engine_destroy: Application->OnExit threw");
        }

        try {
            spdlog::info("engine_destroy: TVPSystemUninit begin");
            TVPSystemUninit();
            spdlog::info("engine_destroy: TVPSystemUninit end");
        } catch (...) {
            spdlog::error("engine_destroy: TVPSystemUninit threw");
        }

        if (auto *scene = TVPMainScene::GetInstance()) {
            spdlog::info("engine_destroy: deleting TVPMainScene...");
            delete scene;
            spdlog::info("engine_destroy: TVPMainScene deleted");
        }
        if (auto *loop = EngineLoop::GetInstance()) {
            spdlog::info("engine_destroy: deleting EngineLoop...");
            delete loop;
            spdlog::info("engine_destroy: EngineLoop deleted");
        }

        try {
            TVPResetRuntimeForRestart();
            TVPResetScriptEngineForRestart();
            TVPResetSysInitImplForRestart();
            TVPResetApplicationForRestart();
            krkr::io::ResetStorageImplForRestart();
            TVPResetRenderManagerForRestart();
            TVPResetWindowListForRestart();
            TVPResetLayerBitmapImplForRestart();
            TVPResetFontImplForRestart();
            TVPResetTransIntfForRestart();
            TVPResetDrawSceneOnceTimingForRestart();
            tTVPBitmapBitsAlloc::ResetForRestart();
            krkr::extension::ResetInstallStateForRestart();
            TVPResetPluginSystemForRestart();
            spdlog::info("engine_destroy: reset-for-restart done");
        } catch (...) {
            spdlog::error("engine_destroy: reset-for-restart threw");
        }

        if (g_engine_bootstrapped) {
            spdlog::info("engine_destroy: TVPEngineBootstrap::Shutdown...");
            TVPEngineBootstrap::Shutdown();
            g_engine_bootstrapped = false;
            spdlog::info("engine_destroy: TVPEngineBootstrap::Shutdown done");
        }

        g_runtime_started_once = false;
        spdlog::info("engine_destroy: runtime teardown complete (restartable)");
        spdlog::default_logger()->flush();
    }

    krkr::stall::Stop();
    delete impl;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle, const char *game_root_path_utf8,
                                 const char *startup_script_utf8) {
    using namespace engine_api;
    (void)startup_script_utf8;
    if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "game_root_path_utf8 is null or empty");
    }

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;

    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is already destroyed");
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine startup is already running");
    }
    if (g_runtime_active) {
        if (g_runtime_owner != handle) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "runtime is already active on another engine handle");
        }
        impl->state = ToStateValue(EngineState::kOpened);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (g_runtime_started_once) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_NOT_SUPPORTED,
            "runtime restart is not supported yet; restart process to open another game");
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

    const engine_result_t open_result = OpenGameCore(handle, impl, game_root_path_utf8);
    if (open_result == ENGINE_RESULT_OK) {
        SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
        PushStartupLog(impl, "engine_open_game => OK");
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if (open_result == ENGINE_RESULT_INTERNAL_ERROR) {
        SetHandleErrorLocked(impl, "StartApplication threw an exception");
    } else if (open_result == ENGINE_RESULT_INVALID_STATE && TVPTerminated) {
        SetHandleErrorLocked(impl, "runtime requested termination during startup");
    } else {
        SetHandleErrorLocked(impl, "engine_open_game failed");
    }
    g_runtime_startup_active = false;
    g_runtime_startup_owner = nullptr;
    SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
    PushStartupLog(impl, std::string("ERROR: ") + impl->last_error);
    return open_result;
}

engine_result_t engine_open_game_async(engine_handle_t handle, const char *game_root_path_utf8,
                                       const char *startup_script_utf8) {
    using namespace engine_api;
    (void)startup_script_utf8;
    if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "game_root_path_utf8 is null or empty");
    }

    std::thread stale_worker;
    std::string game_root_copy(game_root_path_utf8);
    engine_handle_s *impl = nullptr;
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto result = ValidateHandleLocked(handle, &impl);
        if (result != ENGINE_RESULT_OK) return result;

        std::lock_guard<std::recursive_mutex> guard(impl->mutex);
        result = ValidateHandleThreadLocked(impl);
        if (result != ENGINE_RESULT_OK) return result;

        if (impl->state == ToStateValue(EngineState::kDestroyed)) {
            return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                                 "engine is already destroyed");
        }
        if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
            return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                                 "engine startup is already running");
        }
        if (g_runtime_active && g_runtime_owner == handle) {
            SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
            ClearHandleErrorLocked(impl);
            SetThreadError(nullptr);
            return ENGINE_RESULT_OK;
        }
        if (g_runtime_active && g_runtime_owner != handle) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "runtime is already active on another engine handle");
        }
        if (g_runtime_started_once) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_NOT_SUPPORTED,
                "runtime restart is not supported yet; restart process to open another game");
        }

        stale_worker = DetachStartupWorker(impl);
        ResetStartupState(impl);
        SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
        PushStartupLog(impl, "engine_open_game_async: queued startup");
        MarkStartupWorkerRunning(impl, true);
        g_runtime_startup_active = true;
        g_runtime_startup_owner = handle;

        try {
            impl->startup.worker = std::thread([handle, impl, game_root_copy]() mutable {
                RunOpenGameAsync(handle, impl, std::move(game_root_copy));
            });
        } catch (...) {
            MarkStartupWorkerRunning(impl, false);
            SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
            g_runtime_startup_active = false;
            g_runtime_startup_owner = nullptr;
            return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INTERNAL_ERROR,
                                                 "failed to create startup thread");
        }
        ClearHandleErrorLocked(impl);
    }
    if (stale_worker.joinable()) stale_worker.join();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_startup_state(engine_handle_t handle, uint32_t *out_state) {
    using namespace engine_api;
    if (out_state == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_state is null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    *out_state = GetStartupState(impl);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle, char *out_buffer,
                                          uint32_t buffer_size, uint32_t *out_bytes_written) {
    using namespace engine_api;
    if (out_bytes_written == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_bytes_written is null");
    }
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;

    uint32_t written = 0;
    {
        std::lock_guard<std::mutex> startup_guard(impl->startup.mutex);
        while (!impl->startup.logs.empty()) {
            const std::string line = impl->startup.logs.front();
            const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
            if (written + needed > buffer_size) break;
            std::memcpy(out_buffer + written, line.data(), line.size());
            written += static_cast<uint32_t>(line.size());
            out_buffer[written++] = '\n';
            impl->startup.logs.pop_front();
        }
    }
    if (written < buffer_size) {
        out_buffer[written] = '\0';
    } else {
        out_buffer[buffer_size - 1] = '\0';
    }
    *out_bytes_written = written;
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
    using namespace engine_api;
    (void)delta_ms;

    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;

    krkr::stall::MarkStage("engine_tick: 开始");

    if (TVPTerminated) {
        if (TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime has been terminated");
    }
    if (krkr::base::WindowClosePending()) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_WINDOW_CLOSE_REQUESTED;
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_STARTUP_PENDING,
                                             "engine startup is still running");
    }
    if (!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_tick");
    }
    if (impl->state == ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is paused");
    }
    if (impl->state != ToStateValue(EngineState::kOpened)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is not in opened state");
    }
    impl->tick_count += 1;
    krkr::stall::MarkStage("engine_tick: 输入派发");

    {
        const char *dispatch_error = nullptr;
        const engine_result_t dispatch_result = DrainQueuedInputEvents(&dispatch_error);
        if (dispatch_result != ENGINE_RESULT_OK) {
            return SetHandleErrorAndReturnLocked(
                impl, dispatch_result,
                dispatch_error != nullptr ? dispatch_error : "input dispatch failed");
        }
    }

    RefreshWindowMenuSnapshot();
    DrainQueuedMenuInvokes();

#if defined(__ANDROID__)
    if (!impl->render.native_window_attached) {
        ANativeWindow *pending_window = krkr_GetNativeWindow();
        if (pending_window) {
            uint32_t win_w = 0, win_h = 0;
            krkr_GetSurfaceDimensions(&win_w, &win_h);
            auto &egl = krkr::GetEngineEGLContext();
            if (win_w > 0 && win_h > 0) {
                bool attached = false;
                if (!egl.IsValid()) {
                    if (egl.InitializeWithWindow(pending_window, win_w, win_h)) {
                        attached = true;
                    }
                } else {
                    if (egl.AttachNativeWindow(pending_window, win_w, win_h)) {
                        attached = true;
                    }
                }
                if (attached) {
                    impl->render.native_window_attached = true;
                    spdlog::info("engine_tick: auto-attached ANativeWindow {}x{}", win_w, win_h);
                    if (TVPMainWindow) {
                        auto *dd = TVPMainWindow->GetDrawDevice();
                        if (dd) dd->SetWindowSize(static_cast<tjs_int>(win_w),
                                                  static_cast<tjs_int>(win_h));
                    }
                }
            }
            ANativeWindow_release(pending_window);
        }
    } else {
        ANativeWindow *current_window = krkr_GetNativeWindow();
        if (current_window) {
            ANativeWindow_release(current_window);
        } else {
            auto &egl = krkr::GetEngineEGLContext();
            egl.DetachNativeWindow();
            impl->render.native_window_attached = false;
            spdlog::info("engine_tick: ANativeWindow detached, reverted to Pbuffer mode");
        }
    }
#endif

    if (TVPTerminated) {
        if (TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime has been terminated");
    }

    if (impl->fps.limit > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (impl->fps.initialized) {
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                        now - impl->fps.last_render_time)
                                        .count();
            if (static_cast<uint64_t>(elapsed_us) < impl->fps.interval_us) {
                impl->frame.rendered_this_tick = false;
                ClearHandleErrorLocked(impl);
                SetThreadError(nullptr);
                return ENGINE_RESULT_OK;
            }
            const auto ideal_next =
                impl->fps.last_render_time + std::chrono::microseconds(impl->fps.interval_us);
            if (now - ideal_next > std::chrono::microseconds(impl->fps.interval_us)) {
                impl->fps.last_render_time = now;
            } else {
                impl->fps.last_render_time = ideal_next;
            }
        } else {
            impl->fps.last_render_time = now;
            impl->fps.initialized = true;
        }
    }

    {
        auto &egl = krkr::GetEngineEGLContext();
        if (egl.IsValid() && !egl.MakeCurrent()) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_STATE,
                "failed to make EGL context current before engine_tick");
        }
    }

    if (::Application) {
        const auto update_start = std::chrono::steady_clock::now();
        if (TVPMainWindow && TVPHostVideoOverlayFrameActive()) {
            TVPPostWindowUpdate(TVPMainWindow);
        }
        krkr::stall::MarkStage("engine_tick: Application::Run（脚本+合成+绘制）");
        ::Application->Run();
        const auto update_end = std::chrono::steady_clock::now();
        FramePerfAccumulate(impl, update_start, update_end);
    }

    const auto post_start = std::chrono::steady_clock::now();
    krkr::stall::MarkStage("engine_tick: 呈现（DrawSceneOnce/回收/交付）");
    ::TVPDrawSceneOnce(0);
    iTVPTexture2D::RecycleProcess();
    FramePerfAccumulatePost(impl, post_start, std::chrono::steady_clock::now());

    if (TVPTerminated) {
        if (TVPTerminateWindowClosed) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_WINDOW_CLOSED,
                "runtime terminated because the game closed its window");
        }
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_GAME_TERMINATED,
                                             "runtime requested termination");
    }

    impl->frame.rendered_this_tick = true;

    if (impl->render.native_window_attached) {
        impl->frame.serial += 1;
        impl->frame.ready = true;
    } else if (!impl->render.iosurface_attached) {
        const FrameReadbackLayout layout = GetFrameReadbackLayoutLocked(impl);
        const size_t required_size =
            static_cast<size_t>(layout.stride_bytes) * static_cast<size_t>(layout.height);
        if (impl->frame.rgba.size() != required_size) {
            impl->frame.rgba.assign(required_size, 0);
        }
        if (required_size > 0 && ReadCurrentFrameRgba(layout, impl->frame.rgba.data())) {
            impl->frame.width = layout.width;
            impl->frame.height = layout.height;
            impl->frame.stride_bytes = layout.stride_bytes;
            impl->frame.ready = true;
            impl->frame.serial += 1;
        } else if (!impl->frame.ready && required_size > 0) {
            std::fill(impl->frame.rgba.begin(), impl->frame.rgba.end(), 0);
            impl->frame.width = layout.width;
            impl->frame.height = layout.height;
            impl->frame.stride_bytes = layout.stride_bytes;
            impl->frame.ready = true;
            impl->frame.serial += 1;
        }
    } else {
        glFlush();
        impl->frame.serial += 1;
        impl->frame.ready = true;
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
    using namespace engine_api;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_pause");
    }
    if (impl->state == ToStateValue(EngineState::kPaused)) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (impl->state != ToStateValue(EngineState::kOpened)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine_pause requires opened state");
    }
    Application->OnDeactivate();
    impl->input.active_pointer_ids.clear();
    DropQueuedInputEvents();
    impl->state = ToStateValue(EngineState::kPaused);
    krkr::stall::SetPaused(true);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
    using namespace engine_api;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_resume");
    }
    if (impl->state == ToStateValue(EngineState::kOpened)) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (impl->state != ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
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
    using namespace engine_api;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (!TVPTerminated) {
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (TVPTerminateWindowClosed) {
        SetHandleErrorLocked(impl, "cannot cancel: the game closed its window");
        return ENGINE_RESULT_INVALID_STATE;
    }
    TVPTerminated = false;
    TVPTerminateCode = 0;
    if (::Application) ::Application->Unterminate();
    krkr::stall::SetPaused(false);
    krkr::stall::MarkStage("engine_tick: 已取消退出，恢复运行");
    spdlog::info("engine_cancel_termination: 宿主选择继续游戏，已撤销终止标志");
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resolve_window_close(engine_handle_t handle, int32_t allow_close) {
    using namespace engine_api;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (allow_close) {
        krkr::base::ConfirmWindowClose();
        krkr::stall::SetPaused(false);
        spdlog::info("engine_resolve_window_close: 宿主确认退出，已执行关窗终止");
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (TVPTerminated) {
        SetHandleErrorLocked(impl, "cannot keep playing: the runtime already terminated");
        return ENGINE_RESULT_INVALID_STATE;
    }
    krkr::base::CancelWindowClose();
    krkr::stall::SetPaused(false);
    krkr::stall::MarkStage("engine_tick: 已取消关窗请求，恢复运行");
    spdlog::info("engine_resolve_window_close: 宿主选择继续游戏，已取消关窗请求");
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif