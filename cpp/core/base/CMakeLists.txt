cmake_minimum_required(VERSION 3.19)
project(core_base_module LANGUAGES CXX)

set(BASE_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# base = 三个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
# 目录与 target 一一对应，读目录就知道依赖方向：
#
#   core/      core_base_core     消息 / 流 / 字符集 / 存储门面 / 事件接口
#                                 → 只依赖 tjs2 + utils
#   runtime/   core_base_runtime  线程 / 定时器 / 时钟 / 调试输出 / 事件分发 / 文件选择框
#                                 → 依赖 core + environ + visual
#   script/    core_base_script   TJS 脚本引擎 / System 对象 / KAGParser
#                                 → 依赖上述 + visual/sound/plugin/extension/environ
#
# 原来的第四簇 sysinit（启动序列 / 平台契约 / 配置 / CPU 检测）已经整体搬进
# cpp/core/environ/sysinit —— 它本来就是启动路径，放在 base 里只会让
# "base 依赖 environ" 记错账（见 docs/STRUCTURE_DESIGN.md §3.3）。
#
# 一条贯穿 base 的规矩：**接口头放在它服务的那一层，实现放在需要上层的那一层**。
# 例如 EventIntf.h 在 core/（50 个文件包含它，只需要 tjs2 类型），而 EventIntf.cpp
# 在 runtime/（它要推送事件到 TJS，得看见 ScriptMgnIntf.h）。这不是失误，是这个
# 仓库既有的分工方式（core/utils 的头 + core/base 的实现也是同一套）。
#
# 搬走 sysinit 后 intra-base 只剩一条环：script ↔ environ（启动时要跑脚本，
# 脚本退出时要通知 environ/sysinit；ScriptMgnIntf.cpp 反过来要 Application.h）。
# 静态库允许环，但它挡住了 "只看 base 就理解 base"，拆法见 STRUCTURE_DESIGN §3.2。
#
# `core_base_module` 保留为 INTERFACE 聚合层，所以 io/environ/visual/plugins 里
# 的 `target_link_libraries(... core_base_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# base 的公共头目录：三个簇都是"公开面"。
# 现在 include 语句写的还是裸文件名（"MsgIntf.h"），靠这三条 -I 解析；等调用点
# 迁移成 "base/core/MsgIntf.h" 这种显式路径后，这里可以收窄成只留门面。
set(BASE_PUBLIC_INCLUDE_DIRS
    ${BASE_PATH}/core
    ${BASE_PATH}/runtime
    ${BASE_PATH}/script
)

# 三个簇的共同点：都是 STATIC、都对外暴露 tjs2 类型、共享同一张公共头面。
function(base_add_cluster name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${BASE_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# core/ —— 低层：谁都能用，它不用别人
#
# 头：MsgIntf.h MsgIntfInc.h MsgImpl.h
#     CharacterSet.h TVPMmapAlloc.h BinaryStream.h TextStream.h UtilStreams.h
#     StorageIntf.h StorageImpl.h FileSelector.h
#     EventIntf.h EventImpl.h UserEvent.h
#     Platform.h ApplicationSpecialPath.h combase.h typedefine.h cpu_types.h vkdefine.h
#---------------------------------------------------------------------------
base_add_cluster(core_base_core
    ${BASE_PATH}/core/CharacterSet.cpp
    ${BASE_PATH}/core/BinaryStream.cpp
    ${BASE_PATH}/core/TextStream.cpp
    ${BASE_PATH}/core/UtilStreams.cpp
    ${BASE_PATH}/core/MsgIntf.cpp
    ${BASE_PATH}/core/MiscUtility.cpp
    ${BASE_PATH}/core/MsgImpl.cpp
)
target_link_libraries(core_base_core PRIVATE core_utils_module)

# 第三方库集中在 core 这一簇：
#   uchardet   → TextStream.cpp（编码检测）
#   LibArchive → UtilStreams.cpp（<archive.h>）
#   ZLIB       → BinaryStream.cpp（<zlib.h>）
# 其余几个（unrar / LibXml2 / SDL2 / zstd / minizip）base 自己并不 include，
# 但它们历史上挂在 base 的 PRIVATE 链接上，符号被整个引擎共享
# （例如 SDL2 的符号实际由 sound/win32/WaveMixer.cpp 使用）。在拿到编译验证
# 之前按原样保留，避免静默改变最终链接行。
find_package(uchardet CONFIG REQUIRED)
find_package(unrar CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(unofficial-minizip CONFIG REQUIRED)
target_link_libraries(core_base_core PRIVATE
    uchardet::libuchardet
    unrar::unrar
    LibArchive::LibArchive
    LibXml2::LibXml2
    zstd::libzstd
    ZLIB::ZLIB

    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    unofficial::minizip::minizip
)

#---------------------------------------------------------------------------
# runtime/ —— 引擎运行时服务（不是"工具"：要消息、要事件、要 Application）
#
# 这里放的是"接口头在 core/ 或 core/utils、实现必须看见上层"的那批文件：
# EventIntf.cpp / EventImpl.cpp / FileSelector.cpp，加上 DebugIntf、ThreadImpl、
# ClipboardImpl、TickCount、Timer* —— 这些在 DECISIONS.md #12 里被从 utils 搬过来，
# 因为它们的实现要用 MsgIntf / SysInitIntf / EventIntf / SystemControl。
#
# 头：NativeEventQueue.h（visual / sound 也在用）
#---------------------------------------------------------------------------
base_add_cluster(core_base_runtime
    ${BASE_PATH}/runtime/EventIntf.cpp
    ${BASE_PATH}/runtime/EventImpl.cpp
    ${BASE_PATH}/runtime/NativeEventQueue.cpp
    ${BASE_PATH}/runtime/FileSelector.cpp

    ${BASE_PATH}/runtime/DebugIntf.cpp
    ${BASE_PATH}/runtime/ThreadImpl.cpp
    ${BASE_PATH}/runtime/ClipboardImpl.cpp
    ${BASE_PATH}/runtime/TickCount.cpp
    ${BASE_PATH}/runtime/TimerIntf.cpp
    ${BASE_PATH}/runtime/TimerImpl.cpp
)
target_link_libraries(core_base_runtime PRIVATE
    core_base_core
    core_base_script       # EventIntf.cpp 用 TVPExecuteScript 一侧的声明
    core_utils_module
    core_visual_module     # WindowImpl.h / WindowIntf.h
    core_environ_module    # Application.h / SystemControl.h / sysinit/SysInitIntf.h
)

#---------------------------------------------------------------------------
# script/ —— TJS 脚本引擎 + System 对象 + KAGParser
#
# 这一簇的跨模块依赖是一张"注册表"：21 个 TVPCreateNativeClass_* 工厂在这里
# 被内联调用（DECISIONS.md #9）。拆环时它会整体反转成 base 留注入口、
# environ 做注册，届时这里的 visual/sound/plugin/extension 依赖一起消失。
#---------------------------------------------------------------------------
base_add_cluster(core_base_script
    ${BASE_PATH}/script/ScriptMgnIntf.cpp
    ${BASE_PATH}/script/ScriptMgnImpl.cpp
    ${BASE_PATH}/script/KAGParser.cpp
    ${BASE_PATH}/script/SystemIntf.cpp
    ${BASE_PATH}/script/SystemImpl.cpp
)
target_link_libraries(core_base_script PRIVATE
    core_base_core
    core_base_runtime
    core_utils_module
    core_visual_module
    core_sound_module
    core_plugin_module
    core_extension_module
    core_environ_module
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动
#---------------------------------------------------------------------------
add_library(core_base_module INTERFACE)
target_link_libraries(core_base_module INTERFACE
    core_base_core
    core_base_runtime
    core_base_script
    core_io_module
)
