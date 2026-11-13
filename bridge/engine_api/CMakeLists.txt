cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

set(ENGINE_API_SOURCES
    src/engine_api_state.cpp
    src/engine_api_logging.cpp
    src/engine_api_compat.cpp
    src/engine_api_input.cpp
    src/engine_api_menu.cpp
    src/engine_api_lifecycle.cpp
    src/engine_api_frame.cpp
    src/engine_api_options.cpp
    src/engine_api_misc.cpp
    src/engine_api_stub.cpp
)

if(ANDROID)
    # Android: build as a self-contained shared library. Everything the engine
    # needs (core + plugins + vcpkg static deps) is linked into libengine_api.so
    # so the APK only has to ship one native library, loaded by the Kotlin shell
    # via System.loadLibrary -> JNI.
    add_library(engine_api SHARED
        ${ENGINE_API_SOURCES}
        src/engine_api_android_jni.cpp
    )
else()
    add_library(engine_api SHARED
        ${ENGINE_API_SOURCES}
    )
endif()

target_compile_features(engine_api PRIVATE cxx_std_17)

target_compile_definitions(engine_api PRIVATE ENGINE_API_BUILD_SHARED)
target_include_directories(engine_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(ANDROID)
    # engine_api_android_jni.cpp needs krkr::JniHelper (android/KrkrJniHelper.h).
    target_include_directories(engine_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/environ
    )
endif()

if(TARGET krkr2core AND TARGET krkr2plugin)
    target_compile_definitions(engine_api PRIVATE ENGINE_API_USE_KRKR2_RUNTIME)
    if(ANDROID)
        target_link_libraries(engine_api PRIVATE krkr2core krkr2plugin)
        target_link_libraries(engine_api PRIVATE android log)
        target_link_libraries(engine_api PRIVATE EGL GLESv2)
        target_link_options(engine_api PRIVATE "LINKER:--allow-multiple-definition")
    else()
        target_link_libraries(engine_api PRIVATE krkr2core krkr2plugin)
    endif()
endif()

set_target_properties(engine_api PROPERTIES
    OUTPUT_NAME "engine_api"
)
