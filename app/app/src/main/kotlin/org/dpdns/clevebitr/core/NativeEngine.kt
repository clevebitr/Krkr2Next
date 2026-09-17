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

    /**
     * 取消"游戏请求退出"（即让 [engineTick] 返回 [RESULT_GAME_TERMINATED] 的那个状态）。
     *
     * 宿主在确认框里选了"继续游戏"时调用：引擎只清掉终止标志（期间并没有拆过任何
     * 东西），下一帧就照常运行。**必须在渲染线程调用**（与 engineTick 同线程）——
     * [EngineSession.cancelGameTermination] 负责切线程。幂等。
     */
    external fun engineCancelTermination(handle: Long): Int

    /**
     * 答复"游戏请求关窗"（即让 [engineTick] 返回 [RESULT_WINDOW_CLOSE_REQUESTED] 的
     * 那个状态）。
     *
     * @param allowClose 非 0=退出：引擎执行真正的关窗，下一帧 tick 返回
     *   [RESULT_WINDOW_CLOSED]；0=继续游戏：丢掉请求，什么都没拆，游戏从原处接着跑。
     *
     * **必须在渲染线程调用**（与 engineTick 同线程）—— [EngineSession.resolveWindowClose]
     * 负责切线程。幂等。
     */
    external fun engineResolveWindowClose(handle: Long, allowClose: Int): Int

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

    /**
     * 游戏自己要求退出（TJS `System.exit()`）。**不是**错误：`engineTick` 用它
     * 通知宿主"离开游戏界面并销毁引擎"。宿主模式不会因此结束进程，所以不接手的
     * 表现是画面冻结 + 每次 tick 都返回本码。取值与 `engine_api.h` 的
     * `ENGINE_RESULT_GAME_TERMINATED` 一致。
     */
    const val RESULT_GAME_TERMINATED = -6

    /**
     * 游戏仍在启动（StartApplication 在 worker 线程里跑）。**不是**错误：
     * 启动期每帧都会返回它，当成失败累计的话每次开游戏就多出 300+ 个错误
     * （真机 app.log 里 `engineTick failed x301/x304/x373` 全是这一条）。
     * 取值与 `engine_api.h` 的 `ENGINE_RESULT_STARTUP_PENDING` 一致。
     */
    const val RESULT_STARTUP_PENDING = -7

    /**
     * 游戏**关掉了自己的窗口**而终止（宿主模式不结束进程）。与
     * [RESULT_GAME_TERMINATED] 分开：窗口已经没了，宿主要直接离开游戏界面，
     * **不能**提供"继续游戏"——撤销后只是在空场景上继续跑（真机实测画面彻底不动）。
     */
    const val RESULT_WINDOW_CLOSED = -8

    /**
     * 游戏**请求关闭窗口**（KAG 退出菜单：`kag.close()` → `Window.close()`），但引擎把
     * 关闭挂起等宿主确认。此刻什么都没拆：窗口照常绘制、脚本状态完好。
     *
     * **不是错误**（壳不要计数）。宿主必须弹确认框并调 [engineResolveWindowClose] 答复：
     * 非 0 退出（下一帧返回 [RESULT_WINDOW_CLOSED]），0 继续游戏（游戏从原处继续）。
     * 取值与 `engine_api.h` 的 `ENGINE_RESULT_WINDOW_CLOSE_REQUESTED` 一致。
     */
    const val RESULT_WINDOW_CLOSE_REQUESTED = -9

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
