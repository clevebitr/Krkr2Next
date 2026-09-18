cmake_minimum_required(VERSION 3.19)
project(core_compat_module LANGUAGES CXX)

set(COMPAT_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${COMPAT_PATH}/CompatLayer.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${COMPAT_PATH})

# 只依赖 tjs2（spdlog 由 tjs2 传递提供）。本模块不允许依赖 krkr2plugin、
# cpp/core/io 的实现或任何具体游戏/发行版代码（见 compat/README.md §2）。
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2)
