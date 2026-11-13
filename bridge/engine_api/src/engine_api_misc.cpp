#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t *out_api_version) {
    using namespace engine_api;
    if (out_api_version == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_api_version is null");
    }
    *out_api_version = ENGINE_API_VERSION;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle, uint32_t *out_flag) {
    using namespace engine_api;
    if (out_flag == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_flag is null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
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

const char *engine_get_last_error(engine_handle_t handle) {
    using namespace engine_api;
    if (handle == nullptr) return g_thread_error.c_str();
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!IsHandleLiveLocked(handle)) {
        SetThreadError("engine handle is invalid or already destroyed");
        return g_thread_error.c_str();
    }
    auto *impl = reinterpret_cast<engine_handle_s *>(handle);
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    return impl->last_error.c_str();
}

engine_result_t engine_is_modal_active(uint32_t *out_active) {
    using namespace engine_api;
    if (out_active == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_active is null");
    }
    *out_active = (TVPGetActiveModalWindow() != nullptr) ? 1u : 0u;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif