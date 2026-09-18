cmake_minimum_required(VERSION 3.28)
project(krkr2core)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

add_library(${PROJECT_NAME} INTERFACE)

add_subdirectory(tjs2)
add_subdirectory(base)
add_subdirectory(compat)
add_subdirectory(environ)
add_subdirectory(extension)
add_subdirectory(plugin)
add_subdirectory(movie)
add_subdirectory(sound)
add_subdirectory(visual)
add_subdirectory(utils)

target_link_libraries(${PROJECT_NAME} INTERFACE
        tjs2
        core_base_module
        core_compat_module
        core_environ_module
        core_extension_module
        core_plugin_module
        core_movie_module
        core_sound_module
        core_visual_module
        core_utils_module
)

target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} INTERFACE
    -DTJS_TEXT_OUT_CRLF
    -D__STDC_CONSTANT_MACROS
    -DUSE_UNICODE_FSTRING
)

if(NOT APPLE)
    # 启用 OpenMP 支持
    find_package(OpenMP REQUIRED)

    target_link_libraries(${PROJECT_NAME} INTERFACE OpenMP::OpenMP_CXX)
    target_compile_options(${PROJECT_NAME} INTERFACE ${OpenMP_CXX_FLAGS})
endif ()

# 原生 EGL + GLES（不经 ANGLE）。Android 由 NDK sysroot 提供 libEGL.so /
# libGLESv2.so，Linux 宿主构建由系统 GLVND/Mesa 提供。
#
# 为什么链接放在 krkr2core 的 INTERFACE 层，而不是只在 visual 模块 PRIVATE：
# 插件源码（如 krkrgles.cpp）经 INTERFACE_SOURCES 直接编进最终的 libengine_api.so，
# 最终链接发生在 engine_api 上。放在 INTERFACE 层才能让这些库在整个核心的链接
# 接口里可见。krkr2 与 AetherKiri 都采用这一位置。
#
# visual 模块另有一份 PRIVATE 链接（供只链 core_visual_module 的目标，如
# tests/tvpgl_simd_compare 使用），两处都保留。
target_link_libraries(${PROJECT_NAME} INTERFACE EGL GLESv2)

