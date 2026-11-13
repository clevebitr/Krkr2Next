#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;
engine_handle_t g_runtime_owner = nullptr;
bool g_runtime_active = false;
bool g_runtime_started_once = false;
bool g_engine_bootstrapped = false;
std::atomic<bool> g_runtime_startup_active{false};
engine_handle_t g_runtime_startup_owner = nullptr;

void SetThreadError(const char *message) {
    g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result, const char *message) {
    SetThreadError(message);
    return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
    return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle, engine_handle_s **out_impl) {
    if (handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "engine handle is null");
    }
    if (!IsHandleLiveLocked(handle)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine handle is invalid or already destroyed");
    }
    *out_impl = reinterpret_cast<engine_handle_s *>(handle);
    return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s *impl, const char *message) {
    impl->last_error = (message != nullptr) ? message : "";
}

engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s *impl, engine_result_t result,
                                              const char *message) {
    SetHandleErrorLocked(impl, message);
    return result;
}

engine_result_t ValidateHandleThreadLocked(engine_handle_s *impl) {
    if (impl->owner_thread != std::this_thread::get_id()) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine handle must be used on the thread where engine_create was called");
    }
    return ENGINE_RESULT_OK;
}

void ClearHandleErrorLocked(engine_handle_s *impl) {
    impl->last_error.clear();
}

bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height) {
    if (g_engine_bootstrapped) {
        return true;
    }
    if (!TVPEngineBootstrap::Initialize(width, height)) {
        return false;
    }
    g_engine_bootstrapped = true;
    return true;
}

} // namespace engine_api

#endif