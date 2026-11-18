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

# Clang（NDK）比 GCC 严格：vendored tjs 有 C++17 非法的 register、取临时量地址，
# 以及 dependent base 的未限定查找（上游只被 MSVC/GCC 编过）。用编译参数兼容，
# 不改 tjs/ 源码（ARCHITECTURE §4.2：工具链差异一律走垫层参数）。
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(krkr_tjs PUBLIC
    -Wno-register -Wno-address-of-temporary -fdelayed-template-parsing)
endif()

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
# visual：合成与位图（渲染第 8a 片）。软件后端是像素权威，GL 后端后补。
# -----------------------------------------------------------------------------
add_library(krkr_visual STATIC
  visual/Surface.cpp visual/Blend.cpp visual/SoftwareBackend.cpp visual/GLBackend.cpp)
target_include_directories(krkr_visual PUBLIC "${KRKR_ROOT}")
target_link_libraries(krkr_visual PUBLIC krkr_core)

# -----------------------------------------------------------------------------
# platform：平台能力实现（呈现目标）。visual 与 platform 互不引用，
# 二者只通过 core/contracts/IRenderTarget.h 相遇。
# -----------------------------------------------------------------------------
add_library(krkr_platform STATIC platform/desktop/PpmTarget.cpp)
target_include_directories(krkr_platform PUBLIC "${KRKR_ROOT}")
target_link_libraries(krkr_platform PUBLIC krkr_core)

if(WIN32)
  target_sources(krkr_platform PRIVATE
    platform/desktop/win32/Win32WindowTarget.cpp
    platform/desktop/gl/GLContext.cpp)
  target_link_libraries(krkr_platform PRIVATE opengl32)
elseif(ANDROID)
  target_sources(krkr_platform PRIVATE
    platform/android/AndroidTarget.cpp
    platform/android/AndroidGLContext.cpp
    platform/android/AndroidSurface.cpp)
  target_link_libraries(krkr_platform PRIVATE EGL GLESv3 android log)
endif()

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
# debugger：调试器子包（docs/debugger-spec.md）。实现 core 的调试契约，
# 不被任何下层引用，也不进入引擎产物。
# -----------------------------------------------------------------------------
add_library(krkr_debugger STATIC debugger/agent/EventRecorder.cpp)
target_include_directories(krkr_debugger PUBLIC "${KRKR_ROOT}")
target_link_libraries(krkr_debugger PUBLIC krkr_core)

add_executable(krkr_debug debugger/cli/DebugTail.cpp)
target_link_libraries(krkr_debug PRIVATE krkr_debugger)

# -----------------------------------------------------------------------------
# engine：装配根。唯一允许同时依赖 shim 与 core 的层。
# -----------------------------------------------------------------------------
add_library(krkr_engine STATIC
  engine/ScriptRuntime.cpp
  engine/CompatProfile.cpp)
target_include_directories(krkr_engine PUBLIC "${KRKR_ROOT}")
target_link_libraries(krkr_engine PUBLIC krkr_shim krkr_core krkr_tjs)

# -----------------------------------------------------------------------------
# 测试台（host/ 不属于引擎产物）。Android 上只编库，避免把测试台塞进 APK。
# -----------------------------------------------------------------------------
option(KRKR_BUILD_TOOLS "Build test benches and register ctest cases" ON)

if(KRKR_BUILD_TOOLS)
add_executable(tjs_probe host/TjsProbe.cpp)
target_link_libraries(tjs_probe PRIVATE krkr_engine krkr_debugger)

add_executable(krkr_next host/main.cpp)
target_link_libraries(krkr_next PRIVATE krkr_engine krkr_debugger)

add_executable(storage_probe host/StorageProbe.cpp)
target_link_libraries(storage_probe PRIVATE krkr_core)

add_executable(xp3_probe host/Xp3Probe.cpp)
target_link_libraries(xp3_probe PRIVATE krkr_core)

add_executable(xp3pack host/Xp3Pack.cpp)
target_link_libraries(xp3pack PRIVATE krkr_core)

add_executable(render_probe host/RenderProbe.cpp)
target_link_libraries(render_probe PRIVATE krkr_visual krkr_platform)

if(WIN32)
  add_executable(render_window host/RenderWindow.cpp)
  target_link_libraries(render_window PRIVATE krkr_visual krkr_platform)

  add_executable(render_gl_probe host/RenderGlProbe.cpp)
  target_link_libraries(render_gl_probe PRIVATE krkr_visual krkr_platform)
endif()

endif() # KRKR_BUILD_TOOLS

# -----------------------------------------------------------------------------
# 断言（ctest）。断言脚本在 host/fixtures/demo，末尾抛 "<name>-ok" 表示通过。
# -----------------------------------------------------------------------------
if(KRKR_BUILD_TOOLS)
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

add_test(NAME render_surface
  COMMAND render_probe "${CMAKE_BINARY_DIR}/render_probe.ppm")

if(WIN32)
  # 没有可用 GL 的环境（CI 无显示）记 skip，而不是失败。
  add_test(NAME render_gl COMMAND render_gl_probe)
  set_tests_properties(render_gl PROPERTIES SKIP_RETURN_CODE 77)
endif()

foreach(script t_scripts t_system t_debug t_plugins
               t_repro t_alias t_spread t_exit)
  add_test(NAME "script_${script}"
    COMMAND tjs_probe "${KRKR_FIXTURES}/demo/${script}.tjs" "${KRKR_FIXTURE_ROOT_FILE}")
endforeach()

get_property(KRKR_ALL_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${KRKR_ALL_TESTS} PROPERTIES WORKING_DIRECTORY "${KRKR_TEST_WORKDIR}")

# 调试事件：录制一次脚本断言，再断言事件流里出现过脚本、存储与失败事件。
set(KRKR_EVENTS "${CMAKE_BINARY_DIR}/events.jsonl")
add_test(NAME debug_record
  COMMAND "${CMAKE_COMMAND}" -E env "KRKR_DEBUG_EVENTS=${KRKR_EVENTS}"
          "$<TARGET_FILE:tjs_probe>" "${KRKR_FIXTURES}/demo/t_scripts.tjs"
          "${KRKR_FIXTURE_ROOT_FILE}")
set_tests_properties(debug_record PROPERTIES FIXTURES_SETUP debug_events)

foreach(check "ev=script.begin" "ev=script.end" "ev=storage.open,ok=true" "ev=script.fail")
  string(REPLACE "=" "_" check_name "${check}")
  string(REPLACE "," "_" check_name "${check_name}")
  add_test(NAME "debug_${check_name}"
    COMMAND "$<TARGET_FILE:krkr_debug>" --filter "${check}" "${KRKR_EVENTS}")
  set_tests_properties("debug_${check_name}" PROPERTIES FIXTURES_REQUIRED debug_events)
endforeach()

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
endif() # KRKR_BUILD_TOOLS
