cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PUBLIC
    scriptsEx.cpp
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    layerExMovie.cpp
    varfile.cpp
    saveStruct.cpp
    getabout.cpp
    addFont.cpp
    wutcwf.cpp
    windowEx.cpp
    getSample.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    xp3filter.cpp
    extrans.cpp
    textrender.cpp
    alphamovie.cpp
    layerExBTOA.cpp
    layerExImage.cpp
    layerExRaster.cpp
    layerExAreaAverage.cpp
    layerExLongExposure.cpp
    krkrgles.cpp
    drawDeviceD2DCompat.cpp
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
    zcompat/zcompat_plugin.cpp
    compatLegacyPlugins.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKR2CORE_PATH}/
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2CORE_PATH}/environ/sdl
    ${KRKR2CORE_PATH}/extension
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/sound/win32
    ${KRKR2CORE_PATH}/tjs2
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/visual
)

add_definitions(-D__STDC_CONSTANT_MACROS -DTJS_TEXT_OUT_CRLF)

# ---------------------------------------------------------------------------
# Live2D Cubism SDK
# ---------------------------------------------------------------------------
# NOTE: cpp/plugins/cubism/{Framework,Core/lib} are gitignored (proprietary
# binary SDK, download from live2d.com — see README.md「硬约束」).
# CI machines do not have them, so Live2D support is built ONLY when the SDK
# is present on disk; otherwise krkrlive2d.cpp is excluded from the plugin
# and CubismFramework becomes an empty INTERFACE target so downstream
# target_link_libraries() calls keep working.
set(CUBISM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cubism)
set(CUBISM_CORE_DIR ${CUBISM_DIR}/Core)
set(CUBISM_FW_DIR ${CUBISM_DIR}/Framework)

file(GLOB_RECURSE CUBISM_FW_SOURCES
    ${CUBISM_FW_DIR}/*.cpp
)
# Exclude non-OpenGL renderers (already removed from tree, but safety net)
list(FILTER CUBISM_FW_SOURCES EXCLUDE REGEX ".*/D3D9/.*|.*/D3D11/.*|.*/Vulkan/.*|.*/Metal/.*|.*/Cocos2d/.*")

set(CUBISM_CORE_LIB "")
if(IOS)
    set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/ios/Release-iphoneos/libLive2DCubismCore.a)
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/x86_64/libLive2DCubismCore.a)
    else()
        set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/arm64/libLive2DCubismCore.a)
    endif()
elseif(ANDROID)
    # Cubism SDK for Native 的 Android 静态库按 ABI 分目录，但不同 SDK 版本的层级
    # 略有差异（有的是 lib/android/<abi>/，有的是 <abi>/Release/），这里逐个候选路径
    # 探测，免得因为目录名不同而**静默**跳过 Live2D——那种失败在真机上只表现为
    # "krkrlive2d.dll Failed"，很难反查到 CMake。
    # 本项目只出 arm64-v8a（见 app/build.gradle.kts 的 abiFilters）。
    foreach(_cubism_cand
            ${CUBISM_CORE_DIR}/lib/android/${ANDROID_ABI}/libLive2DCubismCore.a
            ${CUBISM_CORE_DIR}/lib/android/${ANDROID_ABI}/Release/libLive2DCubismCore.a
            ${CUBISM_CORE_DIR}/lib/${ANDROID_ABI}/libLive2DCubismCore.a)
        if(EXISTS "${_cubism_cand}")
            set(CUBISM_CORE_LIB "${_cubism_cand}")
            break()
        endif()
    endforeach()
    if(CUBISM_CORE_LIB)
        message(STATUS "Live2D: using ${CUBISM_CORE_LIB}")
    else()
        message(STATUS "Live2D: Android ${ANDROID_ABI} 下没找到 libLive2DCubismCore.a，跳过 Live2D（krkrlive2d.cpp 不参与编译）")
    endif()
endif()

if(CUBISM_FW_SOURCES AND CUBISM_CORE_LIB AND EXISTS "${CUBISM_CORE_LIB}")
    add_library(CubismFramework STATIC ${CUBISM_FW_SOURCES})
    target_include_directories(CubismFramework PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_CORE_DIR}/include
    )
    # CSM_TARGET_ANGLE_ES2 是 Cubism 4 时代的渲染器选择宏，**Cubism 5 已经不用它**
    # （实测 CubismSdkForNative-5-r.5 的 Framework/ 里搜不到任何 CSM_TARGET_* 定义，
    # 也没有任何源码读它）。r.5 的渲染器改由"编哪些源文件"决定：上面的
    # GLOB_RECURSE + 排除 D3D9/D3D11/Vulkan/Metal 之后剩下的就是
    # Rendering/OpenGL/*.cpp，即 GLES2 渲染器，正是 Android 要的那条。
    # 定义留着是为了兼容仍在读它的旧版 SDK，是**惰性**的，不要当成渲染器开关。
    target_compile_definitions(CubismFramework PUBLIC CSM_TARGET_ANGLE_ES2)

    # EGL/GLES 头与库来自平台自带：Android 由 NDK sysroot 提供 libEGL.so/libGLESv2.so，
    # Linux 由系统 GLVND/Mesa 提供，均无需额外 find_package。
    target_link_libraries(CubismFramework PRIVATE EGL GLESv2)

    target_link_libraries(CubismFramework PUBLIC ${CUBISM_CORE_LIB})

    # krkrlive2d.cpp (Live2D TJS plugin) compiles only when the SDK is present
    target_sources(${PROJECT_NAME} PRIVATE krkrlive2d.cpp)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_CORE_DIR}/include
    )
    target_compile_definitions(${PROJECT_NAME} PUBLIC CSM_TARGET_ANGLE_ES2)
    # 显式“Live2D 已启用”信号：cubism 缺失时（CI/缺 SDK/被 gitignore）krkrlive2d.cpp
    # 不参与编译，其余引用侧（engine_api / krkrgles）需以此宏保护对
    # TVPRegisterKrkrLive2DPluginAnchor / g_live2dRenderTarget 的引用，避免链接期未定义符号。
    target_compile_definitions(${PROJECT_NAME} PUBLIC KRKR2_LIVE2D)
else()
    # SDK absent on disk (gitignored) — stub the target so downstream links work
    add_library(CubismFramework INTERFACE)
    target_include_directories(CubismFramework INTERFACE ${CUBISM_CORE_DIR}/include)
    message(WARNING "Live2D Cubism SDK not found (cubism/Framework + Core/lib) — Live2D plugin disabled")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC CubismFramework)

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder})
target_link_libraries(${PROJECT_NAME} PUBLIC krkr2core)

add_subdirectory(psdfile)
add_subdirectory(psbfile)
add_subdirectory(layerex_draw)
add_subdirectory(motionplayer)
add_subdirectory(kagparserex)

#add_subdirectory(json)
add_subdirectory(fstat)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        layerExDraw
        motionplayer
        kagparserex
        fstat
#        json steam DrawDeviceForSteam
)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
