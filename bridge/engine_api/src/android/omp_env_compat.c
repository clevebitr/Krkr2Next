/*
 * omp_env_compat.c —— Berberis(ndk_translation) 翻译环境下的 OpenMP 兼容垫片
 * x86_64 模拟器（16KB 页）跑 arm64 库时靠翻译层 libndk_translation(Berberis) 执行
 * arm64 代码，而翻译层给 **arm64 guest** 呈现的 CPU 视图与真实 CPU 集合不一致：
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
