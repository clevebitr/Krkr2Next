#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

std::mutex g_compat_mutex;
std::string g_compat_request = ENGINE_GAME_COMPAT_PROFILE_AUTO;
std::string g_compat_game_root;
bool g_ogldrawdevice_explicit = false;
std::string g_compat_resolved_name;
std::string g_compat_resolved_mode;

namespace {

struct CompatProfile {
    const char *name;
    int version;
    const char *ogldrawdevice;
};

const CompatProfile kCompatProfiles[] = {
    {ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2, 1, ENGINE_OGLDRAWDEVICE_COMPAT_OFF},
    {ENGINE_GAME_COMPAT_PROFILE_KRKRZ_GPU, 1, ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS},
    {ENGINE_GAME_COMPAT_PROFILE_KRKRZ_OGL, 1, ENGINE_OGLDRAWDEVICE_COMPAT_OGL},
    {ENGINE_GAME_COMPAT_PROFILE_AETHERKIRI, 1, ENGINE_OGLDRAWDEVICE_COMPAT_ALIAS},
};

std::string s_last_resolved_key;

const CompatProfile *FindCompatProfile(const std::string &name) {
    for (const auto &p : kCompatProfiles) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

bool CompatFileExists(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

const char *DetectCompatProfileByMarkers(const std::string &root) {
    static const char *kGpuMarkers[] = {"krkrgles.dll", "krkrlive2d.dll"};
    static const char *kEmoteMarkers[] = {"motionplayer.dll", "motionplayer_nod3d.dll"};
    const auto has_marker = [&root](const char *m) {
        return CompatFileExists(root + "/plugin/" + m) || CompatFileExists(root + "/" + m);
    };
    for (const char *m : kGpuMarkers) {
        if (has_marker(m)) return ENGINE_GAME_COMPAT_PROFILE_KRKRZ_GPU;
    }
    for (const char *m : kEmoteMarkers) {
        if (has_marker(m)) return ENGINE_GAME_COMPAT_PROFILE_AETHERKIRI;
    }
    return ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2;
}

void ApplyCompatProfileLocked() {
    if (g_compat_request == ENGINE_GAME_COMPAT_PROFILE_AUTO && g_compat_game_root.empty()) {
        static bool s_defer_logged = false;
        if (!s_defer_logged) {
            s_defer_logged = true;
            spdlog::info("compat profile: auto 档等待 game_root（不写 "
                         "ogldrawdevice_compat，避免先写先赢把档位锁死）");
        }
        return;
    }

    const CompatProfile *prof = nullptr;
    const char *source = nullptr;
    if (g_compat_request != ENGINE_GAME_COMPAT_PROFILE_AUTO) {
        prof = FindCompatProfile(g_compat_request);
        source = "explicit";
        if (!prof) {
            spdlog::warn("compat profile: 未知档位 '{}'，按默认档处理", g_compat_request);
        }
    } else if (!g_compat_game_root.empty()) {
        const char *detected = DetectCompatProfileByMarkers(g_compat_game_root);
        prof = FindCompatProfile(detected);
        source = "auto";
    }
    if (!prof) {
        prof = FindCompatProfile(ENGINE_GAME_COMPAT_PROFILE_KIRIKIRI2);
        if (!source) source = "default";
    }
    if (!prof) return;

    g_compat_resolved_name = prof->name;
    g_compat_resolved_mode = prof->ogldrawdevice;

    krkr::compat::SetActiveLayer(
        std::string(prof->name) == ENGINE_GAME_COMPAT_PROFILE_AETHERKIRI
            ? krkr::compat::LayerId::AetherKiri
            : krkr::compat::LayerId::Krkr2Classic);
    const std::string resolved_key =
        std::string(prof->name) + "|" + prof->ogldrawdevice + "|" + source + "|" +
        (g_ogldrawdevice_explicit ? "1" : "0") + "|" + g_compat_game_root;
    const bool same_as_last = (resolved_key == s_last_resolved_key);
    s_last_resolved_key = resolved_key;
    if (!same_as_last) {
        spdlog::info("compat profile: {} v{} -> ogldrawdevice_compat={} ({}{})", prof->name,
                     prof->version, prof->ogldrawdevice, source,
                     g_ogldrawdevice_explicit ? ", 但显式选项优先" : "");
    }
    if (g_ogldrawdevice_explicit) return;
    if (!g_compat_game_root.empty() &&
        g_compat_request == ENGINE_GAME_COMPAT_PROFILE_AUTO) {
        if (!same_as_last) {
            spdlog::info("compat profile: 判档依据 game_root={}", g_compat_game_root);
        }
    }
    TVPSetCommandLine(TJS_W("ogldrawdevice_compat"), ttstr(prof->ogldrawdevice).c_str());
}

} // namespace

void ApplyCompatProfile() {
    std::lock_guard<std::mutex> lock(g_compat_mutex);
    ApplyCompatProfileLocked();
}

} // namespace engine_api

extern "C" {

engine_result_t engine_get_compat_profile(char *out_buffer, uint32_t buffer_size) {
    using namespace engine_api;
    if (out_buffer == nullptr || buffer_size == 0) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "out_buffer is null or buffer_size is 0");
    }
    out_buffer[0] = '\0';
    std::lock_guard<std::mutex> lock(g_compat_mutex);
    if (g_compat_resolved_name.empty()) return ENGINE_RESULT_OK;
    std::string text = g_compat_resolved_name;
    if (!g_compat_resolved_mode.empty()) {
        text += ' ';
        text += g_compat_resolved_mode;
    }
    if (text.size() >= buffer_size) text.resize(buffer_size - 1);
    std::memcpy(out_buffer, text.c_str(), text.size() + 1);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif