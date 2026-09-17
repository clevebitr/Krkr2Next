/*
 * KrKr2 Engine - Android JNI glue for the engine_api shared library.
 *
 * Bridges the ANativeWindow (SurfaceTexture/Surface) from the host shell's
 * Kotlin code into the engine runtime, which renders into it via a native EGL
 * WindowSurface and eglSwapBuffers (zero-copy).
 *
 * Aligned with upstream reAAAq/KrKr2-Next (krkr2_android.cpp): also stores the
 * JavaVM (for JNI calls from native threads), the Application Context (used by
 * environ/android/AndroidUtils.cpp as a KR2Activity fallback in host-shell
 * mode), and provides a JNI_OnLoad that hands the VM to krkr::JniHelper.
 *
 * Symbols provided:
 *   - krkr_GetJavaVM() / krkr_GetJNIEnv():
 *     JavaVM/JNIEnv for the current thread (attaches if needed).
 *   - krkr_GetApplicationContext(): global Application Context (host-shell
 * mode), returned WITHOUT extra ref (caller must not free).
 *   - krkr_GetNativeWindow() / krkr_GetSurfaceDimensions(): consumed by
 *     engine_api.cpp (Android) for auto-attaching the Surface render target.
 *     krkr_GetNativeWindow returns an ADDITIONAL reference that the caller must
 *     release with ANativeWindow_release().
 *   - JNI entry points called by org.dpdns.clevebitr.core.NativeEngine
 * (Kotlin): nativeSetSurface(window, width, height) / nativeDetachSurface() /
 *     nativeSetApplicationContext(context).
 *
 * ⚠️ JNI 符号名编码了 Java 包名与类名（`.` → `_`，`_` → `_1`）。改动
 * NativeEngine 的包名或类名时，必须同步修改本文件中的三个符号名，否则
 * 只会在运行时以 UnsatisfiedLinkError 暴露。CI 有对应的符号断言。
 *
 * Thread-safety: stored values are guarded by mutexes.
 */

#if defined(__ANDROID__) || defined(ANDROID)

#include <jni.h>
#include <android/log.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "android/KrkrJniHelper.h"
#include "engine_api.h"

#define LOG_TAG "krkr2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

    // ---------------------------------------------------------------------------
    // JavaVM global storage (for JNI calls from any native thread)
    // ---------------------------------------------------------------------------
    std::mutex g_jvm_mutex;
    JavaVM *g_javaVM = nullptr;

    // ---------------------------------------------------------------------------
    // ANativeWindow global storage for the Surface bridge
    // ---------------------------------------------------------------------------
    std::mutex g_surface_mutex;
    ANativeWindow *g_native_window = nullptr; // retained reference
    uint32_t g_surface_width = 0;
    uint32_t g_surface_height = 0;

    // ---------------------------------------------------------------------------
    // Application Context global storage (host-shell mode; KR2Activity may not
    // run)
    // ---------------------------------------------------------------------------
    std::mutex g_context_mutex;
    jobject g_app_context = nullptr; // global ref

} // namespace

// ---------------------------------------------------------------------------
// JavaVM / JNIEnv accessors
// ---------------------------------------------------------------------------

extern "C" JavaVM *krkr_GetJavaVM() {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_javaVM;
}

extern "C" JNIEnv *krkr_GetJNIEnv() {
    JavaVM *vm = krkr_GetJavaVM();
    if(!vm)
        return nullptr;

    JNIEnv *env = nullptr;
    jint status = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if(status == JNI_EDETACHED) {
        if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("Failed to attach current thread to JVM");
            return nullptr;
        }
    }
    return env;
}

// ---------------------------------------------------------------------------
// Application Context accessor
// ---------------------------------------------------------------------------

extern "C" jobject krkr_GetApplicationContext() {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    return g_app_context;
}

// ---------------------------------------------------------------------------
// ANativeWindow accessors
// ---------------------------------------------------------------------------

extern "C" ANativeWindow *krkr_GetNativeWindow() {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if(g_native_window) {
        ANativeWindow_acquire(g_native_window);
    }
    return g_native_window;
}

extern "C" void krkr_GetSurfaceDimensions(uint32_t *out_width,
                                          uint32_t *out_height) {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if(out_width) {
        *out_width = g_surface_width;
    }
    if(out_height) {
        *out_height = g_surface_height;
    }
}

// ---------------------------------------------------------------------------
// JNI_OnLoad: store JavaVM and hand it to krkr::JniHelper (used by
// AndroidUtils)
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void * /*reserved*/) {
    {
        std::lock_guard<std::mutex> lock(g_jvm_mutex);
        g_javaVM = vm;
    }
    krkr::JniHelper::setJavaVM(vm);
    LOGI("krkr2 JNI_OnLoad: JavaVM stored");
    return JNI_VERSION_1_6;
}

