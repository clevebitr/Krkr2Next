cmake_minimum_required(VERSION 3.19)
project(core_sound_module LANGUAGES CXX)

set(SOUND_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# sound = 四个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
# 目录与 target 一一对应，读目录名就知道依赖方向：
#
#   common/  core_sound_common  纯数据与纯算法：RingBuffer / WaveSegmentQueue /
#                               WaveLoopManager 的头 / xmmlib / WaveFormatConverter
#                               → 只依赖 tjs2
#   intf/    core_sound_intf    TJS 接口类：WaveIntf / SoundBufferBaseIntf /
#                               CDDAIntf / MIDIIntf（base/script、visual、plugins
#                               直接 include 的就是这几个头）
#                               → 依赖 common + base
#   codec/   core_sound_codec   解码器与 DSP：FFWaveDecoder / VorbisWaveDecoder /
#                               PhaseVocoder{DSP,Filter}
#                               → 依赖 intf（解码器返回 tTVPWaveDecoder）
#   impl/    core_sound_impl    平台实现：WaveImpl / WaveMixer / SoundBufferBaseImpl /
#                               CDDAImpl / MIDIImpl / tvpsnd（原 win32/）
#                               → 依赖 intf + codec + base + environ
#
# 第三层原来叫 `win32/`，但它在 **Android 上也全部参与编译**（只有 tvpsnd.cpp 的
# 内容接近 Windows COM ABI），目录名与事实相反，是 TODO.md 里点名的"命名误导"。
# 本轮改名为 `impl/`：它表达的是"接口背后的实现"，不是"某个平台"。
#
# 与 base 的差别（有意为之）：base 把链接行写在各簇上，簇之间已经存在声明式的环。
# sound 的环（见下）只出现在 include 方向，链接行集中在聚合层更清楚，也不会新增
# target 环。
#
# `core_sound_module` 保留为 INTERFACE 聚合层，所以 base/visual/movie/environ
# 里的 `target_link_libraries(... core_sound_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# sound 的公共头面：四层都是"公开面"。现在 include 语句写的还是裸文件名
# （"WaveIntf.h"），靠这五条 -I 解析（第一条是模块根，只为兼容 `"sound/x.h"`
# 这类写法）。等调用点迁移成显式路径后可以收窄。
set(SOUND_LAYER_INCLUDE_DIRS
    ${SOUND_PATH}
    ${SOUND_PATH}/common
    ${SOUND_PATH}/intf
    ${SOUND_PATH}/codec
    ${SOUND_PATH}/impl
)

# 四个簇的共同点：都是 STATIC、都对外暴露 tjs2 类型、共享同一张公共头面。
function(sound_add_layer name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${SOUND_LAYER_INCLUDE_DIRS})
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# common/ —— 纯数据与纯算法
#
# WaveLoopManager 的头在这里、.cpp 在 intf/：它的实现要 tTVPWaveFormat
# （WaveIntf.h），而 header 只用到 WaveSegmentQueue。这条"头在下层、实现在上层"
# 和 base 的 EventIntf.h / EventIntf.cpp 是同一套分工。
#---------------------------------------------------------------------------
sound_add_layer(core_sound_common
    ${SOUND_PATH}/common/WaveSegmentQueue.cpp
    ${SOUND_PATH}/common/WaveFormatConverter.cpp
    # 以下两个**不在构建里**（保持搬迁前的状态，没有编译验证前不打开）：
    #   MathAlgorithms.cpp         : 与 core_utils_module/MathAlgorithms_Default.cpp
    #                                重复定义（utils 那份通过 tjs2 依赖已链进来）
    #   WaveFormatConverter_SSE.cpp : 历史上被注释掉
    #   xmmlib.cpp                 : 历史上被注释掉（xmmlib.h 仍被 MathAlgorithms.h 用）
)

#---------------------------------------------------------------------------
# intf/ —— TJS 接口层
#
# 这一层的四个头被外部直接 include：
#   base/script/ScriptMgnIntf.cpp : CDDAIntf.h / MIDIIntf.h / WaveIntf.h
#   visual/VideoOvlIntf.h         : SoundBufferBaseIntf.h
#   visual/impl/VideoOvlImpl.cpp  : WaveImpl.h（impl/）
#   plugins/wutcwf.cpp            : WaveIntf.h
#
# 注意：WaveIntf.h / SoundBufferBaseIntf.h / CDDAIntf.h / MIDIIntf.h 在**文件中段**
# include 了 impl/ 的实现头（"must define tTJSNI_XXX class"），因为紧随其后的
# tTJSNC_XXX 要拿 tTJSNI_XXX 当基类。这就是 intf ↔ impl 环的根因，拆法见
# README.md §"已知的环"。
#---------------------------------------------------------------------------
sound_add_layer(core_sound_intf
    ${SOUND_PATH}/intf/CDDAIntf.cpp
    ${SOUND_PATH}/intf/MIDIIntf.cpp
    ${SOUND_PATH}/intf/SoundBufferBaseIntf.cpp
    ${SOUND_PATH}/intf/WaveIntf.cpp
    ${SOUND_PATH}/intf/WaveLoopManager.cpp
)

#---------------------------------------------------------------------------
# codec/ —— 解码器与 DSP
#
# FFWaveDecoder 依赖 libav*（movie 模块也用它，见 DECISIONS.md "sound → movie
# 头依赖清零但链接期仍调用 TVPInitLibAVCodec" 那条半解）；Vorbis 走 vcpkg。
#---------------------------------------------------------------------------
sound_add_layer(core_sound_codec
    ${SOUND_PATH}/codec/FFWaveDecoder.cpp
    ${SOUND_PATH}/codec/VorbisWaveDecoder.cpp
    ${SOUND_PATH}/codec/PhaseVocoderDSP.cpp
    ${SOUND_PATH}/codec/PhaseVocoderFilter.cpp
)

#---------------------------------------------------------------------------
# impl/ —— 平台实现（原 win32/）
#
# WaveImpl.cpp 是这一层最脏的文件：它要 SystemControl / Application /
# NativeEventQueue / UserEvent / TVPTimer，是 sound → environ 的主要来源。
#---------------------------------------------------------------------------
sound_add_layer(core_sound_impl
    ${SOUND_PATH}/impl/CDDAImpl.cpp
    ${SOUND_PATH}/impl/MIDIImpl.cpp
    ${SOUND_PATH}/impl/SoundBufferBaseImpl.cpp
    ${SOUND_PATH}/impl/WaveImpl.cpp
    ${SOUND_PATH}/impl/WaveMixer.cpp
    ${SOUND_PATH}/impl/tvpsnd.cpp
    # 不在构建里（保持搬迁前状态）：
    #   tvpsnd.c / tvpsnd.idl : MIDL 生成的 COM ABI，Android 不使用
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动；链接行保持搬迁前的原样
#---------------------------------------------------------------------------
add_library(core_sound_module INTERFACE)
target_link_libraries(core_sound_module INTERFACE
    core_sound_common
    core_sound_intf
    core_sound_codec
    core_sound_impl

    # 搬迁前是 PUBLIC
    tjs2
)
# 搬迁前这些是 PRIVATE：STATIC 库的私有依赖本来就会以 LINK_ONLY 传给最终链接行，
# 集中放在聚合层与放在簇上是等价的，这里只是更好对照搬迁前的写法。
target_link_libraries(core_sound_module INTERFACE
    core_base_module
    core_movie_module
    core_utils_module
    core_environ_module
)

find_package(Vorbis CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)

find_package(OpenAL CONFIG REQUIRED)

# WaveMixer.cpp 在 Android 上用 oboe 作为低延迟音频渲染后端（头文件 oboe/Oboe.h，
# 类 tTVPAudioRendererOboe），运行时失败回退到 tTVPAudioRendererAL（OpenAL）。
# 依赖已加在 vcpkg.json（platform=android）。
#
# 注意：vcpkg 的 oboe 端口（1.8.0）只提供 pkg-config、不输出 CMake config 文件，
# 因此不能用 find_package(oboe CONFIG)；头文件 oboe/Oboe.h 已在 vcpkg_installed/include
# 全局 include 路径里，这里直接用 find_library 定位静态库 liboboe.a 并链接。
# oboe 的 OpenSLES 后端还需要链接系统平台库 libOpenSLES.so。
if(ANDROID)
    find_library(OBOE_STATIC_LIB oboe)
    if(NOT OBOE_STATIC_LIB)
        message(FATAL_ERROR "oboe static library not found via find_library(oboe); check vcpkg oboe install")
    endif()
endif()

target_link_libraries(core_sound_module INTERFACE
    Vorbis::vorbis
    Vorbis::vorbisfile
    Vorbis::vorbisenc

    Opus::opus
    OpusFile::opusfile
    OpenAL::OpenAL
)

if(ANDROID)
    target_link_libraries(core_sound_module INTERFACE ${OBOE_STATIC_LIB} OpenSLES)
endif()
