cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PUBLIC
    scriptsEx.cpp
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    layerExMovie.cpp
    kirikiroid2.cpp
    base64.cpp
    json/jsonPlugin.cpp
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
    extrans_precise/wave.cpp
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
    # Krkr2Compat 纯 TJS 兼容层（k2compat.dll）。默认不执行，只有显式开启引擎
    # 选项 k2compat_scripts=1 时才在框架就绪后安装（见 zcompat_plugin.cpp）。
    zcompat/k2compat_scripts.cpp
    # AetherKiri 层专属的插件模拟（层归属见 compat/ModuleGate.h；来源与改动见
    # compat/upstream/aetherkiri_ports.json）。
    compat/aetherkiri/legacy_zlib_version.cpp
    compat/aetherkiri/legacy_system_misc.cpp
    compat/aetherkiri/legacy_system_env.cpp
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

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)

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
    # CSM_TARGET_ANDROID_ES2 是 r.5 的**渲染器平台开关**，必须定义——实测
    # Framework/ 里有 13 处读它（Rendering/OpenGL/ 下的 Renderer/Shader/
    # RenderTarget/OffscreenRenderTarget 等），不定义就会：
    #   1. CubismRenderTarget_OpenGLES2.hpp 的 GLES2 头整段被跳过 → GLint/GLuint
    #      未声明 → 编译失败（CI 实测 TU 342/815）；
    #   2. 即便编过，CopyRenderTarget() 会走 #if !ANDROID && !IPHONE 那条**非**
    #      Android 分支，而该分支用的 _modelRenderTargets[1] 只有在 ANDROID/IPHONE
    #      下才创建 → 离屏合成路径不可用。
    # 之前这里定义的是 CSM_TARGET_ANGLE_ES2：那是 Cubism 4 时代的宏，实测 r.5 的
    # Framework/ 与 Core/ **零处**读它，属于完全惰性、且掩盖了上述两个问题。
    # 本工程只出 Android（见 app/app/build.gradle.kts 的 abiFilters），故直接定义。
    target_compile_definitions(CubismFramework PUBLIC CSM_TARGET_ANDROID_ES2)

    # EGL/GLES 头与库来自平台自带：Android 由 NDK sysroot 提供 libEGL.so/libGLESv2.so，
    # Linux 由系统 GLVND/Mesa 提供，均无需额外 find_package。
    target_link_libraries(CubismFramework PRIVATE EGL GLESv2)

    target_link_libraries(CubismFramework PUBLIC ${CUBISM_CORE_LIB})

    # krkrlive2d.cpp (Live2D TJS plugin) compiles only when the SDK is present
    target_sources(${PROJECT_NAME} PRIVATE krkrlive2d.cpp)

    # ── 嵌 GLES2 着色器 ────────────────────────────────────────────────────
    # Cubism 的 GLES2 渲染器不内背着色器：GenerateShaders() 通过
    # CubismFramework::Option::LoadFileFunction 回调向宿主索取
    # （LoadShaderProgramFromFile("VertShaderSrc.vert", "FragShaderSrc.frag")）。
    # 本插件此前没设该回调，StartUp() 存下的是未初始化的 Option → 真机一建
    # 立绘模型就 SIGSEGV。这里把 SDK 的 StandardES/*（_ES2 变体，与
    # CSM_TARGET_ANDROID_ES2 对应）在 **configure 期**嵌成 C++ 表，
    # 由 krkrlive2d.cpp 注册的 loader 直接取材，与文件系统解耦。
    set(CUBISM_SHADER_DIR ${CUBISM_FW_DIR}/Rendering/OpenGL/Shaders/StandardES)
    if(NOT EXISTS ${CUBISM_SHADER_DIR})
        message(FATAL_ERROR
            "Live2D: 找不到着色器目录 ${CUBISM_SHADER_DIR}。\n"
            "  SDK 需要在位（scripts/restore_cubism_sdk.sh），且该目录下应有 "
            "VertShaderSrc.vert / FragShaderSrc.frag 等 36 个文件。")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CUBISM_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(CUBISM_SHADER_GEN ${CUBISM_SHADER_GEN_DIR}/embedded_shaders.cpp)
    file(MAKE_DIRECTORY ${CUBISM_SHADER_GEN_DIR})
    add_custom_command(
        OUTPUT ${CUBISM_SHADER_GEN}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/gen_embedded_shaders.py
                ${CUBISM_SHADER_DIR} ${CUBISM_SHADER_GEN}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/gen_embedded_shaders.py
                ${CUBISM_SHADER_DIR}
        COMMENT "嵌入 Cubism GLES2 着色器 -> embedded_shaders.cpp"
        VERBATIM
    )
    # 首次 configure 后立刻生成一次：否则依赖它的 .o 在首次构建时可能先于
    # 自定义命令被调度（自定义命令的输出还不是已存在的文件）。
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/gen_embedded_shaders.py
                ${CUBISM_SHADER_DIR} ${CUBISM_SHADER_GEN}
        RESULT_VARIABLE _shader_gen_rc
    )
    if(NOT _shader_gen_rc EQUAL 0)
        message(FATAL_ERROR "Live2D: 嵌入着色器失败（gen_embedded_shaders.py 退出 ${_shader_gen_rc}）")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE ${CUBISM_SHADER_GEN})

    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_CORE_DIR}/include
    )
    # 与 CubismFramework 用同一个平台宏，原因见上面那段注释。
    target_compile_definitions(${PROJECT_NAME} PUBLIC CSM_TARGET_ANDROID_ES2)
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

#add_subdirectory(json)   # 子目录写法在 krkr2/PocketKrKr/KrKr2-Next/AetherKiri 全是注释的；
# AetherKiri 的做法是把 jsonPlugin.cpp **直接加进本目标源列表**（见下方 target_sources），
# 那才是已验证可用的配置 —— json.dll 是三个验收游戏里两个都会 Plugins.link 的名字。
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
