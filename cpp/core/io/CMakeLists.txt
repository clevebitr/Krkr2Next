cmake_minimum_required(VERSION 3.19)
project(core_io_module LANGUAGES CXX)

set(IO_PATH ${CMAKE_CURRENT_LIST_DIR})

#---------------------------------------------------------------------------
# io = 四个按依赖层次切开的目录 / target + 一个只做聚合的 INTERFACE 门面
#
# 目录与 target 一一对应，读目录名就知道依赖方向：
#
#   policy/   core_io_policy    兼容层注入的 IO 口径（StoragePolicy / IoPolicy）
#                               → 只依赖 base 的存储声明，不认识任何具体层
#   path/     core_io_path      路径解析与模块定位（IoPath / IoModuleLocator）
#                               → 只依赖 base
#   archive/  core_io_archive   归档后端（XP3 / CX / ZIP / 7z / TAR）
#                               → 依赖 policy + base + zlib/zstd/7zip/minizip
#   storage/  core_io_storage   存储门面、本地存储、虚拟文件
#                               → 依赖 policy + path + archive + base
#
# 分层依据是**实测 include 图**，不是文件名：
#   raw 是 4 个 .cpp 的互相 include —— IoStorage{,.Local}.cpp → XP3Archive.h /
#   IoPolicy.h / IoModuleLocator.h；IoStorageLocal.cpp 反向只被同层用，
#   没有任何下层文件 include storage/ 里的头。
#
# 与 base 的差别（有意为之）：base 把链接行写在各簇上，簇之间已经存在
# script ↔ runtime 的声明式环。io 只有 20 个文件、聚合层足够表达完整链接行，
# 所以这里**簇只声明头面，链接行集中在聚合层**，不新增任何 target 环。
#
# `core_io_module` 保留为 INTERFACE 聚合层，所以 base/environ/plugins 里的
# `target_link_libraries(... core_io_module)` 一个字都不用改。
#---------------------------------------------------------------------------

# io 的公共头面。现在 include 语句写的还是裸文件名（"XP3Archive.h"），靠这四条
# -I 解析；等调用点迁移成 "io/archive/XP3Archive.h" 这种显式路径后可以收窄。
set(IO_LAYER_INCLUDE_DIRS
    ${IO_PATH}/policy
    ${IO_PATH}/path
    ${IO_PATH}/archive
    ${IO_PATH}/storage
)

# 历史上 io 目标对整个引擎暴露过的 include 面。收窄它会改变大量下游目标的
# 编译结果，没有编译验证之前按原样保留（cpp/core/io/README.md §"技术债"）。
set(IO_LEGACY_INCLUDE_DIRS
    ${IO_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../base/core
    ${CMAKE_CURRENT_LIST_DIR}/../base/runtime
    ${CMAKE_CURRENT_LIST_DIR}/../base/script
    ${CMAKE_CURRENT_LIST_DIR}/../common
    ${CMAKE_CURRENT_LIST_DIR}/../tjs2
    ${CMAKE_CURRENT_LIST_DIR}/../plugin
    ${CMAKE_CURRENT_LIST_DIR}/../visual
    ${CMAKE_CURRENT_LIST_DIR}/../visual/impl
    ${CMAKE_CURRENT_LIST_DIR}/../environ
    ${CMAKE_CURRENT_LIST_DIR}/../environ/intf
    ${CMAKE_CURRENT_LIST_DIR}/../environ/sysinit
    ${CMAKE_CURRENT_LIST_DIR}/../utils
    ${CMAKE_CURRENT_LIST_DIR}/../utils/c_api
    ${CMAKE_CURRENT_LIST_DIR}/../utils/misc
    ${CMAKE_CURRENT_LIST_DIR}/../utils/intf
    ${CMAKE_CURRENT_LIST_DIR}/../utils/impl
)

# 四个簇的共同点：都是 STATIC、都对外暴露 tjs2 类型、共享同一张公共头面。
function(io_add_layer name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC
        ${IO_LAYER_INCLUDE_DIRS}
        ${IO_LEGACY_INCLUDE_DIRS}
    )
    target_link_libraries(${name} PUBLIC tjs2)
endfunction()

#---------------------------------------------------------------------------
# policy/ —— 口径层：只登记取值，不 include 存储实现
#
# 约束：整个 io **不认识"层"**（krkr2-classic / aetherkiri），只认这份取值；
# 注入方向是 compat → io（compat/CompatLayer.cpp 调 SetActiveStoragePolicy）。
#---------------------------------------------------------------------------
io_add_layer(core_io_policy
    ${IO_PATH}/policy/IoPolicy.cpp
)

#---------------------------------------------------------------------------
# path/ —— 路径与模块定位
#
# IoPath.cpp      : 路径规范化 / 扩展名提取 / auto path 片段（TVP* 转发在这里）
# IoModuleLocator : 模块存在性查询的注入点（plugin/loader/PluginImpl.cpp 用它）
#---------------------------------------------------------------------------
io_add_layer(core_io_path
    ${IO_PATH}/path/IoPath.cpp
    ${IO_PATH}/path/IoModuleLocator.cpp
)

#---------------------------------------------------------------------------
# archive/ —— 归档后端
#
# XP3 段缓存（TVPMmapAlloc / SysInitIntf）与 Cx 解密都在这一层；
# storage/ 通过 XP3Archive.h 的工厂打开归档，不直接认识具体格式。
#---------------------------------------------------------------------------
io_add_layer(core_io_archive
    ${IO_PATH}/archive/XP3Archive.cpp
    ${IO_PATH}/archive/XP3ArchiveCxDecoder.cpp
    ${IO_PATH}/archive/ZIPArchive.cpp
    ${IO_PATH}/archive/7zArchive.cpp
    ${IO_PATH}/archive/TARArchive.cpp
)

#---------------------------------------------------------------------------
# storage/ —— 存储门面与本地文件
#
# IoStorage.cpp     : 挂载表 / auto path 表 / TVPOpenStream 一族
# IoStorageLocal.cpp: 本地文件系统（dirent、TVPSelectFile、Platform.h）
# IoVirtualFile     : 泛型虚拟文件注册点（compat 的 GPU 伴生脚本走这里）
#---------------------------------------------------------------------------
io_add_layer(core_io_storage
    ${IO_PATH}/storage/IoStorage.cpp
    ${IO_PATH}/storage/IoStorageLocal.cpp
    ${IO_PATH}/storage/IoVirtualFile.cpp
)

#---------------------------------------------------------------------------
# 聚合门面：名字不变，调用方零改动；链接行保持搬迁前的原样
#---------------------------------------------------------------------------
add_library(core_io_module INTERFACE)
target_link_libraries(core_io_module INTERFACE
    core_io_policy
    core_io_path
    core_io_archive
    core_io_storage

    # 搬迁前是 PUBLIC：tjs2 类型与 base 的存储声明对这些簇是头依赖的前提
    tjs2
    core_base_module
)
# 搬迁前这些是 PRIVATE：STATIC 库的 PRIVATE 依赖本来就会以 LINK_ONLY 传给最终
# 链接行，所以放在聚合层与放在簇上是等价的；集中放在这里更容易对照搬迁前的写法。
target_link_libraries(core_io_module INTERFACE
    core_visual_module
    core_plugin_module
    core_environ_module
    core_extension_module
    core_sound_module
    core_utils_module
)

find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(7zip CONFIG REQUIRED)
find_package(unofficial-minizip CONFIG REQUIRED)
target_link_libraries(core_io_module INTERFACE
    ZLIB::ZLIB
    zstd::libzstd
    7zip::7zip
    unofficial::minizip::minizip
)
