cmake_minimum_required(VERSION 3.19)
project(krkr2_tests CXX)

# Catch2 由 vcpkg manifest 提供（platform: !ios & !android）。只有桌面预设会走到
# 这里——根 CMakeLists.txt 的条件是 `ENABLE_TESTS AND NOT ANDROID`。
find_package(Catch2 3 REQUIRED)
include(CTest)
include(Catch)

# ---------------------------------------------------------------------------
# tvpgl_simd_compare — SIMD(Highway) vs 标量 逐像素比对
#
# 自带 main()、以退出码表示成败，不是 Catch2 用例；与下面的 Catch2 目标并存。
# core_visual_module 内含 tvpgl.cpp（*_c 标量）与 tvpgl_simd（_hwy），
# 静态库按需拉取，测试只引用 tvpgl 混合函数，不会拖入渲染路径。
# ---------------------------------------------------------------------------
add_executable(tvpgl_simd_compare tvpgl_simd_compare.cpp)
target_link_libraries(tvpgl_simd_compare PRIVATE core_visual_module)
# core_visual_module 以 PRIVATE 链入 tvpgl_simd，simd/ 头目录不会透传到测试，
# 需显式补上，供 #include "tvpgl_simd_init.h"
target_include_directories(tvpgl_simd_compare PRIVATE ${KRKR2CORE_PATH}/visual/simd)
add_test(NAME tvpgl_simd_compare COMMAND tvpgl_simd_compare)

add_subdirectory(unit-tests)
