cmake_minimum_required(VERSION 3.19)
project(core_io_module LANGUAGES CXX)

set(IO_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${IO_PATH}/IoPath.cpp
)

# 自身要能解析 `StoragePolicy.h` / 其他 core 头：cpp/core 与 cpp/core/base 必须在
# 本模块的包含路径里（编译本模块时，别的 target 的 INTERFACE 包含路径不可见）。
target_include_directories(${PROJECT_NAME} PUBLIC
    ${IO_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../base
    ${CMAKE_CURRENT_LIST_DIR}/../base/impl
)

# 依赖方向（见 compat/README.md §2）：
#   本模块 -> tjs2（仅此）
#   core_base_module -> 本模块（StorageIntf/StorageImpl 的公开 API 由本模块提供实现）
# 反过来的依赖（本模块引用核心符号）会立刻造成静态库循环，不允许。
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2)
