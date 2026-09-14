cmake_minimum_required(VERSION 3.16)

project(engine_api LANGUAGES C CXX)

if(ANDROID)
    # Android: build as a self-contained shared library. Everything the engine
    # needs (core + plugins + vcpkg static deps) is linked into libengine_api.so
    # so the APK only has to ship one native library, loaded by the Kotlin shell
    # via System.loadLibrary -> JNI.
    add_library(engine_api SHARED
        src/engine_api.cpp
        src/engine_api_android_jni.cpp
    )
else()
    add_library(engine_api SHARED
        src/engine_api.cpp
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
        # 与上游(reAAAq/KrKr2-Next)一致：插件子库(krkr2plugin/psbfile/motionplayer/…)
        # 均用 target_sources(PUBLIC)，源码会经 INTERFACE_SOURCES 直接编进 engine_api.so，
        # 插件注册/全局构造器天然保留，因此这里必须普通链接 krkr2core + krkr2plugin，
        # 不能再包 "--whole-archive"。（曾经的 whole-archive 会强制把 psbfile.a/motionplayer.a
        # 的对象再拉一份，与 engine_api 里已编译的那份重复 → ld.lld duplicate symbol：
        # motion::ResourceManager / PSB::ArchiveType 等。）
        target_link_libraries(engine_api PRIVATE krkr2core krkr2plugin)
        # ANativeWindow (libandroid) + __android_log_print (liblog)
        target_link_libraries(engine_api PRIVATE android log)
        # engine_api_so 既含我们自己的 JNI_OnLoad（存 JavaVM + krkr::JniHelper::setJavaVM，
        # 供 AndroidUtils.cpp 用），又静态链入 SDL2——SDL 的安卓原生 SDL_android.c 也定义
        # JNI_OnLoad，二者强符号重复导致 ld.lld duplicate symbol。用 allow-multiple-definition
        # 让先出现的源码 object（engine_api 源，先于静态库）的 JNI_OnLoad 生效，SDL 的被忽略；
        # SDL 在此仅作音频格式转换/SetMainReady 用，不依赖其安卓 JNI 初始化。
        target_link_options(engine_api PRIVATE "LINKER:--allow-multiple-definition")
    else()
        target_link_libraries(engine_api PRIVATE krkr2core krkr2plugin)
    endif()
endif()

set_target_properties(engine_api PROPERTIES
    OUTPUT_NAME "engine_api"
)
