cmake_minimum_required(VERSION 3.19)
project(core_base_module LANGUAGES CXX)

set(BASE_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# base = 四个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
# 目录与 target 一一对应，读目录就知道依赖方向：
#
#   core/      core_base_core     消息 / 流 / 字符集 / 存储门面 / 事件接口
#                                 → 只依赖 tjs2 + utils
#   runtime/   core_base_runtime  线程 / 定时器 / 时钟 / 调试输出 / 事件分发 / 文件选择框
#                                 → 依赖 core + sysinit + environ + visual
#   script/    core_base_script   TJS 脚本引擎 / System 对象 / KAGParser
#                                 → 依赖上述 + visual/sound/plugin/extension/environ
#   sysinit/   core_base_sysinit  启动序列 / 平台契约 / 配置 / CPU 检测
#                                 → 依赖 core + script + visual + environ
#
# 一条贯穿 base 的规矩：**接口头放在它服务的那一层，实现放在需要上层的那一层**。
# 例如 EventIntf.h 在 core/（50 个文件包含它，只需要 tjs2 类型），而 EventIntf.cpp
# 在 runtime/（它要推送事件到 TJS，得看见 ScriptMgnIntf.h）。这不是失误，是这个
# 仓库既有的分工方式（core/utils 的头 + core/base 的实现也是同一套）。
#
# 已知的两条 intra-base 环：script ↔ sysinit（启动时要跑脚本、脚本退出时要通知
# sysinit）。静态库允许环，但它是"下一步要拆的东西"，见 docs/STRUCTURE_DESIGN.md §3.2。
#
# `core_base_module` 保留为 INTERFACE 聚合层，所以 io/environ/visual/plugins 里
# 的 `target_link_libraries(... core_base_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# base 的公共头目录：四个簇都是"公开面"。
# 现在 include 语句写的还是裸文件名（"MsgIntf.h"），靠这四条 -I 解析；等调用点
# 迁移成 "base/core/MsgIntf.h" 这种显式路径后，这里可以收窄成只留门面。
set(BASE_PUBLIC_INCLUDE_DIRS
    ${BASE_PATH}/core
    ${BASE_PATH}/runtime
    ${BASE_PATH}/script
    ${BASE_PATH}/sysinit
)

# 四个簇的共同点：都是 STATIC、都对外暴露 tjs2 类型、共享同一张公共头面。
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
    core_base_sysinit      # SysInitIntf.h / SysInitImpl.h
    core_base_script       # EventIntf.cpp 用 TVPExecuteScript 一侧的声明
    core_utils_module
    core_visual_module     # WindowImpl.h / WindowIntf.h
    core_environ_module    # Application.h / SystemControl.h
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
    core_base_sysinit
    core_utils_module
    core_visual_module
    core_sound_module
    core_plugin_module
    core_extension_module
    core_environ_module
)

#---------------------------------------------------------------------------
# sysinit/ —— 启动路径：初始化序列、平台契约、配置、CPU 检测
#
# 本质属于 environ（DECISIONS.md #6 / STRUCTURE_DESIGN §3.3），等编译验证后
# 整体搬过去，`base ↔ environ` 的环随之消失。
#
# ConfigManager/ 是唯一保留二级目录的子簇：调用点写的是
# "ConfigManager/LocaleConfigManager.h"，保目录名就不用改 10 处调用点。
#---------------------------------------------------------------------------
base_add_cluster(core_base_sysinit
    ${BASE_PATH}/sysinit/SysInitIntf.cpp
    ${BASE_PATH}/sysinit/SysInitImpl.cpp
    ${BASE_PATH}/sysinit/DetectCPU.cpp

    ${BASE_PATH}/sysinit/ConfigManager/GlobalConfigManager.cpp
    ${BASE_PATH}/sysinit/ConfigManager/IndividualConfigManager.cpp
    ${BASE_PATH}/sysinit/ConfigManager/LocaleConfigManager.cpp
)
target_link_libraries(core_base_sysinit PRIVATE
    core_base_core
    core_base_script       # SysInitIntf.cpp 的脚本退出通知（反向边，静态库可环）
    core_utils_module
    core_visual_module     # tvpgl.h / GraphicsLoaderIntf.h / LayerIntf.h
    core_environ_module    # Application.h / SystemControl.h
)

# ConfigManager 用 tinyxml2。之前是靠 core 那一堆 vcpkg 库共享同一个
# include 目录"蹭"到头的，现在显式声明成自己的依赖。
find_package(tinyxml2 CONFIG REQUIRED)
target_link_libraries(core_base_sysinit PRIVATE tinyxml2::tinyxml2)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动
#---------------------------------------------------------------------------
add_library(core_base_module INTERFACE)
target_link_libraries(core_base_module INTERFACE
    core_base_core
    core_base_runtime
    core_base_script
    core_base_sysinit
    core_io_module
)
