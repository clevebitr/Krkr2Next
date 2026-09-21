//
// AetherKiri 层伴生脚本 provider 实现。边界与动机见 AetherKiriCompanions.h。
//
#include "AetherKiriCompanions.h"

#include "CompatLayer.h"

#include "base/StorageIntf.h"
#include "io/IoVirtualFile.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>

namespace krkr::compat {
    namespace {

        // ── GPU 伴生脚本占位 ────────────────────────────────────────────────
        // 与上游 `cpp/core/base/impl/GpuCompatScript.h` 的 TVP_GPU_COMPAT_SCRIPT 逐字
        // 一致：链上 krkrgles/krkrlive2d 并定义 KAGWindow 的绘制设备工厂。真正的
        // 接管由 krkrgles 插件在 post-regist 另装（见 cpp/plugins/krkrgles.cpp），
        // 这里只是让游戏 `evalStorage("live2d.tjs")` 之类的调用能拿到合法脚本。
        constexpr char kGpuCompatScript[] =
            "// AetherKiri GPULayer/D3D compatibility placeholder.\n"
            "try { Plugins.link(\"krkrgles.dll\"); } catch(e) { }\n"
            "try { Plugins.link(\"krkrlive2d.dll\"); } catch(e) { }\n"
            "try { Window.OGLDrawDevice = global.OGLDrawDevice; } catch(e) { }\n"
            "try { Window.GLESAdaptor = global.GLESAdaptor; } catch(e) { }\n"
            "function KAGWindow_createDrawDevice() {\n"
            "    var dd = null;\n"
            "    try { dd = new global.OGLDrawDevice(); } catch(e) { try { dd = "
            "new global.GLESAdaptor(); } catch(e2) { dd = null; } }\n"
            "    try { if(dd !== null) dd.setScreenSize(this.width, "
            "this.height); } catch(e) { }\n"
            "    try { this.gpuDrawDevice = dd; } catch(e) { }\n"
            "    try { this.OGLDrawDevice = global.OGLDrawDevice; } catch(e) { "
            "}\n"
            "    try { this.GLESAdaptor = global.GLESAdaptor; } catch(e) { }\n"
            "    try { return new global.Window.BasicDrawDevice(); } catch(e) { "
            "}\n"
            "    try { return new global.Window.PassThroughDrawDevice(); } "
            "catch(e) { }\n"
            "    return null;\n"
            "}\n"
            "try { KAGWindow.KAGWindow_createDrawDevice = "
            "KAGWindow_createDrawDevice; } catch(e) { }\n"
            "try { KAGWindow.prototype.KAGWindow_createDrawDevice = "
            "KAGWindow_createDrawDevice; } catch(e) { }\n"
            "try { KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } "
            "catch(e) { }\n";

        // 与上游 TVPIsGpuCompanionScript 同名同序。
        const char *const kGpuCompanionNames[] = {
            "gpulayer.tjs",
            "gpuaffinelayer.tjs",
            "d3d.tjs",
            "d3daffinesource.tjs",
            "d3daffinesourcepicture.tjs",
            "d3daffinesourceimage.tjs",
            "d3daffinesourcemotion.tjs",
            "d3daffinesourcelive2d.tjs",
            "d3daffinesourceemote.tjs",
            "affinesourcelive2d.tjs",
            "live2d.tjs",
        };

