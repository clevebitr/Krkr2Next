#!/usr/bin/env bash
#
# check_cmake_graph.sh — 不需要 NDK / vcpkg / 第三方库，验证某一个模块的
# CMakeLists.txt 能不能正确解析、簇 target 是否建成、聚合层是否指向它们。
#
# 为什么需要它：本仓库的模块重组是"目录 = target + 一个 INTERFACE 聚合层"。
# 这类改动最容易在两种地方出错，而这两种错在没有编译器时完全静默：
#   1. 聚合层少写/写错一个簇 → 该簇的目标文件不会被编进最终 .so（符号缺失要到
#      真机 dlopen 才炸）；
#   2. 目录搬了、CMakeLists 里还是旧路径 → add_library 拿到不存在的源文件。
# 真正判据仍然是 `./build.sh debug --engine-only`；这个脚本只是把"显然错"提前到
# 现在就能发现。
#
# 原理：造一个临时 project，把所有跨模块依赖和 find_package 的包都替成空桩，
# 再 include 目标模块的 CMakeLists.txt（include 会把 CMAKE_CURRENT_LIST_DIR
# 指到那个模块目录，所以 ${CMAKE_CURRENT_LIST_DIR}/... 照常解析）。
#
# 用法：
#   scripts/check_cmake_graph.sh io sound      # 指定模块
#   scripts/check_cmake_graph.sh iosound 之类不会被当成模块 -> 直接报错退出
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE="$ROOT/cpp/core"

[[ $# -ge 1 ]] || { echo "用法: $0 <模块名> [模块名...]" >&2; exit 1; }

# 跨模块 target 桩：新增模块时在这里补一行，否则 include 到
# target_link_libraries(<别的模块>) 会因为 target 不存在而报错。
STUB_TARGETS=(
    tjs2
    core_base_module core_io_module core_compat_module core_environ_module
    core_extension_module core_plugin_module core_movie_module
    core_sound_module core_visual_module core_utils_module
    OpenSLES
)

# find_package 桩（vcpkg 才有真 config，CI 之外的机器上都没有）。
STUB_PACKAGES=(
    ZLIB zstd 7zip unofficial-minizip Vorbis Opus OpusFile OpenAL
    uchardet unrar LibArchive LibXml2 SDL2 tinyxml2
)

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/fakelib"
: > "$WORK/fakelib/liboboe.a"        # ANDROID 分支的 find_library(OBOE_STATIC_LIB oboe)

for p in "${STUB_PACKAGES[@]}"; do
    mkdir -p "$WORK/stubs/$p"
    printf 'set(%s_FOUND TRUE)\nset(%s_VERSION 0.0.0)\n' "$p" "$p" \
        > "$WORK/stubs/$p/${p}Config.cmake"
done

{
    echo 'cmake_minimum_required(VERSION 3.28)'
    # 勾上 C：core/utils 的 c_api/ 簇只有 .c 源文件（真实构建里根 project() 已经开了 C CXX）
    echo 'project(check_cmake_graph LANGUAGES C CXX)'
    echo "set(CMAKE_PREFIX_PATH \"$WORK/stubs\")"
    echo "set(CMAKE_LIBRARY_PATH \"$WORK/fakelib\")"

    echo 'foreach(t'
    for t in "${STUB_TARGETS[@]}"; do
        # 被检查模块自己的聚合层由它自己的 CMakeLists 建，先建桩就会撞名
        skip=0
        for m in "$@"; do [[ $t == "core_${m}_module" ]] && skip=1; done
        (( skip )) || echo "    $t"
    done
    echo ')'
    echo '    if(NOT TARGET ${t})'
    echo '        add_library(${t} INTERFACE)'
    echo '    endif()'
    echo 'endforeach()'

    echo 'foreach(t'
    printf '    %s\n' "ZLIB::ZLIB" "zstd::libzstd" "7zip::7zip" \
        "unofficial::minizip::minizip" "Vorbis::vorbis" "Vorbis::vorbisfile" \
        "Vorbis::vorbisenc" "Opus::opus" "OpusFile::opusfile" "OpenAL::OpenAL" \
        "uchardet::libuchardet" "unrar::unrar" "LibArchive::LibArchive" \
        "LibXml2::LibXml2" "SDL2::SDL2" "tinyxml2::tinyxml2"
    echo ')'
    echo '    add_library(${t} INTERFACE IMPORTED)'
    echo 'endforeach()'

    for m in "$@"; do
        f="$CORE/$m/CMakeLists.txt"
        [[ -f $f ]] || { echo "找不到模块 CMakeLists: $f" >&2; exit 1; }
        echo "include(\"$f\")"
    done

    # 每个模块都必须建出 <module> 聚合层；簇的名字不强制（各模块层次不同）。
    for m in "$@"; do
        echo "if(NOT TARGET core_${m}_module)"
        echo "    message(FATAL_ERROR \"模块 $m 没有建出聚合层 core_${m}_module\")"
        echo "endif()"
        echo "get_target_property(link core_${m}_module INTERFACE_LINK_LIBRARIES)"
        echo "message(STATUS \"core_${m}_module -> \${link}\")"
    done

    # 所有 STATIC 簇的源文件必须真的存在（搬目录最大的坑）。
    echo 'get_property(all_targets DIRECTORY "'"$WORK"'" PROPERTY BUILDSYSTEM_TARGETS)'
    echo 'foreach(t ${all_targets})'
    echo '    get_target_property(type ${t} TYPE)'
    echo '    if(type STREQUAL "STATIC_LIBRARY")'
    echo '        get_target_property(srcs ${t} SOURCES)'
    echo '        foreach(s ${srcs})'
    # generator expression（$<...>）在这里只求值为空串或不存在的文件，
    # 因为桩工程里没有 MACOS/IOS 这些条件变量；按原样跳过，只检查字面路径。
    echo '            if(s MATCHES "\\$<")'
    echo '                continue()'
    echo '            endif()'
    echo '            if(NOT EXISTS ${s})'
    echo '                message(FATAL_ERROR "${t}: 源文件不存在 ${s}")'
    echo '            endif()'
    echo '        endforeach()'
    echo '        list(LENGTH srcs n)'
    echo '        message(STATUS "${t}: ${n} 个源文件")'
    echo '    endif()'
    echo 'endforeach()'
} > "$WORK/CMakeLists.txt"

if cmake -S "$WORK" -B "$WORK/build" -G Ninja 2>&1 | tee "$WORK/log.txt" | grep -E '^(--|CMake Error)' ; then :; fi

if grep -q 'CMake Error' "$WORK/log.txt"; then
    echo
    echo "✗ CMake 图校验失败（见上面的报错）。" >&2
    exit 1
fi

echo
echo "✓ CMake 图校验通过：聚合层与源文件都存在。"
echo "  注意：这只证明 target 图正确，不证明能编译（见文件头说明）。"
