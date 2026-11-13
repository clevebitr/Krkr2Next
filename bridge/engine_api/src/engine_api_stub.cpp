#include "engine_api_internal.h"

#if !defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>

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

enum class EngineState { kCreated = 0, kOpened, kPaused, kDestroyed };
inline int ToStateValue(EngineState state) { return static_cast<int>(state); }

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;

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

} // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t *out_api_version) {
    if (out_api_version == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_api_version is null");
    }
    *out_api_version = ENGINE_API_VERSION;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t *desc, engine_handle_t *out_handle) {
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
    auto *impl = new (std::nothrow) engine_handle_s();
    if (impl == nullptr) {
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
    if (handle == nullptr) {
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    engine_handle_s *impl = nullptr;
    {
        std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
        auto it = g_live_handles.find(handle);
        if (it == g_live_handles.end()) {
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

engine_result_t engine_open_game(engine_handle_t handle, const char *game_root_path_utf8,
                                 const char *startup_script_utf8) {
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
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
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

engine_result_t engine_open_game_async(engine_handle_t handle, const char *game_root_path_utf8,
                                       const char *startup_script_utf8) {
    return engine_open_game(handle, game_root_path_utf8, startup_script_utf8);
}

engine_result_t engine_get_startup_state(engine_handle_t handle, uint32_t *out_state) {
    if (out_state == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_state is null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    *out_state = impl->startup_state;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle, char *out_buffer,
                                          uint32_t buffer_size, uint32_t *out_bytes_written) {
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
    uint32_t written = 0;
    while (!impl->startup_logs.empty()) {
        const std::string line = impl->startup_logs.front();
        const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
        if (written + needed > buffer_size) break;
        std::memcpy(out_buffer + written, line.data(), line.size());
        written += static_cast<uint32_t>(line.size());
        out_buffer[written++] = '\n';
        impl->startup_logs.pop_front();
    }
    if (written < buffer_size) {
        out_buffer[written] = '\0';
    } else {
        out_buffer[buffer_size - 1] = '\0';
    }
    *out_bytes_written = written;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_log_file_path(const char * /*path*/) {
    SetThreadError("engine_set_log_file_path is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
    (void)delta_ms;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine is paused");
        return ENGINE_RESULT_INVALID_STATE;
    }
    if (impl->state != ToStateValue(EngineState::kOpened)) {
        SetHandleErrorLocked(impl, "engine_open_game must succeed before engine_tick");
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
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kPaused)) {
        impl->last_error.clear();
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (impl->state != ToStateValue(EngineState::kOpened)) {
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
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kOpened)) {
        impl->last_error.clear();
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }
    if (impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine_resume requires paused state");
        return ENGINE_RESULT_INVALID_STATE;
    }
    impl->state = ToStateValue(EngineState::kOpened);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle, const engine_option_t *option) {
    if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(
            ENGINE_RESULT_INVALID_ARGUMENT,
            "option and option->key_utf8 must be non-null/non-empty");
    }
    if (option->value_utf8 == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "option->value_utf8 must be non-null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
        SetHandleErrorLocked(impl, "engine is already destroyed");
        return ENGINE_RESULT_INVALID_STATE;
    }
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_cancel_termination(engine_handle_t handle) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_resolve_window_close(engine_handle_t handle, int32_t /*allow_close*/) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "width and height must be > 0");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
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
    if (out_frame_desc == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_frame_desc is null");
    }
    if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine_frame_desc_t.struct_size is too small");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
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
    if (out_pixels == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_pixels is null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state != ToStateValue(EngineState::kOpened) &&
        impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(
            impl, "engine_open_game must succeed before engine_read_frame_rgba");
        return ENGINE_RESULT_INVALID_STATE;
    }
    const size_t required_size = static_cast<size_t>(impl->surface_width) *
                                 static_cast<size_t>(impl->surface_height) * 4u;
    if (out_pixels_size < required_size) {
        SetHandleErrorLocked(impl,
                             "out_pixels_size is smaller than required frame buffer size");
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    std::memset(out_pixels, 0, required_size);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle, void **out_window_handle) {
    if (out_window_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_window_handle is null");
    }
    *out_window_handle = nullptr;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state != ToStateValue(EngineState::kOpened) &&
        impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(
            impl, "engine_open_game must succeed before engine_get_host_native_window");
        return ENGINE_RESULT_INVALID_STATE;
    }
    SetHandleErrorLocked(impl, "engine_get_host_native_window is not supported");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_host_native_view(engine_handle_t handle, void **out_view_handle) {
    if (out_view_handle == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_view_handle is null");
    }
    *out_view_handle = nullptr;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state != ToStateValue(EngineState::kOpened) &&
        impl->state != ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(
            impl, "engine_open_game must succeed before engine_get_host_native_view");
        return ENGINE_RESULT_INVALID_STATE;
    }
    SetHandleErrorLocked(impl, "engine_get_host_native_view is not supported");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_send_input(engine_handle_t handle, const engine_input_event_t *event) {
    if (event == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "event is null");
    }
    if (event->struct_size < sizeof(engine_input_event_t)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine_input_event_t.struct_size is too small");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kPaused)) {
        SetHandleErrorLocked(impl, "engine is paused");
        return ENGINE_RESULT_INVALID_STATE;
    }
    if (impl->state != ToStateValue(EngineState::kOpened)) {
        SetHandleErrorLocked(impl, "engine_open_game must succeed before engine_send_input");
        return ENGINE_RESULT_INVALID_STATE;
    }
    switch (event->type) {
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

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle, uint32_t /*id*/,
                                                   uint32_t /*w*/, uint32_t /*h*/) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    SetHandleErrorLocked(impl,
                         "engine_set_render_target_iosurface is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle, void * /*native_window*/,
                                                 uint32_t /*w*/, uint32_t /*h*/) {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    SetHandleErrorLocked(impl,
                         "engine_set_render_target_surface is not supported in stub build");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle, uint32_t *out_flag) {
    if (out_flag == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_flag is null");
    }
    *out_flag = 0;
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t /*handle*/, char *out_buffer,
                                         uint32_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';
    const char *stub_info = "Stub (no runtime)";
    std::strncpy(out_buffer, stub_info, buffer_size - 1);
    out_buffer[buffer_size - 1] = '\0';
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_compat_profile(char *out_buffer, uint32_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    // 与运行时实现保持同一语义：未解析出档位时返回 OK + 空串。
    out_buffer[0] = '\0';
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t *out_stats) {
    if (out_stats == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_stats is null");
    }
    if (out_stats->struct_size < sizeof(engine_memory_stats_t)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine_memory_stats_t.struct_size is too small");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->struct_size = sizeof(engine_memory_stats_t);
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

const char *engine_get_last_error(engine_handle_t handle) {
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

engine_result_t engine_list_window_menu(char *out_buffer, uint32_t buffer_size,
                                        uint32_t *out_bytes_written) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';
    if (out_bytes_written != nullptr) *out_bytes_written = 0;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_invoke_window_menu(const char *item_id_utf8) {
    if (item_id_utf8 == nullptr || item_id_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "item_id_utf8 is null or empty");
    }
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "window menu is not available in this build");
}

engine_result_t engine_is_modal_active(uint32_t *out_active) {
    if (out_active == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_active is null");
    }
    *out_active = 0u;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif