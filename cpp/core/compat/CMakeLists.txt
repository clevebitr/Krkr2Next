cmake_minimum_required(VERSION 3.19)
project(core_compat_module LANGUAGES CXX)

set(COMPAT_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# compat = 两层 + 一个 INTERFACE 聚合层
#
#   layer/       core_compat_layer   兼容层定义与激活 + 模块门控
#                                    CompatLayer.{h,cpp}（层 id / IO 策略接线）
#                                    ModuleGate.{h,cpp}（某个模块在当前层生不生效）
#                                    → 只依赖 io 的策略头 + tjs2
#   companions/  core_compat_companions  层专属的虚拟文件 provider 与内嵌资源
#                                    AetherKiriCompanions.{h,cpp}（GPU 伴生脚本等）
#                                    resources/D3DEmote.tjs（configure_file 生成字节数组）
#                                    → 依赖 layer + base 的 StorageIntf + io 的注册点
#
# 这两个目录对应 compat/README.md §1 讲的三块里的后两块：
# "层选择链路"（layer/）与"层专属内容"（companions/），第三块是 io/ 自己的实现。
#
# 拆分依据是实测 include 图：Companions.cpp 要 base 的 StorageIntf 与 io 的
# IoVirtualFile；而 CompatLayer.cpp 只 include CompatLayer.h / Companions.h /
# IoPolicy.h —— 也就是**策略不需要认识内容**，内容需要认识策略。
# 以前这三件事在一个 target 里，构建期分不开"谁把 base 拖进来了"。
#
# `core_compat_module` 保留为 INTERFACE 聚合层，所以 base/plugin/plugins/bridge
# 里的 `target_link_libraries(... core_compat_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# D3DEmote.tjs 嵌入（方案 B：用参考实现的脚本覆盖游戏的 `system/motion.tjs`）
#
# 为什么用 configure_file 生成字节数组而不是手写 C++ 字符串字面量：1340 行日文脚本，
# 转义一旦写错就是静默的语法错误；hex 字节数组不会错，也不受源文件编码影响。
# 与 AetherKiri `cpp/core/base/CMakeLists.txt` 同一做法。
# ---------------------------------------------------------------------------
set(COMPAT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(COMPAT_D3DEMOTE_RESOURCE ${COMPAT_PATH}/companions/resources/D3DEmote.tjs)
set(COMPAT_D3DEMOTE_SOURCE ${COMPAT_GENERATED_DIR}/D3DEmote_tjs.cpp)
file(MAKE_DIRECTORY ${COMPAT_GENERATED_DIR})
file(READ ${COMPAT_D3DEMOTE_RESOURCE} COMPAT_D3DEMOTE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
       COMPAT_D3DEMOTE_BYTES "${COMPAT_D3DEMOTE_HEX}")
configure_file(${COMPAT_PATH}/companions/resources/D3DEmote_tjs.cpp.in
               ${COMPAT_D3DEMOTE_SOURCE} @ONLY)

# 公共头面。include 语句仍是裸文件名（"CompatLayer.h" / "ModuleGate.h"）。
# 注意：改这两个目录会波及 7 处显式路径调用点（plugins/krkrgles.cpp、
# plugins/krkrlive2d.cpp、plugins/compat/aetherkiri/*.cpp、
# plugin/ncb/ncbind.cpp、bridge/engine_api/src/engine_api_internal.h），
# 见 cpp/core/compat/README.md §"显式路径"。
#
# 自身要能解析 `io/policy/StoragePolicy.h`：cpp/core 必须在本模块的包含路径里。
# （krkr2core 的 INTERFACE 包含路径只作用于链接它的目标，编译本模块时不可见 ——
#  CI 35335370234 就是栽在这一条上。）
add_library(core_compat_headers INTERFACE)
target_include_directories(core_compat_headers INTERFACE
    ${COMPAT_PATH}
    ${COMPAT_PATH}/layer
    ${COMPAT_PATH}/companions
    ${COMPAT_PATH}/..
)

#---------------------------------------------------------------------------
# layer/ —— 层定义、激活、门控
#---------------------------------------------------------------------------
add_library(core_compat_layer STATIC
    ${COMPAT_PATH}/layer/CompatLayer.cpp
    ${COMPAT_PATH}/layer/ModuleGate.cpp
)
target_include_directories(core_compat_layer PUBLIC
    ${COMPAT_PATH}
    ${COMPAT_PATH}/layer
    ${COMPAT_PATH}/companions
)
target_link_libraries(core_compat_layer PUBLIC tjs2 core_compat_headers)

#---------------------------------------------------------------------------
# companions/ —— 层专属内容（虚拟文件 provider + 内嵌资源）
#
# provider 只产出内容，由 io 包成流（io/storage/IoVirtualFile.h 的注册点）。
# 这一簇是 compat 唯一合法地 include base/io 头的对方。
#---------------------------------------------------------------------------
add_library(core_compat_companions STATIC
    ${COMPAT_PATH}/companions/AetherKiriCompanions.cpp
    # 由上面的 configure_file 生成（D3DEmote.tjs 的字节数组）。
    ${COMPAT_D3DEMOTE_SOURCE}
)
target_include_directories(core_compat_companions PUBLIC
    ${COMPAT_PATH}
    ${COMPAT_PATH}/layer
    ${COMPAT_PATH}/companions
)
target_link_libraries(core_compat_companions PUBLIC tjs2 core_compat_headers PRIVATE core_compat_layer)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动
#
# 只依赖 tjs2（spdlog 由 tjs2 传递提供）。本模块不允许依赖 krkr2plugin、
# cpp/core/io 的**实现细节**或任何具体游戏/发行版代码（见 compat/README.md §2）；
# 但允许经 io 的**接口**（io/storage/IoVirtualFile.h 的注册点）贡献虚拟文件。
# ---------------------------------------------------------------------------
add_library(core_compat_module INTERFACE)
target_link_libraries(core_compat_module INTERFACE
    core_compat_headers
    core_compat_layer
    core_compat_companions
    tjs2
)
