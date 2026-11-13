cmake_minimum_required(VERSION 3.19)
project(core_io_module LANGUAGES CXX)

set(IO_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${IO_PATH}/IoPath.cpp
    ${IO_PATH}/IoPolicy.cpp
    ${IO_PATH}/IoModuleLocator.cpp
    ${IO_PATH}/IoVirtualFile.cpp
    ${IO_PATH}/IoStorage.cpp
    ${IO_PATH}/IoStorageLocal.cpp
    ${IO_PATH}/XP3Archive.cpp
    ${IO_PATH}/XP3ArchiveCxDecoder.cpp
    ${IO_PATH}/ZIPArchive.cpp
    ${IO_PATH}/7zArchive.cpp
    ${IO_PATH}/TARArchive.cpp
)

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