        // 取 basename 并转小写（与上游 TVPIsGpuCompanionScript 同款）。
        std::string ExtractLowerName(const ttstr &name) {
            std::string storage = TVPExtractStorageName(name).AsStdString();
            if(storage.empty()) {
                storage = name.AsStdString();
                const auto slash = storage.find_last_of("/\\");
                if(slash != std::string::npos)
                    storage = storage.substr(slash + 1);
            }
            std::transform(storage.begin(), storage.end(), storage.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return storage;
        }

        bool IsGpuCompanionScript(const ttstr &name) {
            const std::string storage = ExtractLowerName(name);
            for(const char *candidate : kGpuCompanionNames) {
                if(storage == candidate)
                    return true;
            }
            return false;
        }

        // `motion_<inner>.<mtn|psb>.tjs` → `<inner>.<mtn|psb>`。
        bool GetMotionParameterSource(const ttstr &name, ttstr &source) {
            const std::string storage = ExtractLowerName(name);
            if(storage.size() <= 11 || storage.rfind("motion_", 0) != 0 ||
               storage.compare(storage.size() - 4, 4, ".tjs") != 0)
                return false;
            const std::string inner = storage.substr(7, storage.size() - 11);
            const auto dot = inner.rfind('.');
            if(dot == std::string::npos)
                return false;
            const std::string ext = inner.substr(dot);
            if(ext != ".mtn" && ext != ".psb")
                return false;
            source = ttstr(inner.c_str());
            return true;
        }

        // `[dx_]<name>emo.{psb,mtn,mt}`：split-emote 虚拟存储。
        bool IsSplitEmoteVirtualStorage(const ttstr &name) {
            std::string storage = ExtractLowerName(name);
            if(storage.rfind("dx_", 0) == 0)
                storage = storage.substr(3);

            const auto stripSuffix = [](std::string &value,
                                        const std::string &suffix) {
                if(value.size() < suffix.size() ||
                   value.compare(value.size() - suffix.size(), suffix.size(),
                                 suffix) != 0)
                    return false;
                value.resize(value.size() - suffix.size());
                return true;
            };
            if(!stripSuffix(storage, ".mtn") && !stripSuffix(storage, ".psb"))
                stripSuffix(storage, ".mt");
            return storage.size() > 3 &&
                   storage.compare(storage.size() - 3, 3, "emo") == 0;
        }

        std::string EscapeTJSStringLiteral(const std::string &value) {
            std::string escaped;
            escaped.reserve(value.size());
            for(unsigned char ch : value) {
                switch(ch) {
                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\r':
                        escaped += "\\r";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    case '\t':
                        escaped += "\\t";
                        break;
                    default:
                        escaped.push_back(static_cast<char>(ch));
                        break;
                }
            }
            return escaped;
        }

        bool IsAetherKiriLayer() {
            return ActiveLayer() == LayerId::AetherKiri;
        }

        // 每个名字只打一次，且总量封顶：companion 命中是低频事件，但同一脚本会被反复
        // 探测，motion 类名还可能很多；高频日志必须采样/去重（AGENTS §9）。
        void LogCompanionOnce(const char *kind, const std::string &name) {
            static std::mutex mutex;
            static std::map<std::string, bool> emitted;
            constexpr size_t kMaxLogged = 32;
            const std::string key = std::string(kind) + ":" + name;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(emitted[key])
                    return;
                if(emitted.size() >= kMaxLogged)
                    return;
                emitted[key] = true;
            }
            spdlog::info("compat companion: 虚拟提供 {} [{}]", name, kind);
        }

        bool AetherKiriCompanionExists(const ttstr &name) {
            if(!IsAetherKiriLayer())
                return false;
            if(IsGpuCompanionScript(name))
                return true;
            if(IsSplitEmoteVirtualStorage(name))
                return true;
            ttstr source;
            if(GetMotionParameterSource(name, source))
                return !TVPGetPlacedPath(source).IsEmpty();
            return false;
        }

        bool AetherKiriCompanionContent(const ttstr &name,
                                        std::string &content) {
            if(!IsAetherKiriLayer())
                return false;
            if(IsGpuCompanionScript(name)) {
                LogCompanionOnce("gpu-compat-script", ExtractLowerName(name));
                content.assign(kGpuCompatScript, sizeof(kGpuCompatScript) - 1);
                return true;
            }
            if(IsSplitEmoteVirtualStorage(name)) {
                LogCompanionOnce("split-emote", ExtractLowerName(name));
                content.clear(); // 空文件
                return true;
            }
            ttstr source;
            if(GetMotionParameterSource(name, source)) {
                LogCompanionOnce("motion-parameter", ExtractLowerName(name));
                // 让 KAG 的 checkAnimImageData() 保留原文件名，从而把 .PSB 路由到
                // MotionAffineSourceLayer → Motion.EmotePlayer。
                content = "%[\"storage\" => \"" +
                          EscapeTJSStringLiteral(source.AsStdString()) + "\"]";
                return true;
            }
            return false;
        }

    } // namespace

    void RegisterAetherKiriCompanions() {
        io::RegisterVirtualFileProvider(&AetherKiriCompanionExists,
                                        &AetherKiriCompanionContent);
    }

    void UnregisterAetherKiriCompanions() {
        io::UnregisterVirtualFileProvider(&AetherKiriCompanionExists);
    }

    namespace {
        // 静态注册：进程启动即挂上（provider 内部按层判断，classic 层不受影响）。
        struct AetherKiriCompanionRegistrar {
            AetherKiriCompanionRegistrar() { RegisterAetherKiriCompanions(); }
        } g_aetherkiri_companion_registrar;
    } // namespace

} // namespace krkr::compat
