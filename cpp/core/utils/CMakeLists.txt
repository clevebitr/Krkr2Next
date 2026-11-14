cmake_minimum_required(VERSION 3.19)
project(core_utils_module LANGUAGES CXX)

set(UTILS_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# utils = 四个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
#   c_api/  core_utils_c_api  C 接口与生成表：md5 / iconv 适配 / 编码转换表
#                             → 不 include 任何 C++ 头，谁都能用
#   misc/   core_utils_misc   纯 C++ 小工具：StringUtil / LogUtil / StallWatchdog /
#                             DeferredDestroyer / ObjectList / MiscUtility /
#                             Random / VelocityTracker / MathAlgorithms / RealFFT
#                             → 只依赖 STL + tjs2 + c_api
#   intf/   core_utils_intf   TJS 接口契约：ThreadIntf / TimerIntf / PadIntf /
#                             ClipboardIntf / DebugIntf / TickCount / AsyncTriggerMode
#                             → 依赖 misc + impl（见下：这是已知的环）
#   impl/   core_utils_impl   平台实现（**原 win32/**）：ThreadImpl / TimerImpl /
#                             PadImpl / ClipboardImpl / DebugImpl / TVPTimer
#                             → 依赖 intf + tjs2
#
# `win32/` 改名 `impl/`：这 9 个文件在 Android 上也全部参与编译
# （DebugImpl / PadImpl / TVPTimer 都在 CMake 的源文件列表里），目录名与事实相反。
# 与 sound（win32 → impl）、environ（win32 解散）是同一批"命名误导"清理。
#
# 与 sound 一样的已知环：`intf/*.h` 在文件中段 include `impl/*.h`
# （ThreadIntf.h:→ThreadImpl.h、TimerIntf.h:→TimerImpl.h、PadIntf.h:→PadImpl.h），
# 因为紧随其后的 tTJSNC_* 要拿 tTJSNI_* 当基类。搬目录不会消掉这条边，
# 拆法见 README.md §"已知的环"。
#
# `core_utils_module` 保留为 INTERFACE 聚合层，所以 base/environ/plugin/visual 里的
# `target_link_libraries(... core_utils_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# 公共头面。include 语句仍是裸文件名（"ThreadIntf.h" / "md5.h"），靠这几条 -I 解析。
# iconv/ 单独进 include 面是历史行为（`#include "iconv.h"` 这种写法），保留。
set(UTILS_HEADERS_DIR
    ${UTILS_PATH}
    ${UTILS_PATH}/c_api
    ${UTILS_PATH}/c_api/iconv
    ${UTILS_PATH}/misc
    ${UTILS_PATH}/intf
    ${UTILS_PATH}/impl
)

function(utils_add_layer name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${UTILS_HEADERS_DIR})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# c_api/ —— C 接口与生成表
#
# md5 是第三方 C 实现（AndroidUtils.cpp / PluginImpl.cpp 也直接 include md5.h）；
# encoding/*.c 是从字符集数据生成的转换表；iconv/ 是我们自己的适配头。
#---------------------------------------------------------------------------
add_library(core_utils_c_api STATIC
    ${UTILS_PATH}/c_api/md5.c
    ${UTILS_PATH}/c_api/encoding/gbk2unicode.c
    ${UTILS_PATH}/c_api/encoding/jis2unicode.c
)
target_include_directories(core_utils_c_api PUBLIC ${UTILS_HEADERS_DIR})

#---------------------------------------------------------------------------
# misc/ —— 纯 C++ 小工具
#
# 这一层刻意不碰 tjsNative：需要 TJS 类的那几个都在 intf/。
# 注意 MathAlgorithms.h 与 sound/common/MathAlgorithms.h 内容重复（登记过的例外），
# 定义只有 core_utils 这一份（sound 侧的 MathAlgorithms.cpp 不在构建里）。
#---------------------------------------------------------------------------
utils_add_layer(core_utils_misc
    ${UTILS_PATH}/misc/MathAlgorithms_Default.cpp
    ${UTILS_PATH}/misc/Random.cpp
    ${UTILS_PATH}/misc/RealFFT_Default.cpp
    ${UTILS_PATH}/misc/VelocityTracker.cpp
)

#---------------------------------------------------------------------------
# intf/ —— TJS 接口契约
#
# TickCount.h 是"头在下层、实现在上层"的典型：头在这里，实现是
# base/runtime/TickCount.cpp（实现侧的 TVPGetRoughTickCount32），所以
# TVPTimer.cpp（impl/）在链接期需要 core_base_module —— 这就是
# "utils 头依赖为零、链接依赖不为零"那条半解（DECISIONS.md #11）。
#---------------------------------------------------------------------------
utils_add_layer(core_utils_intf
    ${UTILS_PATH}/intf/ClipboardIntf.cpp
    ${UTILS_PATH}/intf/PadIntf.cpp
    ${UTILS_PATH}/intf/ThreadIntf.cpp
)

#---------------------------------------------------------------------------
# impl/ —— 平台实现（原 win32/）
#
# DebugImpl.cpp 里的 `TVPGetControllerClass` 是 static（本文件内部），
# 不是跨模块依赖；PadImpl.cpp 里的 `PadFormUnit.h` include 整段在 `#if 0` 里
# （上游文本编辑器遗物），文件在本仓库不存在 —— 清理 #if 0 属于独立一轮。
#---------------------------------------------------------------------------
utils_add_layer(core_utils_impl
    ${UTILS_PATH}/impl/DebugImpl.cpp
    ${UTILS_PATH}/impl/PadImpl.cpp
    ${UTILS_PATH}/impl/TVPTimer.cpp
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动；链接行保持搬迁前的原样
#---------------------------------------------------------------------------
add_library(core_utils_module INTERFACE)
target_link_libraries(core_utils_module INTERFACE
    core_utils_c_api
    core_utils_misc
    core_utils_intf
    core_utils_impl

    # 搬迁前是 PUBLIC
    tjs2
)
# 搬迁前这些是 PRIVATE。真实的链接需求只有一条：TVPTimer.cpp 用
# TVPGetRoughTickCount32（实现在 base/runtime/TickCount.cpp）。core_environ_module
# 这条目前没找到使用者，但**没有链接验证之前不动**（去掉它可能只是少了 --start-group
# 的一次机会，也可能是某个模板实例的来源）。
target_link_libraries(core_utils_module INTERFACE
    core_base_module
    core_environ_module
)
