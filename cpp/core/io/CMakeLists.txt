cmake_minimum_required(VERSION 3.19)
project(core_io_module LANGUAGES CXX)

set(IO_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${IO_PATH}/IoPath.cpp
    # 存储系统实现（原 cpp/core/base/StorageIntf.cpp，git mv 保留历史）。
    # 公开声明仍在 cpp/core/base/StorageIntf.h —— 约 200 个调用点不动，只换定义所在 TU。
    ${IO_PATH}/IoStorage.cpp
    # 本地文件媒体 / 归档打开 / 启动挂载 / 补丁优先级（原
    # cpp/core/base/impl/StorageImpl.cpp，git mv 保留历史）。声明仍在
    # cpp/core/base/impl/StorageImpl.h。
    ${IO_PATH}/IoStorageLocal.cpp
)

# 自身要能解析 `StoragePolicy.h` / 其他 core 头：cpp/core 与 cpp/core/base 必须在
# 本模块的包含路径里（编译本模块时，别的 target 的 INTERFACE 包含路径不可见）。
# 包含路径照抄 core_base_module 的集合：本模块现在包含原 base 的存储实现，
# 它会 include StorageIntf.h / MsgIntf.h / XP3Archive.h / ncbind.hpp 等。
target_include_directories(${PROJECT_NAME} PUBLIC
    ${IO_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../base
    ${CMAKE_CURRENT_LIST_DIR}/../base/impl
    ${CMAKE_CURRENT_LIST_DIR}/../common
    ${CMAKE_CURRENT_LIST_DIR}/../tjs2
    ${CMAKE_CURRENT_LIST_DIR}/../plugin
    ${CMAKE_CURRENT_LIST_DIR}/../visual
    ${CMAKE_CURRENT_LIST_DIR}/../visual/impl
    ${CMAKE_CURRENT_LIST_DIR}/../environ
    ${CMAKE_CURRENT_LIST_DIR}/../environ/win32
    ${CMAKE_CURRENT_LIST_DIR}/../utils
    ${CMAKE_CURRENT_LIST_DIR}/../utils/win32
)

# 依赖方向（见 compat/README.md §2 与 IO 搬迁顺序）：
#   core_base_module -> 本模块（存储公开 API 的实现已在本模块）
#   本模块 -> core_base_module（仍需 TVPCreateFileMedia / TVPOpenArchive / 消息常量等）
# 两者都是 STATIC 库，CMake 允许 STATIC 之间的环并在最终链接时重复它们（shared/可执行
# 目标之间的环才会报错）。等到 M1.4/M1.5 把归档工厂与本地文件媒体也搬进来后，这条回边
# 应当消失 —— 那是本组件"单一性"的验收点之一。
target_link_libraries(${PROJECT_NAME} PUBLIC
    tjs2
    core_base_module
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    core_visual_module
    core_plugin_module
    core_environ_module
    core_extension_module
    core_sound_module
    core_utils_module
)
