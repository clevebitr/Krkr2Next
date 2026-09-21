cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# Android-only project。宿主壳是原生 Kotlin/Compose 应用（app/），引擎在 Gradle
# 之外独立构建，产物投放到 app/app/src/main/jniLibs。
# 第三方依赖通过 vcpkg toolchain 提供。
#
# 带守卫：Gradle 的 externalNativeBuild 会传入 NDK 自己的 toolchain file，
# 无条件覆盖会被 CMake 判定为 toolchain 重定义而行为异常。保留守卫以便将来
# 接入 externalNativeBuild，也便于调用方显式指定别的 toolchain。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # Android 走 vcpkg 官方推荐的两段式：CMAKE_TOOLCHAIN_FILE 必须是 vcpkg 自己，
    # NDK 的 android.toolchain.cmake 经 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 串在它后面，
    # 由后者设置 sysroot / 目标三元组 / 编译器。
    #
    # 不能改用「CMAKE_SYSTEM_NAME=Android + CMAKE_ANDROID_NDK + 显式
    # CMAKE_C(XX)_COMPILER=clang/clang++」：一旦显式给了编译器，CMake 的 Android
    # 平台模块就不再把它改写成 NDK 的 clang，于是拿宿主的 /usr/bin/clang++ 去编，
    # 既没有 NDK sysroot（EGL/egl.h 找不到），__ANDROID__ 也没有定义——编出来的是
    # Linux 那套源码，却写进 out/android/。这类错误要到很靠后的编译步骤才炸。
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        if(NOT DEFINED ENV{ANDROID_NDK_HOME})
            message(FATAL_ERROR
                "Android 构建需要 ANDROID_NDK_HOME 环境变量（见 scripts/build_engine_android.sh）")
        endif()
        # C++ 运行时用静态（preset 里显式写死 ANDROID_STL=c++_static）：引擎是**一个**
        # 自包含的 .so，没有共享 libc++ 的理由；而 vcpkg 的端口是按 NDK 默认值编的、
        # 也是静态，混用会让同一个 .so 里出现两份 libc++（链接期被
        # --allow-multiple-definition 盖住，运行期才可能以异常/本地化的怪象暴露）。
        # 另外静态还能省掉随 APK 打包 libc++_shared.so —— 少一个必须正确打包的文件。
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
            "$ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
            CACHE STRING "NDK android.toolchain.cmake, chainloaded by vcpkg")
        message(STATUS
            "Android toolchain: $ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake (chainloaded by vcpkg)")
    endif()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

set(APP_NAME krkr2)

project(${APP_NAME})

# Android 目标自检。上面那段注释描述的退化（宿主 clang、无 sysroot、无
# __ANDROID__）不会让 configure 失败，只会在编译到某个引用了 EGL/GLES 头的
# 文件时才报错——那时已经烧掉大半轮 CI。这里在 configure 期就编译一小段探针，
# 同时验证 __ANDROID__ 与 EGL 头，把这类问题压到几秒内暴露。
if(ANDROID)
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #ifndef __ANDROID__
        #error not an Android target
        #endif
        #include <EGL/egl.h>
        #include <GLES2/gl2.h>
        int main() { return 0; }
    " KRKR_TARGET_IS_ANDROID)
    if(NOT KRKR_TARGET_IS_ANDROID)
        message(FATAL_ERROR
            "Android 目标自检失败：编译器未定义 __ANDROID__ 或找不到 EGL/GLES 头。\n"
            "  CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}\n"
            "  CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}\n"
            "  VCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}\n"
            "Android 构建必须经 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用 NDK 的 "
            "build/cmake/android.toolchain.cmake（见本文件开头）。")
    endif()
endif()

option(ENABLE_TESTS "enable tests (excluded on Android)" ON)
option(BUILD_TOOLS "build tools (excluded on Android)" ON)
option(BUILD_ENGINE_API "build engine_api bridge library" ON)

# 统一诊断探针（渲染、字体、PSB 和 MotionPlayer）编译开关。
# 默认 OFF：release 不含探针、零性能开销与日志刷屏。黑屏/渲染问题排查时
# cmake -DENABLE_RENDER_PROBE=ON 开启。
option(ENABLE_RENDER_PROBE "enable diagnostic probes" OFF)
if(ENABLE_RENDER_PROBE)
    add_compile_definitions(KRKR_RENDER_PROBE)
    message(STATUS "KRKR_RENDER_PROBE: ON (诊断探针已启用)")
else()
    message(STATUS "KRKR_RENDER_PROBE: OFF")
endif()

# 引擎运行时日志级别覆盖（独立于 debug/release）。
# empty=auto：按构建类型默认（Release→info，Debug→debug）。
# 显式设置时覆盖 NDEBUG 推断，经 KRKR_LOG_LEVEL_NUM 传给 engine_api.cpp，
# 供 CI/本地命令行在 workflow 里单独选择日志级别（排查时无需改代码）。
# 取值：trace|debug|info|warn|err|critical|off（大小写不敏感）。
set(KRKR_LOG_LEVEL "" CACHE STRING "engine runtime log level override: trace|debug|info|warn|err|critical|off (empty=auto by build type)")

if(KRKR_LOG_LEVEL)
    string(TOLOWER "${KRKR_LOG_LEVEL}" _krkr_log_level)
    if(_krkr_log_level STREQUAL "auto")
        # "auto" 等价于不设：按构建类型默认（Release→info，Debug→debug），不注入宏
        message(STATUS "KRKR_LOG_LEVEL=auto, keeping build-type default")
    elseif(_krkr_log_level STREQUAL "trace")
        set(_krkr_log_level_num 0)
    elseif(_krkr_log_level STREQUAL "debug")
        set(_krkr_log_level_num 1)
    elseif(_krkr_log_level STREQUAL "warn")
        set(_krkr_log_level_num 3)
    elseif(_krkr_log_level STREQUAL "err" OR _krkr_log_level STREQUAL "error")
        set(_krkr_log_level_num 4)
    elseif(_krkr_log_level STREQUAL "critical")
        set(_krkr_log_level_num 5)
    elseif(_krkr_log_level STREQUAL "off")
        set(_krkr_log_level_num 6)
    else()
        # "info" 或未知值：落为默认 info（spdlog 数值 2）；未知取值仅告警不报错。
        if(NOT _krkr_log_level STREQUAL "info")
            message(WARNING "Unknown KRKR_LOG_LEVEL '${KRKR_LOG_LEVEL}', falling back to info")
        endif()
        set(_krkr_log_level_num 2)
    endif()
    if(DEFINED _krkr_log_level_num)
        add_compile_definitions(KRKR_LOG_LEVEL_NUM=${_krkr_log_level_num})
        message(STATUS "KRKR_LOG_LEVEL=${KRKR_LOG_LEVEL} -> KRKR_LOG_LEVEL_NUM=${_krkr_log_level_num}")
    endif()
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

# 宿主壳是 app/ 下的 Kotlin 应用；这里只构建引擎库。
# engine_api 在 Android 上编译为自包含的 libengine_api.so，由 Kotlin 壳经
# System.loadLibrary + JNI 加载。

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})
if(BUILD_ENGINE_API)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api)
endif()

if(ENABLE_TESTS AND NOT ANDROID)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

if(BUILD_TOOLS AND NOT ANDROID)
    add_subdirectory(tools)
endif()
