//
// 模块归属门实现（见 ModuleGate.h）。
//
#include "ModuleGate.h"

#include <cctype>
#include <map>
#include <string>
#include <spdlog/spdlog.h>

namespace krkr::compat {

    namespace {
        // 模块名（小写）→ 归属层。
        std::map<std::string, LayerId> &Owners() {
            static std::map<std::string, LayerId> owners;
            return owners;
        }

        std::string LowerAscii(const ttstr &name) {
            std::string s = name.AsLowerCase().AsStdString();
            return s;
        }
    } // namespace

    void RegisterModuleOwner(const char *moduleName, LayerId layer) {
        if(!moduleName || !*moduleName)
            return;
        std::string key(moduleName);
        for(char &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto &owners = Owners();
        auto it = owners.find(key);
        if(it != owners.end()) {
            if(it->second != layer) {
                if(auto logger = spdlog::get("core")) {
                    logger->error(
                        "module gate: 模块 '{}' 被登记为两层（先={} 后={}），保留先登记者",
                        key, LayerName(it->second), LayerName(layer));
                }
            }
            return;
        }
        owners.emplace(key, layer);
    }

    bool AllowModuleLoad(const ttstr &moduleName) {
        const std::string key = LowerAscii(moduleName);
        const auto &owners = Owners();
        auto it = owners.find(key);
        if(it == owners.end())
            return true; // 未登记 = 不限制（旧层现状）

        const LayerId active = ActiveLayer();
        if(it->second == active)
            return true;

        if(auto logger = spdlog::get("core")) {
            logger->info("module gate: 模块 '{}' 属于 {} 层，当前激活 {} 层 ⇒ 跳过注册",
                         key, LayerName(it->second), LayerName(active));
        }
        return false;
    }

} // namespace krkr::compat