/*
 * Java class:  org.dpdns.clevebitr.core.NativeEngine
 * JNI symbol:  Java_org_dpdns_clevebitr_core_NativeEngine_nativeSetSurface
 * Pass null surface to detach.
 */
extern "C" JNIEXPORT void JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_nativeSetSurface(
    JNIEnv *env, jobject /*thiz*/, jobject surface, jint width, jint height) {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if(g_native_window) {
        ANativeWindow_release(g_native_window);
        g_native_window = nullptr;
        g_surface_width = 0;
        g_surface_height = 0;
    }
    if(surface) {
        g_native_window = ANativeWindow_fromSurface(env, surface);
        if(g_native_window) {
            g_surface_width = width > 0 ? static_cast<uint32_t>(width) : 0;
            g_surface_height = height > 0 ? static_cast<uint32_t>(height) : 0;
            LOGI("nativeSetSurface: ANativeWindow acquired (%dx%d)", width,
                 height);
        } else {
            LOGE("nativeSetSurface: ANativeWindow_fromSurface failed");
        }
    } else {
        LOGI("nativeSetSurface: Surface detached (null)");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_nativeDetachSurface(
    JNIEnv * /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if(g_native_window) {
        ANativeWindow_release(g_native_window);
        g_native_window = nullptr;
        g_surface_width = 0;
        g_surface_height = 0;
        LOGI("nativeDetachSurface: ANativeWindow released");
    }
}

/*
 * JNI bridge: Kotlin host shell -> C++ engine.
 * Passes the Android Application Context so engine code (AndroidUtils.cpp)
 * can call Context methods (getExternalFilesDirs / getFilesDir, ...).
 */
extern "C" JNIEXPORT void JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_nativeSetApplicationContext(
    JNIEnv *env, jobject /*thiz*/, jobject context) {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    if(g_app_context) {
        env->DeleteGlobalRef(g_app_context);
        g_app_context = nullptr;
    }
    if(context) {
        g_app_context = env->NewGlobalRef(context);
        LOGI("nativeSetApplicationContext: Application Context stored");
    } else {
        LOGW("nativeSetApplicationContext: null context passed");
    }
}

// ===========================================================================
// Kotlin 宿主壳的 JNI 包装
//
// `engine_*` 系列是普通 C ABI 函数。原 Flutter 壳用 Dart FFI 以 dlsym 直接
// 调用它们；Kotlin/JNI 没有等价的动态符号调用能力，因此这里为宿主所需的每个
// 函数提供一个薄包装。包装只做类型转换，不引入额外状态或逻辑。
//
// 约定：
//   - 句柄用 jlong 传递，0 表示无效。
//   - 返回 engine_result_t 的函数：0 为成功，负值为错误码。
//   - 返回"值"的函数（版本号、启动状态、日志字节数）：失败时返回负值。
//   - 字符串一律 UTF-8。
// ===========================================================================

namespace {

    // jstring -> UTF-8；null 返回空串
    std::string ToUtf8(JNIEnv *env, jstring value) {
        if(value == nullptr)
            return {};
        const char *chars = env->GetStringUTFChars(value, nullptr);
        if(chars == nullptr)
            return {};
        std::string out(chars);
        env->ReleaseStringUTFChars(value, chars);
        return out;
    }

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetRuntimeApiVersion(
    JNIEnv * /*env*/, jobject /*thiz*/) {
    uint32_t version = 0;
    if(engine_get_runtime_api_version(&version) != ENGINE_RESULT_OK)
        return -1;
    return static_cast<jint>(version);
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineCreate(JNIEnv *env,
                                                        jobject /*thiz*/,
                                                        jstring writable_path,
                                                        jstring cache_path) {
    const std::string writable = ToUtf8(env, writable_path);
    const std::string cache = ToUtf8(env, cache_path);

    engine_create_desc_t desc{};
    desc.struct_size = sizeof(engine_create_desc_t);
    desc.api_version = ENGINE_API_VERSION;
    desc.writable_path_utf8 = writable.empty() ? nullptr : writable.c_str();
    desc.cache_path_utf8 = cache.empty() ? nullptr : cache.c_str();

    engine_handle_t handle = nullptr;
    if(engine_create(&desc, &handle) != ENGINE_RESULT_OK)
        return 0;
    return reinterpret_cast<jlong>(handle);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineDestroy(JNIEnv * /*env*/,
                                                         jobject /*thiz*/,
                                                         jlong handle) {
    return static_cast<jint>(
        engine_destroy(reinterpret_cast<engine_handle_t>(handle)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineTick(JNIEnv * /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle,
                                                      jint delta_ms) {
    return static_cast<jint>(
        engine_tick(reinterpret_cast<engine_handle_t>(handle),
                    static_cast<uint32_t>(delta_ms)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_enginePause(JNIEnv * /*env*/,
                                                       jobject /*thiz*/,
                                                       jlong handle) {
    return static_cast<jint>(
        engine_pause(reinterpret_cast<engine_handle_t>(handle)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineResume(JNIEnv * /*env*/,
                                                        jobject /*thiz*/,
                                                        jlong handle) {
    return static_cast<jint>(
        engine_resume(reinterpret_cast<engine_handle_t>(handle)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineCancelTermination(
    JNIEnv * /*env*/, jobject /*thiz*/, jlong handle) {
    return static_cast<jint>(
        engine_cancel_termination(reinterpret_cast<engine_handle_t>(handle)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineResolveWindowClose(
    JNIEnv * /*env*/, jobject /*thiz*/, jlong handle, jint allow_close) {
    return static_cast<jint>(engine_resolve_window_close(
        reinterpret_cast<engine_handle_t>(handle),
        static_cast<int32_t>(allow_close)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineOpenGameAsync(
    JNIEnv *env, jobject /*thiz*/, jlong handle, jstring game_root_path,
    jstring startup_script) {
    const std::string root = ToUtf8(env, game_root_path);
    const std::string script = ToUtf8(env, startup_script);
    return static_cast<jint>(engine_open_game_async(
        reinterpret_cast<engine_handle_t>(handle), root.c_str(),
        script.empty() ? nullptr : script.c_str()));
}

// 返回 engine_startup_state_t（0=IDLE 1=RUNNING 2=SUCCEEDED
// 3=FAILED）；失败返回 -1
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetStartupState(
    JNIEnv * /*env*/, jobject /*thiz*/, jlong handle) {
    uint32_t state = 0;
    if(engine_get_startup_state(reinterpret_cast<engine_handle_t>(handle),
                                &state) != ENGINE_RESULT_OK) {
        return -1;
    }
    return static_cast<jint>(state);
}

// 把启动日志写进调用方提供的 byte[]，返回写入字节数；失败返回 -1
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineDrainStartupLogs(
    JNIEnv *env, jobject /*thiz*/, jlong handle, jbyteArray buffer) {
    if(buffer == nullptr)
        return -1;
    const jsize len = env->GetArrayLength(buffer);
    if(len <= 0)
        return 0;

    std::vector<char> tmp(static_cast<size_t>(len));
    uint32_t written = 0;
    const engine_result_t rc = engine_drain_startup_logs(
        reinterpret_cast<engine_handle_t>(handle), tmp.data(),
        static_cast<uint32_t>(len), &written);
    if(rc != ENGINE_RESULT_OK)
        return -1;

    if(written > 0) {
        env->SetByteArrayRegion(buffer, 0, static_cast<jsize>(written),
                                reinterpret_cast<const jbyte *>(tmp.data()));
    }
    return static_cast<jint>(written);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineSetSurfaceSize(
    JNIEnv * /*env*/, jobject /*thiz*/, jlong handle, jint width, jint height) {
    return static_cast<jint>(engine_set_surface_size(
        reinterpret_cast<engine_handle_t>(handle), static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineSetOption(
    JNIEnv *env, jobject /*thiz*/, jlong handle, jstring key, jstring value) {
    const std::string k = ToUtf8(env, key);
    const std::string v = ToUtf8(env, value);

    engine_option_t option{};
    option.key_utf8 = k.c_str();
    option.value_utf8 = v.c_str();
    return static_cast<jint>(
        engine_set_option(reinterpret_cast<engine_handle_t>(handle), &option));
}

extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineSetLogFilePath(
    JNIEnv *env, jobject /*thiz*/, jstring path) {
    const std::string p = ToUtf8(env, path);
    return static_cast<jint>(engine_set_log_file_path(p.c_str()));
}

// 逐字段传递而不是传结构体，避免 Kotlin 侧做内存布局与对齐匹配。
// key_code 必须是 Windows VK 码（见 README.md「硬约束」）。
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineSendInput(
    JNIEnv * /*env*/, jobject /*thiz*/, jlong handle, jint type, jdouble x,
    jdouble y, jdouble delta_x, jdouble delta_y, jint pointer_id, jint button,
    jint key_code, jint modifiers, jint unicode_codepoint,
    jlong timestamp_micros) {
    engine_input_event_t event{};
    event.struct_size = sizeof(engine_input_event_t);
    event.type = static_cast<uint32_t>(type);
    event.timestamp_micros = static_cast<uint64_t>(timestamp_micros);
    event.x = x;
    event.y = y;
    event.delta_x = delta_x;
    event.delta_y = delta_y;
    event.pointer_id = pointer_id;
    event.button = button;
    event.key_code = key_code;
    event.modifiers = modifiers;
    event.unicode_codepoint = static_cast<uint32_t>(unicode_codepoint);

    return static_cast<jint>(
        engine_send_input(reinterpret_cast<engine_handle_t>(handle), &event));
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetLastError(JNIEnv *env,
                                                              jobject /*thiz*/,
                                                              jlong handle) {
    const char *msg =
        engine_get_last_error(reinterpret_cast<engine_handle_t>(handle));
    return env->NewStringUTF(msg != nullptr ? msg : "");
}

// 渲染器信息写进调用方提供的 byte[]，返回写入字节数；失败返回 -1
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetRendererInfo(
    JNIEnv *env, jobject /*thiz*/, jlong handle, jbyteArray buffer) {
    if(buffer == nullptr)
        return -1;
    const jsize len = env->GetArrayLength(buffer);
    if(len <= 0)
        return 0;

    std::vector<char> tmp(static_cast<size_t>(len));
    const engine_result_t rc =
        engine_get_renderer_info(reinterpret_cast<engine_handle_t>(handle),
                                 tmp.data(), static_cast<uint32_t>(len));
    if(rc != ENGINE_RESULT_OK)
        return -1;

    // engine_get_renderer_info 写入的是 NUL 结尾字符串
    const size_t written = strnlen(tmp.data(), static_cast<size_t>(len));
    if(written > 0) {
        env->SetByteArrayRegion(buffer, 0, static_cast<jsize>(written),
                                reinterpret_cast<const jbyte *>(tmp.data()));
    }
    return static_cast<jint>(written);
}

// 内存/缓存统计。按**固定顺序**把 engine_memory_stats_t 的字段写进调用方的
// long[]， 返回写入的字段数；失败返回 -1。
//
// 为什么用数组而不是新建 Java 对象：性能叠加层会按 4Hz 轮询它，数组只需一次
// SetLongArrayRegion，省掉类查找与对象分配。顺序在 Kotlin 侧有一份对应的解析
// （EngineSession.MemoryStats），两边必须同步修改。
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetMemoryStats(
    JNIEnv *env, jobject /*thiz*/, jlong handle, jlongArray out) {
    constexpr jsize kFieldCount = 16;
    if(out == nullptr || env->GetArrayLength(out) < kFieldCount)
        return -1;

    engine_memory_stats_t stats{};
    stats.struct_size = sizeof(stats);
    const engine_result_t rc = engine_get_memory_stats(
        reinterpret_cast<engine_handle_t>(handle), &stats);
    if(rc != ENGINE_RESULT_OK)
        return -1;

    const jlong fields[kFieldCount] = {
        static_cast<jlong>(stats.self_used_mb),
        static_cast<jlong>(stats.system_free_mb),
        static_cast<jlong>(stats.system_total_mb),
        static_cast<jlong>(stats.graphic_cache_bytes),
        static_cast<jlong>(stats.graphic_cache_limit_bytes),
        static_cast<jlong>(stats.xp3_segment_cache_bytes),
        static_cast<jlong>(stats.psb_cache_bytes),
        static_cast<jlong>(stats.psb_cache_entries),
        static_cast<jlong>(stats.psb_cache_entry_limit),
        static_cast<jlong>(stats.psb_cache_hits),
        static_cast<jlong>(stats.psb_cache_misses),
        static_cast<jlong>(stats.archive_cache_entries),
        static_cast<jlong>(stats.archive_cache_limit),
        static_cast<jlong>(stats.autopath_cache_entries),
        static_cast<jlong>(stats.autopath_cache_limit),
        static_cast<jlong>(stats.autopath_table_entries),
    };
    env->SetLongArrayRegion(out, 0, kFieldCount, fields);
    return kFieldCount;
}

// 当前生效的兼容档，写成 `<profile> <mode>`（如 `krkrz-kag kag`），供性能叠加层
// 显示"这个游戏按哪条血脉跑"。引擎侧不校验调用线程，所以壳按 4Hz 从任意线程轮询；
// 还没定档时写入长度为 0。
extern "C" JNIEXPORT jint JNICALL
Java_org_dpdns_clevebitr_core_NativeEngine_engineGetCompatProfile(
    JNIEnv *env, jobject /*thiz*/, jbyteArray buffer) {
    if(buffer == nullptr)
        return -1;
    const jsize len = env->GetArrayLength(buffer);
    if(len <= 0)
        return 0;

    std::vector<char> tmp(static_cast<size_t>(len));
    const engine_result_t rc =
        engine_get_compat_profile(tmp.data(), static_cast<uint32_t>(len));
    if(rc != ENGINE_RESULT_OK)
        return -1;

    // engine_get_compat_profile 写入的是 NUL 结尾字符串
    const size_t written = strnlen(tmp.data(), static_cast<size_t>(len));
    if(written > 0) {
        env->SetByteArrayRegion(buffer, 0, static_cast<jsize>(written),
                                reinterpret_cast<const jbyte *>(tmp.data()));
    }
    return static_cast<jint>(written);
}

#endif // __ANDROID__