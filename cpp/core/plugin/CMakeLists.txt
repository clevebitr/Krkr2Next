cmake_minimum_required(VERSION 3.19)
project(core_plugin_module LANGUAGES CXX)

set(PLUGIN_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# plugin = 三个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
#   host/    core_plugin_host     宿主 API 契约（PluginIntf.h/.cpp）
#                                 TVP_malloc / TVP_free / ZLIB_* / TVP_md5_* /
#                                 TVPRegisterGlobalObject / TVPDoTryBlock …
#                                 → 只依赖 base 的 MsgIntf
#   ncb/     core_plugin_ncb      NCB 框架（ncbind.hpp/.cpp、ncb_invoke.hpp、
#                                 ncb_foreach.inc）
#                                 → 依赖 base（Storage/ScriptMgn/Debug/Msg/CharacterSet）
#                                   + compat/ModuleGate
#   loader/  core_plugin_loader   插件加载器与注册表（PluginImpl.h/.cpp）
#                                 tvpLoadPlugins / TVPRegisteredPlugins /
#                                 TVPResetPluginSystemForRestart / 自动注册
#                                 → 依赖 host + ncb + io + environ
#
# 为什么要单独切出 ncb/：NCB 是宏 + 模板层，`NCB_*` 宏会在**插件作用域**展开
# `template <>` 显式特化（DECISIONS.md「using 救不了显式特化」），所以它的改名
# 风险和目标文件形态都和宿主 API 完全不同。ncbind.hpp 被 50 个插件源文件 include，
# 它一改整仓库都要重编 —— 把它隔离在一个 target 里，至少链接行和 include 面能分开看。
#
# 这一轮只做"目录 + target"，**不动宏**：把 NCB 收成 krkr::plugin::ncb 就是下一步
# （见 cpp/core/plugin/README.md §"没做的"）。
#
# `core_plugin_module` 保留为 INTERFACE 聚合层，所以 base/environ/visual/plugins
# 里的 `target_link_libraries(... core_plugin_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# 公共头面。include 语句仍是裸文件名（"ncbind.hpp" / "PluginImpl.h"），
# 靠这四条 -I 解析（模块根只为兼容 `"plugin/x.h"` 这类写法）。
set(PLUGIN_HEADERS_DIR
        ${PLUGIN_PATH}
        ${PLUGIN_PATH}/host
        ${PLUGIN_PATH}/ncb
        ${PLUGIN_PATH}/loader
)

function(plugin_add_layer name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${PLUGIN_HEADERS_DIR})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# host/ —— 宿主 API：插件看到的那张表
#---------------------------------------------------------------------------
plugin_add_layer(core_plugin_host
        ${PLUGIN_PATH}/host/PluginIntf.cpp
)

#---------------------------------------------------------------------------
# ncb/ —— NCB 框架（宏 + 模板）
#
# ncbind.cpp 只 include ncbind.hpp + compat/layer/ModuleGate.h；真正的宏展开发生在
# 每个插件 TU 里，所以这一簇的 .cpp 很薄，成本全在头文件上。
#---------------------------------------------------------------------------
plugin_add_layer(core_plugin_ncb
        ${PLUGIN_PATH}/ncb/ncbind.cpp
)

#---------------------------------------------------------------------------
# loader/ —— 插件加载器 / 注册表
#
# PluginImpl.h include kmp_pi.h（sound/impl 的 MIDL 头，为了 TKMP_* 的
# acquire/release 声明）—— 这是 plugin → sound 那条边，已知且暂时保留：
# 它是 MIDL 生成的 ABI 头，不属于任何一层的实现。
#---------------------------------------------------------------------------
plugin_add_layer(core_plugin_loader
        ${PLUGIN_PATH}/loader/PluginImpl.cpp
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动；链接行保持搬迁前的原样
#---------------------------------------------------------------------------
add_library(core_plugin_module INTERFACE)
target_link_libraries(core_plugin_module INTERFACE
        core_plugin_host
        core_plugin_ncb
        core_plugin_loader

        # 搬迁前是 PUBLIC
        tjs2
)
# 搬迁前这些是 PRIVATE（STATIC 库的私有依赖本来就会以 LINK_ONLY 传给最终链接行）
target_link_libraries(core_plugin_module INTERFACE
        core_base_module
        core_environ_module
        core_visual_module
        core_utils_module
)
