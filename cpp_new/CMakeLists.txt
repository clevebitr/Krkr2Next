# cpp_new 的构建目标。
#
# 目标划分与 docs/ARCHITECTURE.md 的分层一致，依赖方向由 target_link_libraries
# 表达：tjs ← shim ← engine ← host，core 只被依赖、不反向引用上层。
# 技术债 #38 由本文件落实；tools/build_probes.sh 可删除。

cmake_minimum_required(VERSION 3.20)
project(krkr_next LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(KRKR_PRELUDE "${KRKR_ROOT}/shim/tjs_host/tjs_prelude.h")

# tjs2 上游为 MSVC 编写，GCC/Clang 下存在大量无害告警（技术债 #36）。
set(KRKR_TJS_NOISE -w -fpermissive)

# -----------------------------------------------------------------------------
# tjs：权威实现。除排除项外不修改，编译参数即垫层开关。
# -----------------------------------------------------------------------------
file(GLOB KRKR_TJS_SOURCES CONFIGURE_DEPENDS "${KRKR_ROOT}/tjs/*.cpp")
list(REMOVE_ITEM KRKR_TJS_SOURCES
  "${KRKR_ROOT}/tjs/tjsDebug.cpp"
  "${KRKR_ROOT}/tjs/tjsRegExp.cpp")

# 宿主符号（调试设施空实现、正则桩）由垫层提供，tjs 在链接期依赖它。
# 单独成库是为了表达"tjs 源码不依赖 shim，但链接需要宿主符号"。
add_library(krkr_tjs_host STATIC
  shim/tjs_host/TjsHostShim.cpp
  shim/tjs_host/TjsRegExpShim.cpp)
target_link_libraries(krkr_tjs_host PUBLIC krkr_tjs)

add_library(krkr_tjs STATIC ${KRKR_TJS_SOURCES})
target_include_directories(krkr_tjs PUBLIC "${KRKR_ROOT}")
target_include_directories(krkr_tjs PRIVATE
  "${KRKR_ROOT}/shim/tjs_host" "${KRKR_ROOT}/shim/tjs_host/generated")
target_compile_definitions(krkr_tjs PUBLIC UNICODE _UNICODE TJS_NO_REGEXP)
target_compile_options(krkr_tjs PUBLIC
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:${KRKR_TJS_NOISE}>"
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-include>"
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:${KRKR_PRELUDE}>"
  "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/FI${KRKR_PRELUDE}>")
target_link_libraries(krkr_tjs PUBLIC krkr_tjs_host)

# -----------------------------------------------------------------------------
# core：基座（日志、存储、对外 API）。不引用 shim / platform / tjs。
# -----------------------------------------------------------------------------
set(KRKR_CORE_SOURCES
  core/log/Logger.cpp
  core/api/EngineRuntime.cpp
  core/io/PathUtil.cpp
  core/io/IoName.cpp
  core/io/DirectoryBackend.cpp
  core/io/Storage.cpp
  core/io/archive/Xp3Index.cpp
  core/io/archive/Xp3Stream.cpp
  core/io/archive/Inflate.cpp
  core/io/archive/third_party/puff.c)

# puff 以 C++ 编译：上游 Inflate.cpp 按 C++ 链接名调用它（见 todo.md 4.8），
# 用 C 编译会得到未修饰的符号名而链接失败。
set_source_files_properties("${KRKR_ROOT}/core/io/archive/third_party/puff.c"
  PROPERTIES LANGUAGE CXX)

add_library(krkr_core STATIC ${KRKR_CORE_SOURCES})
target_include_directories(krkr_core PUBLIC "${KRKR_ROOT}")

# -----------------------------------------------------------------------------
# platform：平台能力实现（当前仅有空目录，见技术债 #39）。
# -----------------------------------------------------------------------------
add_library(krkr_platform INTERFACE)
target_link_libraries(krkr_platform INTERFACE krkr_core)

# -----------------------------------------------------------------------------
# shim：垫层（TJS 门面、脚本装载、原生类桥接、宿主符号）。
# -----------------------------------------------------------------------------
set(KRKR_SHIM_SOURCES
  shim/engine/TjsEngine.cpp
  shim/engine/ScriptLoader.cpp
  shim/engine/NativeClass.cpp
  shim/classes/TjsStorages.cpp
  shim/classes/TjsScripts.cpp
  shim/classes/TjsSystem.cpp
  shim/classes/TjsDebug.cpp
  shim/classes/TjsPlugins.cpp
  shim/TjsBridge.cpp
  shim/TjsDiagnostics.cpp
  shim/text/TjsText.cpp)

add_library(krkr_shim STATIC ${KRKR_SHIM_SOURCES})
target_include_directories(krkr_shim PUBLIC
  "${KRKR_ROOT}" "${KRKR_ROOT}/shim/tjs_host" "${KRKR_ROOT}/shim/tjs_host/generated")
target_compile_options(krkr_shim PRIVATE
  "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:${KRKR_TJS_NOISE}>")
target_link_libraries(krkr_shim PUBLIC krkr_core PRIVATE krkr_tjs)

# -----------------------------------------------------------------------------
# engine：装配根。唯一允许同时依赖 shim 与 core 的层。
# -----------------------------------------------------------------------------
add_library(krkr_engine STATIC
  engine/ScriptRuntime.cpp
  engine/CompatProfile.cpp)
target_include_directories(krkr_engine PUBLIC "${KRKR_ROOT}")
target_link_libraries(krkr_engine PUBLIC krkr_shim krkr_core krkr_tjs)

# -----------------------------------------------------------------------------
# 测试台（host/ 不属于引擎产物）
# -----------------------------------------------------------------------------
add_executable(tjs_probe host/TjsProbe.cpp)
target_link_libraries(tjs_probe PRIVATE krkr_engine)

add_executable(krkr_next host/main.cpp)
target_link_libraries(krkr_next PRIVATE krkr_engine)

add_executable(storage_probe host/StorageProbe.cpp)
target_link_libraries(storage_probe PRIVATE krkr_core)

add_executable(xp3_probe host/Xp3Probe.cpp)
target_link_libraries(xp3_probe PRIVATE krkr_core)

add_executable(xp3pack host/Xp3Pack.cpp)
target_link_libraries(xp3pack PRIVATE krkr_core)

# -----------------------------------------------------------------------------
# 断言（ctest）。断言脚本在 host/fixtures/demo，末尾抛 "<name>-ok" 表示通过。
# -----------------------------------------------------------------------------
enable_testing()

set(KRKR_FIXTURES "${KRKR_ROOT}/host/fixtures")
set(KRKR_FIXTURE_ROOT_FILE "${CMAKE_BINARY_DIR}/fixtures_root.txt")
file(WRITE "${KRKR_FIXTURE_ROOT_FILE}" "${KRKR_FIXTURES}/demo\n")
set(KRKR_ARCHIVE "${CMAKE_BINARY_DIR}/probe_fixture.xp3")
set(KRKR_EXTRACTED "${CMAKE_BINARY_DIR}/probe_extract.tjs")

# 断言脚本里的相对路径（如 Debug.logLocation = "build/..."）以仓库根为基准，
# 与 tools/run_probes.sh 的历史行为一致。
set(KRKR_TEST_WORKDIR "${KRKR_ROOT}/..")

add_test(NAME storage_dir COMMAND storage_probe "${KRKR_FIXTURES}/demo")

add_test(NAME xp3_pack COMMAND xp3pack "${KRKR_ARCHIVE}"
  "${KRKR_FIXTURES}/demo/startup.tjs" "${KRKR_FIXTURES}/demo/t_exec.tjs")
set_tests_properties(xp3_pack PROPERTIES FIXTURES_SETUP xp3_fixture)

add_test(NAME xp3_find COMMAND xp3_probe "${KRKR_ARCHIVE}" --find startup.tjs)
set_tests_properties(xp3_find PROPERTIES FIXTURES_REQUIRED xp3_fixture)

add_test(NAME xp3_extract COMMAND storage_probe "${KRKR_ARCHIVE}"
  --extract startup.tjs "${KRKR_EXTRACTED}")
set_tests_properties(xp3_extract PROPERTIES FIXTURES_REQUIRED xp3_fixture)

add_test(NAME xp3_extract_identical
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${KRKR_EXTRACTED}"
          "${KRKR_FIXTURES}/demo/startup.tjs")
set_tests_properties(xp3_extract_identical PROPERTIES FIXTURES_REQUIRED xp3_fixture)

add_test(NAME startup_state COMMAND krkr_next --path-file "${KRKR_FIXTURE_ROOT_FILE}")

foreach(script t_scripts t_system t_debug t_plugins
               t_repro t_alias t_spread t_exit)
  add_test(NAME "script_${script}"
    COMMAND tjs_probe "${KRKR_FIXTURES}/demo/${script}.tjs" "${KRKR_FIXTURE_ROOT_FILE}")
endforeach()

get_property(KRKR_ALL_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${KRKR_ALL_TESTS} PROPERTIES WORKING_DIRECTORY "${KRKR_TEST_WORKDIR}")

# 真游戏回归（可选）：build/real_root.txt 存在时启用，当前预期停在 Window 类。
set(KRKR_REAL_ROOT_FILE "${KRKR_ROOT}/../build/real_root.txt")
if(EXISTS "${KRKR_REAL_ROOT_FILE}")
  set(KRKR_REAL_SCRIPT "${KRKR_ROOT}/../build/py_startup.utf8.tjs")
  if(EXISTS "${KRKR_REAL_SCRIPT}")
    add_test(NAME real_startup
      COMMAND tjs_probe "${KRKR_REAL_SCRIPT}" "${KRKR_REAL_ROOT_FILE}")
    set_tests_properties(real_startup PROPERTIES DISABLED TRUE)
  endif()
endif()
