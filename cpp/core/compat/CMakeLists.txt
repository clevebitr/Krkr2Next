cmake_minimum_required(VERSION 3.19)
project(core_compat_module LANGUAGES CXX)

set(COMPAT_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${COMPAT_PATH}/CompatLayer.cpp
    ${COMPAT_PATH}/ModuleGate.cpp
)

# 自身要能解析 `io/StoragePolicy.h`：cpp/core 必须在本模块的包含路径里。
# （krkr2core 的 INTERFACE 包含路径只作用于链接它的目标，编译本模块时不可见 ——
#  CI 35335370234 就是栽在这一条上。）
target_include_directories(${PROJECT_NAME} PUBLIC
    ${COMPAT_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

# 只依赖 tjs2（spdlog 由 tjs2 传递提供）。本模块不允许依赖 krkr2plugin、
# cpp/core/io 的实现或任何具体游戏/发行版代码（见 compat/README.md §2）。
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2)
