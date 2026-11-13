#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

std::mutex g_menu_mutex;
std::string g_menu_snapshot;
std::mutex g_menu_invoke_mutex;
std::deque<std::string> g_menu_invoke_queue;
std::atomic<bool> g_menu_snapshot_requested{false};

void RefreshWindowMenuSnapshot() {
    if (!g_menu_snapshot_requested.exchange(false)) return;
    std::string fresh;
    try {
        TVPSerializeMainWindowMenu(fresh);
    } catch (...) {
        fresh.clear();
    }
    std::lock_guard<std::mutex> lk(g_menu_mutex);
    if (fresh != g_menu_snapshot) g_menu_snapshot = std::move(fresh);
}

void DrainQueuedMenuInvokes() {
    for (;;) {
        std::string id;
        {
            std::lock_guard<std::mutex> lk(g_menu_invoke_mutex);
            if (g_menu_invoke_queue.empty()) break;
            id = std::move(g_menu_invoke_queue.front());
            g_menu_invoke_queue.pop_front();
        }
        bool ok = false;
        try {
            ok = TVPInvokeMainWindowMenuItem(id);
        } catch (...) {
            ok = false;
        }
        if (!ok) {
            spdlog::debug("window menu item '{}' not found or disabled", id);
        }
    }
}

} // namespace engine_api

extern "C" {

engine_result_t engine_list_window_menu(char *out_buffer, uint32_t buffer_size,
                                        uint32_t *out_bytes_written) {
    using namespace engine_api;
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    std::string snapshot;
    {
        std::lock_guard<std::mutex> lk(g_menu_mutex);
        snapshot = g_menu_snapshot;
    }
    g_menu_snapshot_requested.store(true);
    if (snapshot.size() >= buffer_size) {
        snapshot.resize(buffer_size - 1);
        const size_t last_nl = snapshot.rfind('\n');
        snapshot.resize(last_nl == std::string::npos ? 0 : last_nl + 1);
    }
    std::memcpy(out_buffer, snapshot.c_str(), snapshot.size() + 1);
    if (out_bytes_written) *out_bytes_written = static_cast<uint32_t>(snapshot.size());
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

engine_result_t engine_invoke_window_menu(const char *item_id_utf8) {
    using namespace engine_api;
    if (item_id_utf8 == nullptr || item_id_utf8[0] == '\0') {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "item_id_utf8 is null or empty");
    }
    {
        std::lock_guard<std::mutex> lk(g_menu_invoke_mutex);
        g_menu_invoke_queue.emplace_back(item_id_utf8);
        if (g_menu_invoke_queue.size() > kMaxQueuedMenuInvokes)
            g_menu_invoke_queue.pop_front();
    }
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif