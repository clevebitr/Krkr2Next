/*
 * omp_env_compat.c —— Berberis(ndk_translation) 翻译环境下的 OpenMP 兼容垫片
 *
 * ── 背景（2026-09 排查结论，详见 HANDOFF.md §1.14）────────────────────────
 * x86_64 模拟器（16KB 页）跑 arm64 库时靠翻译层 libndk_translation(Berberis) 执行
 * arm64 代码，而翻译层给 **arm64 guest** 呈现的 CPU 视图与真实 CPU 集合不一致：
 *
 *     sysconf(_SC_NPROCESSORS_ONLN) / sched_getaffinity / /sys/.../online  → 4
 *     /proc/cpuinfo（guest 视角）                                          → 2
 *     （且没有 x86 的 physical id / core id / apicid 字段 → parsedEntries=0）
 *
 * libomp 18.0.1 默认按 /proc/cpuinfo 建 CPU 拓扑，于是“2 条硬件线程”与“4 个可用
 * OS proc”冲突，在数量一致性断言处直接 abort()：
 *
 *     OMP: Error #13: Assertion failure at kmp_affinity.cpp(4567).
 *
 * 而 libengine_api.so 的**静态构造函数会触碰 OpenMP 运行时**（OMPT / offload /
 * archer / memkind 探测），所以进程在 `krkr2 JNI_OnLoad` 之前就 SIGABRT。Kotlin
 * 侧只表现为“启动即崩”，而且 tombstone 里**没有 `Abort message:`**——libomp 的
 * 致命信息只写 stderr，再直接调 abort()。
 *
 * 实测（探针 APK，见 HANDOFF.md §1.14）：`KMP_AFFINITY=disabled` 能在臂长上绕过；
 * `verbose,none` 不行（仍会建拓扑、仍会断言），即必须**完全跳过** affinity 初始化。
 *
 * ── 谁来加载 ────────────────────────────────────────────────────────────
 * `NativeEngine.init` 中、`System.loadLibrary("engine_api")` **之前**，且只在翻译
 * 环境（`Build.SUPPORTED_ABIS[0] != "arm64-v8a"`，即主 ABI 是 x86_64 而本工程只出
 * arm64 库）加载。真机 arm64 没有这个翻译 bug，不会加载本库、行为完全不变。
 *
 * ── 为什么必须在 arm64 原生代码里 setenv ─────────────────────────────────
 * libomp 是 arm64 guest 库，读的是 **guest libc** 的 `environ`。Java 侧的
 * `android.system.Os.setenv` 走的是宿主 x86_64 libc，改完 guest `getenv` 读到的
 * 仍是 NULL：实测 `Os.setenv(KMP_AFFINITY, disabled) ok` 之后
 * `guest view via native getenv: KMP_AFFINITY=(null)`，engine 照崩。
 *
 * ── 构建 ────────────────────────────────────────────────────────────────
 * 由 `scripts/build_engine_android.sh` 编进 jniLibs（与 libengine_api.so 同一目录）：
 *     aarch64-linux-android24-clang -shared -fPIC -O2 \
 *         -Wl,-z,max-page-size=16384 -o libomp_env_compat.so omp_env_compat.c -llog
 * `-Wl,-z,max-page-size=16384` 不能省：16KB 页设备上 LOAD 段对齐不足会在 dlopen 时
 * 直接报 "program alignment (4096) cannot be smaller than system page size (16384)"。
 */
#include <jni.h>
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "KrKr2Next/omp"

/* 本次进程里是不是我们自己设的（用于日志区分“我们设的”和“环境里本来就有”） */
static int g_krkr_omp_set_by_us = 0;

/*
 * 确保 KMP_AFFINITY 已设成 disabled。
 * 返回值：1 = 本次设置成功；0 = 环境里已有值（尊重用户/调试者，不覆盖）；-1 = 失败。
 */
static int krkr_omp_compat_ensure_disabled(void) {
    const char *cur = getenv("KMP_AFFINITY");
    if (cur != NULL && cur[0] != '\0') {
        return 0;
    }
    if (setenv("KMP_AFFINITY", "disabled", 1) != 0) {
        return -1;
    }
    g_krkr_omp_set_by_us = 1;
    return 1;
}

/*
 * 构造函数（priority 101 = 尽早）在 dlopen 本库时立刻执行，早于本库 JNI_OnLoad，
 * 也早于之后任何对 libengine_api.so / libomp 的加载。
 */
__attribute__((constructor(101)))
static void krkr_omp_compat_ctor(void) {
    (void)krkr_omp_compat_ensure_disabled();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;

    /* 幂等：ctor 里基本已经设过，这里只是兜底 + 打一行可核对的日志。 */
    int r = krkr_omp_compat_ensure_disabled();
    const char *eff = getenv("KMP_AFFINITY");
    const char *src;
    if (g_krkr_omp_set_by_us != 0) {
        src = "set by omp_env_compat";
    } else if (r == 0) {
        src = "pre-existing, left as-is";
    } else if (r < 0) {
        src = "setenv FAILED";
    } else {
        src = "set now";
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "omp_env_compat loaded: KMP_AFFINITY=\"%s\" (%s)",
                        eff != NULL ? eff : "(null)", src);
    return JNI_VERSION_1_6;
}
