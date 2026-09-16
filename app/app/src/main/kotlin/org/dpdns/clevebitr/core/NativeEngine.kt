package org.dpdns.clevebitr.core

import android.content.Context
import android.view.Surface

/**
 * `libengine_api.so` 的 JNI 绑定（`engine_api` C ABI）。
 *
 * ⚠️ 每个 external 方法都对应 `bridge/engine_api/src/engine_api_android_jni.cpp`
 * 里的一个 `Java_org_dpdns_clevebitr_core_NativeEngine_<方法名>` 符号。JNI 符号名编码了
 * **包名与类名**（`.` → `_`，`_` → `_1`），所以本文件所在的包、对象名、方法名
 * 都不能单方面修改：不一致只会在运行时以 `UnsatisfiedLinkError` 暴露，编译期
 * 不会报错。CI 有一条符号一致性断言专门拦这个。
 *
 * 返回 `engine_result_t` 的方法：0 为成功，负值为错误码（见下方常量）。
 * 返回"值"的方法（版本号、启动状态、字节数）：失败返回负值。
 */
object NativeEngine {

    init {
        System.loadLibrary("engine_api")
    }

    // ── 版本 ──────────────────────────────────────────────────────────────
    /** @return API 版本号（如 0x01000000），失败返回 -1 */
    external fun engineGetRuntimeApiVersion(): Int

    // ── 生命周期 ──────────────────────────────────────────────────────────
    /** @return 引擎句柄；失败返回 0 */
    external fun engineCreate(writablePath: String, cachePath: String): Long

    external fun engineDestroy(handle: Long): Int

    /** @param deltaMs 距上一帧的毫秒数 */
    external fun engineTick(handle: Long, deltaMs: Int): Int

    external fun enginePause(handle: Long): Int
    external fun engineResume(handle: Long): Int

    // ── 打开游戏 ──────────────────────────────────────────────────────────
    /** @param startupScript 传 null 用默认启动脚本 */
    external fun engineOpenGameAsync(
        handle: Long,
        gameRootPath: String,
        startupScript: String?,
    ): Int

    /** @return [STARTUP_IDLE] / [STARTUP_RUNNING] / [STARTUP_SUCCEEDED] / [STARTUP_FAILED]；失败返回 -1 */
    external fun engineGetStartupState(handle: Long): Int

    /** 把启动日志写进 [buffer]，@return 写入字节数；失败返回 -1 */
    external fun engineDrainStartupLogs(handle: Long, buffer: ByteArray): Int

    // ── 渲染与配置 ────────────────────────────────────────────────────────
    external fun engineSetSurfaceSize(handle: Long, width: Int, height: Int): Int

    external fun engineSetOption(handle: Long, key: String, value: String): Int

    external fun engineSetLogFilePath(path: String): Int

    /** 渲染器信息写进 [buffer]，@return 写入字节数；失败返回 -1 */
    external fun engineGetRendererInfo(handle: Long, buffer: ByteArray): Int

    /**
     * 当前生效的游戏兼容档（`<profile> <mode>`，如 `krkrz-kag kag`）写进 [buffer]，
     * @return 写入字节数；失败返回 -1。**不需要 handle**，任意线程可调。
     */
    external fun engineGetCompatProfile(buffer: ByteArray): Int

    /**
     * 内存/缓存统计写进 [out]（长度需 ≥ [MEMORY_STATS_FIELDS]），
     * @return 写入的字段数；失败返回 -1。
     *
     * 字段顺序与 `engine_api_android_jni.cpp` 里那个 `kFieldCount` 数组一一对应，
     * 解析见 `EngineSession.MemoryStats`。两边必须同步改。
     */
    external fun engineGetMemoryStats(handle: Long, out: LongArray): Int

    // ── 输入 ──────────────────────────────────────────────────────────────
    /**
     * @param keyCode **Windows VK 码**，不是 Android `KEYCODE_*`——
     *   见 `README.md`「硬约束」与 [VkCodes]。
     * @param x,y 视图坐标（物理像素），不要乘 density。
     */
    external fun engineSendInput(
        handle: Long,
        type: Int,
        x: Double,
        y: Double,
        deltaX: Double,
        deltaY: Double,
        pointerId: Int,
        button: Int,
        keyCode: Int,
        modifiers: Int,
        unicodeCodepoint: Int,
        timestampMicros: Long,
    ): Int

    external fun engineGetLastError(handle: Long): String

    // ── Surface 与 Application Context 交接 ───────────────────────────────
    /**
     * 把一个 Surface 交给引擎。传 null 表示分离。
     *
     * 必须是 **SurfaceView 系**（`SurfaceHolder.getSurface()`）而非 TextureView：
     * 引擎用 `eglSwapBuffers` 直出，这条路径依赖 SurfaceView 的独立 surface。
     */
    external fun nativeSetSurface(surface: Surface?, width: Int, height: Int)

    external fun nativeDetachSurface()

    /** 供 `AndroidUtils.cpp` 在拿不到 Activity 时回退使用（getExternalFilesDirs 等）。 */
    external fun nativeSetApplicationContext(context: Context)

    // ── 结果码 ────────────────────────────────────────────────────────────
    const val RESULT_OK = 0
    const val RESULT_INVALID_ARGUMENT = -1
    const val RESULT_INVALID_STATE = -2
    const val RESULT_NOT_SUPPORTED = -3
    const val RESULT_IO_ERROR = -4
    const val RESULT_INTERNAL_ERROR = -5

    // ── 启动状态 ──────────────────────────────────────────────────────────
    const val STARTUP_IDLE = 0
    const val STARTUP_RUNNING = 1
    const val STARTUP_SUCCEEDED = 2
    const val STARTUP_FAILED = 3

    // ── 性能叠加层 ────────────────────────────────────────────────────────
    /** [engineGetMemoryStats] 一次写入的字段数（Kotlin 侧解析依赖它）。 */
    const val MEMORY_STATS_FIELDS = 16

    /** [engineGetCompatProfile] 建议的缓冲区长度（最长档名 + 模式名，留足余量）。 */
    const val COMPAT_PROFILE_BUFFER_SIZE = 64
}
