#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

extern "C" {

engine_result_t engine_set_option(engine_handle_t handle, const engine_option_t *option) {
    using namespace engine_api;
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
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) return result;

    const std::string key(option->key_utf8);
    if (key == ENGINE_OPTION_FPS_LIMIT) {
        const int fps = std::atoi(option->value_utf8);
        impl->fps.limit = fps > 0 ? static_cast<uint32_t>(fps) : 0;
        impl->fps.interval_us = fps > 0 ? (1000000u / static_cast<uint32_t>(fps)) : 0;
        impl->fps.initialized = false;
        spdlog::info("engine_set_option: fps_limit={} (interval={}us)", impl->fps.limit,
                     impl->fps.interval_us);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if (key == ENGINE_OPTION_ANGLE_BACKEND) {
        spdlog::info("engine_set_option: angle_backend='{}' ignored (ANGLE removed; using native EGL)",
                     option->value_utf8);
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
    }

    if (key == ENGINE_OPTION_FONT_FALLBACK_MODE) {
        TVPSetFontFallbackModeFromString(option->value_utf8);
        spdlog::info("engine_set_option: font_fallback_mode={}", option->value_utf8);
    }

    if (key == "frame_perf") {
        const std::string v(option->value_utf8);
        impl->perf_enabled = !(v == "off" || v == "0" || v == "false");
        spdlog::info("engine_set_option: frame_perf={}", impl->perf_enabled ? "on" : "off");
    }

    if (key == ENGINE_OPTION_GAME_COMPAT_PROFILE) {
        std::string requested(option->value_utf8);
        if (requested.empty()) requested = ENGINE_GAME_COMPAT_PROFILE_AUTO;
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_compat_request = requested;
        }
        ApplyCompatProfile();
    } else if (key == ENGINE_OPTION_GAME_COMPAT_GAME_ROOT) {
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_compat_game_root = option->value_utf8;
        }
        ApplyCompatProfile();
    } else if (key == ENGINE_OPTION_OGLDRAWDEVICE_COMPAT) {
        const std::string mode(option->value_utf8);
        const bool known = mode == ENGINE_OGLDRAWDEVICE_COMPAT_OFF ||
                           mode == ENGINE_OGLDRAWDEVICE_COMPAT_OGL ||
                           mode == ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS;
        {
            std::lock_guard<std::mutex> lock(g_compat_mutex);
            g_ogldrawdevice_explicit = true;
        }
        spdlog::info("engine_set_option: ogldrawdevice_compat={}{}", mode,
                     known ? "" : " (unknown, treated as off)");
    }

    {
        static const char *const kGraphicsOptions[] = {
            "ogl_compress_tex", "software_compress_tex", "ogl_accurate_render",
            "ogl_max_texsize", "memusage",
        };
        for (const char *graphics_key : kGraphicsOptions) {
            if (key != graphics_key) continue;
            TVPSetShellOption(key, option->value_utf8);
            TVPInvalidateGraphicsOptionCaches();
            spdlog::info("engine_set_option: {}={}", key, option->value_utf8);
            break;
        }
    }

    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));

    if (key == ENGINE_OPTION_ARCHIVE_CACHE_COUNT) {
        const int count = std::atoi(option->value_utf8);
        if (g_engine_bootstrapped && count > 0) {
            krkr::io::SetArchiveCacheCount(static_cast<tjs_uint>(count));
        }
    } else if (key == ENGINE_OPTION_AUTOPATH_CACHE_COUNT) {
        const int count = std::atoi(option->value_utf8);
        if (g_engine_bootstrapped && count > 0) {
            krkr::io::SetAutoPathCacheMaxCount(static_cast<tjs_uint>(count));
        }
    } else if (key == ENGINE_OPTION_PSB_CACHE_MB) {
        const int cache_mb = std::atoi(option->value_utf8);
        if (cache_mb > 0) impl->memory_options.psb_cache_mb = cache_mb;
        if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
            impl->memory_options.psb_cache_mb > 0) {
            PSB::SetPSBMediaCacheBudget(
                static_cast<size_t>(impl->memory_options.psb_cache_entries),
                static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL * 1024ULL);
        }
    } else if (key == ENGINE_OPTION_PSB_CACHE_ENTRIES) {
        const int entries = std::atoi(option->value_utf8);
        if (entries > 0) impl->memory_options.psb_cache_entries = entries;
        if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
            impl->memory_options.psb_cache_mb > 0) {
            PSB::SetPSBMediaCacheBudget(
                static_cast<size_t>(impl->memory_options.psb_cache_entries),
                static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL * 1024ULL);
        }
    }

    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif