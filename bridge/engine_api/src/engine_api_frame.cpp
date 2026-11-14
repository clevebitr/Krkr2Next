#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s *impl) {
    FrameReadbackLayout layout;
    layout.width = impl->frame.surface_width;
    layout.height = impl->frame.surface_height;

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (glGetError() == GL_NO_ERROR && viewport[2] > 0 && viewport[3] > 0) {
        layout.read_x = viewport[0];
        layout.read_y = viewport[1];
        layout.width = static_cast<uint32_t>(viewport[2]);
        layout.height = static_cast<uint32_t>(viewport[3]);
    } else {
        auto &egl = krkr::GetEngineEGLContext();
        if (egl.IsValid()) {
            const uint32_t egl_w = egl.GetWidth();
            const uint32_t egl_h = egl.GetHeight();
            if (egl_w > 0 && egl_h > 0) {
                layout.width = egl_w;
                layout.height = egl_h;
            }
        }
    }
    if (layout.width == 0) layout.width = 1;
    if (layout.height == 0) layout.height = 1;
    layout.stride_bytes = layout.width * 4u;
    return layout;
}

bool ReadCurrentFrameRgba(const FrameReadbackLayout &layout, void *out_pixels) {
    if (layout.width == 0 || layout.height == 0 || out_pixels == nullptr) return false;
    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(static_cast<GLint>(layout.read_x), static_cast<GLint>(layout.read_y),
                 static_cast<GLsizei>(layout.width), static_cast<GLsizei>(layout.height),
                 GL_RGBA, GL_UNSIGNED_BYTE, out_pixels);
    const GLenum read_pixels_error = glGetError();
    if (read_pixels_error != GL_NO_ERROR) return false;

    auto *bytes = static_cast<uint8_t *>(out_pixels);
    const size_t row_bytes = static_cast<size_t>(layout.stride_bytes);
    std::vector<uint8_t> row_buffer(row_bytes);
    const uint32_t half_rows = layout.height / 2u;
    for (uint32_t y = 0; y < half_rows; ++y) {
        uint8_t *row_top = bytes + static_cast<size_t>(y) * row_bytes;
        uint8_t *row_bottom = bytes + static_cast<size_t>(layout.height - 1u - y) * row_bytes;
        std::memcpy(row_buffer.data(), row_top, row_bytes);
        std::memcpy(row_top, row_bottom, row_bytes);
        std::memcpy(row_bottom, row_buffer.data(), row_bytes);
    }
    return true;
}

} // namespace engine_api

extern "C" {

engine_result_t engine_set_surface_size(engine_handle_t handle, uint32_t width, uint32_t height) {
    using namespace engine_api;
    if (width == 0 || height == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "width and height must be > 0");
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
    impl->frame.surface_width = width;
    impl->frame.surface_height = height;
    impl->frame.width = 0;
    impl->frame.height = 0;
    impl->frame.stride_bytes = 0;
    impl->frame.rgba.clear();
    impl->frame.ready = false;

    if (g_runtime_active && g_runtime_owner == handle) {
        auto &egl = krkr::GetEngineEGLContext();
        if (egl.IsValid()) {
            if (egl.HasNativeWindow()) {
                egl.UpdateNativeWindowSize(width, height);
            } else {
                const uint32_t cur_w = egl.GetWidth();
                const uint32_t cur_h = egl.GetHeight();
                if (cur_w != width || cur_h != height) {
                    egl.Resize(width, height);
                    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
                }
            }
        }
        if (TVPMainWindow) {
            auto *dd = TVPMainWindow->GetDrawDevice();
            if (dd) dd->SetWindowSize(static_cast<tjs_int>(width), static_cast<tjs_int>(height));
        }
    }
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t *out_frame_desc) {
    using namespace engine_api;
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
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "engine is already destroyed");
    }

    FrameReadbackLayout layout;
    if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
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
    using namespace engine_api;
    if (out_pixels == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "out_pixels is null");
    }
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    engine_handle_s *impl = nullptr;
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) return result;
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (impl->state != ToStateValue(EngineState::kOpened) &&
        impl->state != ToStateValue(EngineState::kPaused)) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_read_frame_rgba");
    }

    FrameReadbackLayout layout;
    if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
        impl->frame.stride_bytes > 0) {
        layout.width = impl->frame.width;
        layout.height = impl->frame.height;
        layout.stride_bytes = impl->frame.stride_bytes;
    } else {
        layout = GetFrameReadbackLayoutLocked(impl);
        const size_t required_size =
            static_cast<size_t>(layout.stride_bytes) * static_cast<size_t>(layout.height);
        impl->frame.rgba.assign(required_size, 0);
        impl->frame.width = layout.width;
        impl->frame.height = layout.height;
        impl->frame.stride_bytes = layout.stride_bytes;
        impl->frame.ready = true;
    }

    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) * static_cast<size_t>(layout.height);
    if (out_pixels_size < required_size) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_ARGUMENT,
            "out_pixels_size is smaller than required frame buffer size");
    }
    if (impl->frame.rgba.size() < required_size) impl->frame.rgba.resize(required_size, 0);
    std::memcpy(out_pixels, impl->frame.rgba.data(), required_size);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle, void **out_window_handle) {
    using namespace engine_api;
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
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_get_host_native_window");
    }
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "engine_get_host_native_window is only supported on macOS runtime");
}

