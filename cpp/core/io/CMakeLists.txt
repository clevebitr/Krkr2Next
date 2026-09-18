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
    # 归档后端（原 cpp/core/base/{XP3Archive*,ZIPArchive,7zArchive,TARArchive}.cpp，git mv）。
    # 归档知识与 IO 组件同处一地；它们仍会用到核心基础设施（消息常量、UtilStreams、
    # TVPMmapAlloc），那是**基础设施回边**，不是 IO 知识外泄。
    ${IO_PATH}/XP3Archive.cpp
    ${IO_PATH}/XP3ArchiveCxDecoder.cpp
    ${IO_PATH}/ZIPArchive.cpp
    ${IO_PATH}/7zArchive.cpp
    ${IO_PATH}/TARArchive.cpp
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
#   core_base_module -> 本模块（存储/归档实现已在本模块）
#   本模块 -> core_base_module：**只剩基础设施**（消息常量 MsgIntf、UtilStreams 的
#   流基类、TVPMmapAlloc、CharacterSet 等），IO/归档知识本身**不再**依赖 base。
# 两者都是 STATIC 库，CMake 允许 STATIC 之间的环并会在最终链接时重复它们。
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

# 归档后端需要的第三方依赖。**必须在本模块重复声明**：base 的这些依赖是 PRIVATE，
# 编译期包含路径不会经 INTERFACE 传过来（`$<LINK_ONLY:...>` 只带链接），而
# XP3Archive.cpp(<zlib.h>) / ZIPArchive.cpp(<minizip/ioapi.h>) / 7zArchive.cpp(<7zip/C/7z.h>)
# 现在由本模块编译。
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(7zip CONFIG REQUIRED)
find_package(unofficial-minizip CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ZLIB::ZLIB
    zstd::libzstd
    7zip::7zip
    unofficial::minizip::minizip
)
