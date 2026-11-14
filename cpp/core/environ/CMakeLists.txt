cmake_minimum_required(VERSION 3.19)
project(core_environ_module LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(ENVIRON_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# environ = 四个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
#   intf/      core_environ_intf     契约头（无 .cpp，INTERFACE 库）
#                                    Application / SystemControl / SysInit{Intf,Impl} /
#                                    DetectCPU / Engine{Bootstrap,Loop} / MainScene /
#                                    XP3ArchiveRepack / TVPWindow / EmergencyExit
#                                    → 只依赖 tjs2（DetectCPU.h 与 SystemControl.h 要 utils）
#   sysinit/   core_environ_sysinit  启动序列、平台契约、配置、CPU 检测、诊断上报
#                                    SysInitIntf.cpp / SysInitImpl.cpp / DetectCPU.cpp /
#                                    ConfigManager/* / DumpSend.cpp
#                                    → 依赖 intf + base + visual + io
#   platform/  core_environ_platform 平台实现：android/ sdl/ stubs/ + SystemControl.cpp
#                                    → 依赖 intf + sysinit + visual + base
#   app/       core_environ_app      顶层：Application 与启动流程
#                                    Application / EngineBootstrap / EngineLoop /
#                                    MainScene / XP3ArchiveRepack
#                                    → 依赖上面全部
#
# 这一层划分的依据是**实测 include 图**：所有被外部 include 的头
# （Application.h 20 处、SysInitIntf.h 38 处、SystemControl.h 6 处）自己只 include
# tjsString / tjsTypes / TVPTimer 这类东西，所以它们能被单独放在最下层 —— 这就是
# base README 里那条"接口头放在它服务的那一层，实现放在需要上层的那一层"。
# 反过来，Application.cpp / SysInitImpl.cpp 是整条启动链的最上层：它们要 TJS 类、
# 要 visual、要 io、要 plugin。
#
# 历史：sysinit/ 原本是 core/base 的第四簇（target core_base_sysinit），e1f80ea 之后
# 整簇搬进 environ（docs/STRUCTURE_DESIGN.md §3.3）；win32/ 里的 SystemControl 与
# TVPWindow/EmergencyExit 在这一轮散开：实现进 platform/，头进 intf/，
# 因为 `win32/` 目录名与事实不符（SystemControl.cpp 在 Android 上也编译）。
#
# `core_environ_module` 保留为 INTERFACE 聚合层，所以 base/visual/movie/sound/
# plugins 里的 `target_link_libraries(... core_environ_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# environ 的公共头面。现在 include 语句写的还是裸文件名（"Application.h"），
# 靠这几条 -I 解析；显式路径（environ/intf/Application.h）是下一步的调用点迁移。
# 顺序：模块根 → intf → sysinit（它对外的 SysInitIntf.h 少数同名场景优先）→
# platform → 两个子目录。
set(ENVIRON_HEADERS_DIR
    ${ENVIRON_PATH}
    ${ENVIRON_PATH}/intf
    ${ENVIRON_PATH}/sysinit
    ${ENVIRON_PATH}/platform
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/platform/android>
    ${ENVIRON_PATH}/platform/sdl
)

#---------------------------------------------------------------------------
# intf/ —— 契约头（无源文件，所以是 INTERFACE 库）
#
# 这些头被外部大量 include：
#   Application.h(20) SysInitIntf.h(38) SysInitImpl.h(6) DetectCPU.h(4)
#   SystemControl.h(6) TVPWindow.h(1) PluginImpl.h → 见 plugin 模块
# 它们自己只 include tjsString / tjsTypes / TVPTimer，构成这一层是成立的。
#
# ConfigManager/ 没有一起搬：它的头是靠 "ConfigManager/LocaleConfigManager.h"
# 这条**带子目录的裸路径**被 10 处调用点引用的，把 .h / .cpp 拆到两个目录只会
# 制造两个同名目录；整簇留在 sysinit/ 是登记过的例外。
#---------------------------------------------------------------------------
add_library(core_environ_intf INTERFACE)
target_include_directories(core_environ_intf INTERFACE ${ENVIRON_HEADERS_DIR})
target_link_libraries(core_environ_intf INTERFACE tjs2)

function(environ_add_layer name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${ENVIRON_HEADERS_DIR})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# sysinit/ —— 启动序列 / 配置 / CPU 检测 / 诊断上报
#
# SysInitIntf.cpp 要 ScriptMgnIntf.h + KAGParser.h（base_script）、tvpgl.h（visual）；
# SysInitImpl.cpp 要 GraphicsLoaderIntf/LayerIntf（visual）+ Application.h /
# SystemControl.h（本模块 intf）+ ConfigManager。所以这一簇是 environ 最脏的一层，
# 也正是 base ↔ environ 那条环里 base 侧看到的那一半。
#---------------------------------------------------------------------------
environ_add_layer(core_environ_sysinit
    ${ENVIRON_PATH}/sysinit/SysInitIntf.cpp
    ${ENVIRON_PATH}/sysinit/SysInitImpl.cpp
    ${ENVIRON_PATH}/sysinit/DetectCPU.cpp

    ${ENVIRON_PATH}/sysinit/ConfigManager/GlobalConfigManager.cpp
    ${ENVIRON_PATH}/sysinit/ConfigManager/IndividualConfigManager.cpp
    ${ENVIRON_PATH}/sysinit/ConfigManager/LocaleConfigManager.cpp

    # 诊断上报（dump）：只读 config + StorageImpl，不需要 Application
    ${ENVIRON_PATH}/app/DumpSend.cpp
)

#---------------------------------------------------------------------------
# platform/ —— 平台实现
#
#   android/  KrkrJniHelper + AndroidUtils：JNI 桥（走 KR2Activity/AppContext）
#   sdl/      tvpsdl.cpp：SDL2 平台的 Application 适配（Linux 宿主验证用）
#   stubs/    platform_linux.cpp（非 Android 的平台桩）、ui_stubs.cpp（UI 桩）
#   SystemControl.cpp：音量/亮度/剪贴板/文件选择/CPU clock 这些"系统服务"
#
# 为什么不复用上游 linux/Platform.cpp：它的 #ifdef LINUX 分支在 Android 下为空
# 且强依赖 gtk，不适合移动端。
#---------------------------------------------------------------------------
environ_add_layer(core_environ_platform
    ${ENVIRON_PATH}/platform/SystemControl.cpp

    $<$<BOOL:${MACOS}>:${ENVIRON_PATH}/apple/macos/platform.mm>

    $<$<BOOL:${IOS}>:${ENVIRON_PATH}/apple/ios/platform.mm>

    ${ENVIRON_PATH}/platform/sdl/tvpsdl.cpp

    ${ENVIRON_PATH}/platform/stubs/ui_stubs.cpp

    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/platform/android/KrkrJniHelper.cpp>
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/platform/android/AndroidUtils.cpp>
    $<$<NOT:$<BOOL:${ANDROID}>>:${ENVIRON_PATH}/platform/stubs/platform_linux.cpp>
)

#---------------------------------------------------------------------------
# app/ —— 顶层：Application 对象与启动流程
#
# Application.cpp 是整条启动链最脏的文件：它要 SysInit / ScriptMgn / PluginMgn /
# SystemControl / GraphicsLoadThread / FontImpl / ConfigManager。
# EngineLoop / EngineBootstrap 要 RenderManager 与 EGL/GL，属于启动路径的末端。
#---------------------------------------------------------------------------
environ_add_layer(core_environ_app
    ${ENVIRON_PATH}/app/Application.cpp
    ${ENVIRON_PATH}/app/EngineBootstrap.cpp
    ${ENVIRON_PATH}/app/EngineLoop.cpp
    ${ENVIRON_PATH}/app/MainScene.cpp
    ${ENVIRON_PATH}/app/XP3ArchiveRepack.cpp
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动；链接行保持搬迁前的原样
#---------------------------------------------------------------------------
add_library(core_environ_module INTERFACE)
target_link_libraries(core_environ_module INTERFACE
    core_environ_intf
    core_environ_sysinit
    core_environ_platform
    core_environ_app

    # 搬迁前是 PUBLIC
    tjs2
)
# 搬迁前这些是 PRIVATE（STATIC 库的私有依赖本来就会以 LINK_ONLY 传给最终链接行）
target_link_libraries(core_environ_module INTERFACE
    core_base_module
    core_visual_module
    core_sound_module
    core_plugin_module
    core_movie_module
    core_utils_module
)

find_package(7zip CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(unofficial-minizip CONFIG REQUIRED)

target_link_libraries(core_environ_module INTERFACE
    7zip::7zip
    unofficial::minizip::minizip
    tinyxml2::tinyxml2
)