engine_result_t engine_get_host_native_view(engine_handle_t handle, void **out_view_handle) {
    using namespace engine_api;
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
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;
    if (!g_runtime_active || g_runtime_owner != handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_INVALID_STATE,
            "engine_open_game must succeed before engine_get_host_native_view");
    }
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "engine_get_host_native_view is not supported in headless mode");
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle, uint32_t iosurface_id,
                                                   uint32_t width, uint32_t height) {
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
            "engine_open_game must succeed before engine_set_render_target_iosurface");
    }
    (void)iosurface_id;
    (void)width;
    (void)height;
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "IOSurface render target is not supported (Android uses ANativeWindow)");
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle, void *native_window,
                                                 uint32_t width, uint32_t height) {
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
            "engine_open_game must succeed before engine_set_render_target_surface");
    }
#if defined(__ANDROID__)
    auto &egl = krkr::GetEngineEGLContext();
    if (!egl.IsValid()) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "EGL context not initialized");
    }
    if (native_window == nullptr) {
        egl.DetachNativeWindow();
        impl->render.native_window_attached = false;
        spdlog::info("engine_set_render_target_surface: detached, Pbuffer mode");
    } else {
        if (width == 0 || height == 0) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INVALID_ARGUMENT,
                "width and height must be > 0 when setting Surface");
        }
        if (!egl.AttachNativeWindow(native_window, width, height)) {
            return SetHandleErrorAndReturnLocked(
                impl, ENGINE_RESULT_INTERNAL_ERROR,
                "failed to attach Android Surface as render target");
        }
        impl->render.native_window_attached = true;
        spdlog::info("engine_set_render_target_surface: attached {}x{}", width, height);
        if (TVPMainWindow) {
            auto *dd = TVPMainWindow->GetDrawDevice();
            if (dd) dd->SetWindowSize(static_cast<tjs_int>(width), static_cast<tjs_int>(height));
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

engine_result_t engine_get_renderer_info(engine_handle_t handle, char *out_buffer,
                                         uint32_t buffer_size) {
    using namespace engine_api;
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';
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
            "engine_open_game must succeed before engine_get_renderer_info");
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
        return SetHandleErrorAndReturnLocked(
            impl, ENGINE_RESULT_STARTUP_PENDING,
            "engine_get_renderer_info is unavailable while the async startup is still running");
    }
    auto &egl = krkr::GetEngineEGLContext();
    if (!egl.IsValid()) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "EGL context not initialized");
    }
    if (!egl.MakeCurrent()) {
        return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                             "failed to make EGL context current");
    }
    const char *gl_renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *gl_version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (!gl_renderer) gl_renderer = "(unknown)";
    if (!gl_version) gl_version = "(unknown)";
    std::string info = std::string(gl_renderer) + " | " + std::string(gl_version);
    std::strncpy(out_buffer, info.c_str(), buffer_size - 1);
    out_buffer[buffer_size - 1] = '\0';
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t *out_stats) {
    using namespace engine_api;
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
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;

    std::memset(out_stats, 0, sizeof(*out_stats));
    out_stats->struct_size = sizeof(engine_memory_stats_t);
    TVPMemoryInfo meminfo{};
    TVPGetMemoryInfo(meminfo);
    out_stats->self_used_mb = static_cast<uint32_t>(std::max<tjs_int>(0, TVPGetSelfUsedMemory()));
    out_stats->system_free_mb =
        static_cast<uint32_t>(std::max<tjs_int>(0, TVPGetSystemFreeMemory()));
    out_stats->system_total_mb = static_cast<uint32_t>(meminfo.MemTotal / 1024);
    out_stats->graphic_cache_bytes = TVPGetGraphicCacheTotalBytes();
    out_stats->graphic_cache_limit_bytes = TVPGetGraphicCacheLimit();
    out_stats->xp3_segment_cache_bytes = krkr::io::GetXP3SegmentCacheTotalBytes();
    out_stats->archive_cache_entries = krkr::io::GetArchiveCacheCount();
    out_stats->archive_cache_limit = krkr::io::GetArchiveCacheLimit();
    out_stats->autopath_cache_entries = krkr::io::GetAutoPathCacheCount();
    out_stats->autopath_cache_limit = krkr::io::GetAutoPathCacheLimit();
    out_stats->autopath_table_entries = krkr::io::GetAutoPathTableCount();

    PSB::PSBMediaCacheStats psb_stats{};
    if (PSB::GetPSBMediaCacheStats(psb_stats)) {
        out_stats->psb_cache_bytes = psb_stats.bytesInUse;
        out_stats->psb_cache_entries = static_cast<uint32_t>(psb_stats.entryCount);
        out_stats->psb_cache_entry_limit = static_cast<uint32_t>(psb_stats.entryLimit);
        out_stats->psb_cache_hits = psb_stats.hitCount;
        out_stats->psb_cache_misses = psb_stats.missCount;
    }
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif