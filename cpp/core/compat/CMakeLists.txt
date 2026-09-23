cmake_minimum_required(VERSION 3.19)
project(core_compat_module LANGUAGES CXX)

set(COMPAT_PATH ${CMAKE_CURRENT_LIST_DIR})

# ---------------------------------------------------------------------------
# D3DEmote.tjs 嵌入（方案 B：用参考实现的脚本覆盖游戏的 `system/motion.tjs`）
#
# 为什么用 configure_file 生成字节数组而不是手写 C++ 字符串字面量：1340 行日文脚本，
# 转义一旦写错就是静默的语法错误；hex 字节数组不会错，也不受源文件编码影响。
# 与 AetherKiri `cpp/core/base/CMakeLists.txt` 同一做法。
# ---------------------------------------------------------------------------
set(COMPAT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(COMPAT_D3DEMOTE_RESOURCE ${COMPAT_PATH}/resources/D3DEmote.tjs)
set(COMPAT_D3DEMOTE_SOURCE ${COMPAT_GENERATED_DIR}/D3DEmote_tjs.cpp)
file(MAKE_DIRECTORY ${COMPAT_GENERATED_DIR})
file(READ ${COMPAT_D3DEMOTE_RESOURCE} COMPAT_D3DEMOTE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
       COMPAT_D3DEMOTE_BYTES "${COMPAT_D3DEMOTE_HEX}")
configure_file(${COMPAT_PATH}/resources/D3DEmote_tjs.cpp.in
               ${COMPAT_D3DEMOTE_SOURCE} @ONLY)

add_library(${PROJECT_NAME} STATIC
    ${COMPAT_PATH}/CompatLayer.cpp
    ${COMPAT_PATH}/ModuleGate.cpp
    # AetherKiri 层的伴生脚本（虚拟文件）提供者。provider 只产出内容，由 io 包成流。
    ${COMPAT_PATH}/AetherKiriCompanions.cpp
    # 由上面的 configure_file 生成（D3DEmote.tjs 的字节数组）。
    ${COMPAT_D3DEMOTE_SOURCE}
)

# 自身要能解析 `io/StoragePolicy.h`：cpp/core 必须在本模块的包含路径里。
# （krkr2core 的 INTERFACE 包含路径只作用于链接它的目标，编译本模块时不可见 ——
#  CI 35335370234 就是栽在这一条上。）
target_include_directories(${PROJECT_NAME} PUBLIC
    ${COMPAT_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

# 只依赖 tjs2（spdlog 由 tjs2 传递提供）。本模块不允许依赖 krkr2plugin、
# cpp/core/io 的**实现细节**或任何具体游戏/发行版代码（见 compat/README.md §2）；
# 但允许经 io 的**接口**（io/IoVirtualFile.h 的注册点）贡献虚拟文件。
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2)
