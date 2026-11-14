cmake_minimum_required(VERSION 3.19)
project(core_base_module LANGUAGES CXX)

set(BASE_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# base = 四个按依赖层次切开的 target + 一个只做聚合的 INTERFACE 门面
#
# 历史上 base 是一个 60 文件的 STATIC 库，于是"base 依赖 7 个模块"这句话把
# 低层（消息/流）和高层（启动/脚本注册表）的依赖记在了同一个名字上。切完之后：
#
#   core_base_core     消息 / 流 / 字符集 / 存储门面 / 事件接口
#                      → 只依赖 tjs2 + utils
#   core_base_runtime  线程 / 定时器 / 时钟 / 调试输出 / 事件分发 / 文件选择框
#                      → 依赖 core + sysinit + environ + visual
#   core_base_script   TJS 脚本引擎 / System 对象 / KAGParser
#                      → 依赖 core + runtime + sysinit + visual/sound/plugin/extension/environ
#   core_base_sysinit  启动序列 / 平台契约 / 配置 / CPU 检测
#                      → 依赖 core + script + visual + environ
#
# 括号里的依赖就是"读这个 target 需要同时打开哪些目录"，不是链接凑数。
# core 与 runtime 的对外依赖是干净的；script 与 sysinit 是顶层，互相引用
# （script→sysinit 的头、sysinit→script 的启动回调），这是已知的两条环，
# 拆 namespace 之后由 TJS 类注册表注入统一处理（见 docs/STRUCTURE_DESIGN.md §3.2）。
#
# `core_base_module` 保留为 INTERFACE 聚合层，所以 io/environ/visual/plugins 里
# 的 `target_link_libraries(... core_base_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# base 的公共头目录。现在仍是两张扁平面（base/、base/impl），四个簇共用同一张面。
# 下一步按 core/ runtime/ script/ sysinit/ 分目录，这一行会变成四条（见
# docs/STRUCTURE_DESIGN.md §5）。
set(BASE_PUBLIC_INCLUDE_DIRS
    ${BASE_PATH}/
    ${BASE_PATH}/impl
)

# 四个簇的共同点：都是 STATIC、都对外暴露 tjs2 类型、共享同一张公共头面。
function(base_add_cluster name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${BASE_PUBLIC_INCLUDE_DIRS})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# core_base_core —— 低层：谁都能用，它不用别人
#
# 头：MsgIntf.h MsgIntfInc.h impl/MsgImpl.h
#     CharacterSet.h TVPMmapAlloc.h BinaryStream.h TextStream.h UtilStreams.h
#     StorageIntf.h impl/StorageImpl.h impl/FileSelector.h
#     EventIntf.h impl/EventImpl.h UserEvent.h
#     Platform.h ApplicationSpecialPath.h combase.h typedefine.h cpu_types.h vkdefine.h
#---------------------------------------------------------------------------
base_add_cluster(core_base_core
    ${BASE_PATH}/CharacterSet.cpp
    ${BASE_PATH}/BinaryStream.cpp
    ${BASE_PATH}/TextStream.cpp
    ${BASE_PATH}/UtilStreams.cpp
    ${BASE_PATH}/MsgIntf.cpp

    ${BASE_PATH}/impl/MiscUtility.cpp
    ${BASE_PATH}/impl/MsgImpl.cpp
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
# core_base_runtime —— 引擎运行时服务（不是"工具"：要消息、要事件、要 Application）
#
# 头：impl/NativeEventQueue.h（visual / sound 也在用）
#     DebugIntf / ThreadIntf / TimerIntf / TickCount / ClipboardIntf 的接口头在
#     core/utils，实现在这里 —— "头在下层、实现在上层"是本仓库的既定分工。
#---------------------------------------------------------------------------
base_add_cluster(core_base_runtime
    ${BASE_PATH}/EventIntf.cpp

    ${BASE_PATH}/impl/EventImpl.cpp
    ${BASE_PATH}/impl/NativeEventQueue.cpp
    ${BASE_PATH}/impl/FileSelector.cpp

    ${BASE_PATH}/impl/DebugIntf.cpp
    ${BASE_PATH}/impl/ThreadImpl.cpp
    ${BASE_PATH}/impl/ClipboardImpl.cpp
    ${BASE_PATH}/impl/TickCount.cpp
    ${BASE_PATH}/impl/TimerIntf.cpp
    ${BASE_PATH}/impl/TimerImpl.cpp
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
# core_base_script —— TJS 脚本引擎 + System 对象 + KAGParser
#
# 这一簇的跨模块依赖是一张"注册表"：21 个 TVPCreateNativeClass_* 工厂在这里
# 被内联调用（DECISIONS.md #9）。拆环时它会整体反转成 base 留注入口、
# environ 做注册，届时这里的 visual/sound/plugin/extension 依赖一起消失。
#---------------------------------------------------------------------------
base_add_cluster(core_base_script
    ${BASE_PATH}/ScriptMgnIntf.cpp
    ${BASE_PATH}/impl/ScriptMgnImpl.cpp
    ${BASE_PATH}/KAGParser.cpp
    ${BASE_PATH}/SystemIntf.cpp
    ${BASE_PATH}/impl/SystemImpl.cpp
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
# core_base_sysinit —— 启动路径：初始化序列、平台契约、配置、CPU 检测
#
# 本质属于 environ（DECISIONS.md #6 / STRUCTURE_DESIGN §3.3），等编译验证后
# 整体搬过去，`base ↔ environ` 的环随之消失。
#---------------------------------------------------------------------------
base_add_cluster(core_base_sysinit
    ${BASE_PATH}/SysInitIntf.cpp
    ${BASE_PATH}/impl/SysInitImpl.cpp
    ${BASE_PATH}/DetectCPU.cpp

    ${BASE_PATH}/ConfigManager/GlobalConfigManager.cpp
    ${BASE_PATH}/ConfigManager/IndividualConfigManager.cpp
    ${BASE_PATH}/ConfigManager/LocaleConfigManager.cpp
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
