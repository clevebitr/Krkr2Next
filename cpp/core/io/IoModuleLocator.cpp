//
// 模块存在性查询的注入点实现（见 IoModuleLocator.h）。
//
#include "IoModuleLocator.h"

#include <spdlog/spdlog.h>

namespace krkr::io {

    namespace {
        ModuleQueryFn g_moduleQuery = nullptr;
    } // namespace

    void SetModuleLocator(ModuleQueryFn fn) {
        g_moduleQuery = fn;
        if(auto logger = spdlog::get("core")) {
            logger->debug("io: 模块定位查询已{}", fn ? "注入" : "清空");
        }
    }

    bool HasModule(const ttstr &name) {
        return g_moduleQuery ? g_moduleQuery(name) : false;
    }

} // namespace krkr::io
