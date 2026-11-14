cmake_minimum_required(VERSION 3.19)
project(core_environ_module LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(ENVIRON_PATH ${CMAKE_CURRENT_LIST_DIR})
set(ENVIRON_SOURCE_FILES
    ${ENVIRON_PATH}/Application.cpp
    ${ENVIRON_PATH}/DumpSend.cpp
    ${ENVIRON_PATH}/EngineBootstrap.cpp
    ${ENVIRON_PATH}/EngineLoop.cpp
    ${ENVIRON_PATH}/XP3ArchiveRepack.cpp

    $<$<BOOL:${MACOS}>:${ENVIRON_PATH}/apple/macos/platform.mm>

    $<$<BOOL:${IOS}>:${ENVIRON_PATH}/apple/ios/platform.mm>

    ${ENVIRON_PATH}/MainScene.cpp

    ${ENVIRON_PATH}/sdl/tvpsdl.cpp

    # ui/ — all removed (Flutter handles UI now).
    # Stub implementations are in stubs/ui_stubs.cpp
    ${ENVIRON_PATH}/stubs/ui_stubs.cpp

    # 平台源：
    #   Android：JNI 桥（KrkrJniHelper + AndroidUtils，完整 JNI 平台实现，走 KR2Activity/AppContext）
    #   Linux：stubs/platform_linux.cpp（__linux__ 守卫，CI 宿主验证用）
    # 不复用上游 linux/Platform.cpp：其 #ifdef LINUX 在 Android 下为空且强依赖 gtk，不适合移动端。
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android/KrkrJniHelper.cpp>
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android/AndroidUtils.cpp>
    $<$<NOT:$<BOOL:${ANDROID}>>:${ENVIRON_PATH}/stubs/platform_linux.cpp>

    ${ENVIRON_PATH}/win32/SystemControl.cpp

    # sysinit/ —— 启动序列、平台契约、配置、CPU 检测。
    # 原本是 core/base 的第四簇（target core_base_sysinit），整体搬到这里：
    # 它读 Application/SystemControl、要 GraphicsLoaderIntf/LayerIntf，本来就是
    # 启动路径而不是"base 的低层设施"。搬家后 base 不再有一条"假装自己是低层"
    # 的 environ 依赖（docs/STRUCTURE_DESIGN.md §3.3）。
    ${ENVIRON_PATH}/sysinit/SysInitIntf.cpp
    ${ENVIRON_PATH}/sysinit/SysInitImpl.cpp
    ${ENVIRON_PATH}/sysinit/DetectCPU.cpp

    ${ENVIRON_PATH}/sysinit/ConfigManager/GlobalConfigManager.cpp
    ${ENVIRON_PATH}/sysinit/ConfigManager/IndividualConfigManager.cpp
    ${ENVIRON_PATH}/sysinit/ConfigManager/LocaleConfigManager.cpp
)

set(ENVIRON_HEADERS_DIR
    ${ENVIRON_PATH}/
    # sysinit/ 必须在 win32/ 之前：它对外提供 SysInitIntf.h / SysInitImpl.h /
    # DetectCPU.h / ConfigManager/，目前 base 的 runtime、script 与 plugin、
    # visual 都靠裸文件名 include 它们。
    ${ENVIRON_PATH}/sysinit
    ${ENVIRON_PATH}/win32
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android>
    ${ENVIRON_PATH}/sdl
)

add_library(${PROJECT_NAME} STATIC ${ENVIRON_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${ENVIRON_HEADERS_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_visual_module core_sound_module core_plugin_module core_movie_module core_utils_module)

find_package(7zip CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(unofficial-minizip CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
        7zip::7zip
        unofficial::minizip::minizip
        tinyxml2::tinyxml2
)
