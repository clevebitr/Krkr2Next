package org.dpdns.clevebitr.core

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.view.Choreographer
import android.view.Surface

/**
 * 引擎会话：持有渲染线程与引擎句柄。
 *
 * ## 为什么必须单线程
 *
 * `eglMakeCurrent` 是**线程绑定**的，而 `engine_tick` 会在**调用它的线程**上自动
 * attach ANativeWindow 并创建 EGL context（`engine_api.cpp` 的 Android 分支）。
 * 因此 `engine_create` → 所有 `engine_tick` → `engine_destroy` 必须始终在同一条
 * 线程上执行，否则 EGL 上下文与当前线程不匹配，表现为黑屏或 GL 调用静默失败。
 *
 * 本类用一条 `HandlerThread` 承载上述全部调用；UI 线程只允许调用
 * [attachSurface] / [detachSurface]（它们内部只存 ANativeWindow，不碰 EGL）。
 *
 * ## 帧节拍
 *
 * 循环用 `Choreographer` 驱动：它在构造它的线程上绑定该线程的 Looper，因此可以
 * 脱离 UI 线程获得 vsync 节拍。节拍本身由 vsync 决定；需要更低的上限时交给引擎的
 * `fps_limit`（它会在间隔不足时直接跳过整个 tick）。
 */
class EngineSession(
    private val writablePath: String,
    private val cachePath: String,
    /** 引擎帧率上限；0 = 不限速，跟随 vsync。 */
    private val fpsLimit: Int = 0,
    /**
     * 引擎字体回退策略（`auto` / `legacy` / `chain`，见 [AppPrefs.FONT_FALLBACK_MODES]）。
     * 引擎侧只在初始化时读一次，所以是"下次开游戏生效"。
     */
    private val fontFallbackMode: String = "auto",
    /**
     * krkrz 的 OGLDrawDevice 兼容档位（`off` / `alias` / `kag`，
     * 见 [AppPrefs.OGLDRAWDEVICE_COMPAT_MODES]）。引擎侧在插件注册时读一次，
     * 所以是"下次开游戏生效"。
     */
    private val oglDrawDeviceCompat: String = "off",
    /**
     * 游戏兼容档（`auto` / `kirikiri2-classic` / `krkrz-gpu` / `krkrz-kag` /
     * `krkrz-ogl`，见 [AppPrefs.GAME_COMPAT_PROFILES]）。
     *
     * `auto` 时由引擎按**血脉标记**判档（不看游戏名），所以要在 `openGame()` 里
     * 连同游戏根目录一起传进去 —— 插件是在 `StartApplication`（post-regist）
     * 读 `ogldrawdevice_compat` 的，晚于 [start] 里的选项下发。
     * 非 `auto` 时这份取值与 [oglDrawDeviceCompat] 一起下发，显式模式优先。
     */
    private val gameCompatProfile: String = "auto",
    /**
     * 引擎日志。**在渲染线程回调**——只做日志落盘/打印，不要在这里碰 UI 状态。
     */
    private val onLog: (String) -> Unit = {},
    /**
     * 启动状态变化。**已切到主线程回调**，可以直接写 Compose 状态。
     */
    private val onStartupStateChanged: (Int) -> Unit = {},
    /**
     * 不可恢复的错误（引擎创建失败、游戏启动失败）。**已切到主线程回调**。
     */
    private val onFatal: (String) -> Unit = {},
    /**
     * 游戏自己要求退出（TJS `System.exit()` / 游戏内"退出游戏"）。
     *
     * **已切到主线程回调**，且只会回调一次。宿主应当**先弹确认框问用户**：
     *   - 确认退出 → 离开游戏界面并销毁本会话（`MainActivity.exitToLauncher()`）；
     *   - 用户选"继续" → 调 [cancelGameTermination]，游戏从当前进度继续。
     * 此刻引擎处于"终止挂起"状态：不再渲染，但**什么都没拆**（所以可以取消）。
     */
    private val onGameExitRequested: () -> Unit = {},
    /**
     * 游戏**关掉自己的窗口**而退出（与 [onGameExitRequested] 区分）。**已切到主线程
     * 回调**，只回调一次。宿主应直接离开游戏界面：窗口已经没了，"继续游戏"无从谈起。
     */
    private val onWindowClosed: () -> Unit = {},
    /**
     * 游戏**请求关闭窗口**（KAG 退出菜单走的就是这条）。**已切到主线程回调**，只回调
     * 一次。此刻引擎什么都没拆，只是把这一帧停住等答复：
     *   - 确认退出 → [resolveWindowClose]`(true)`（下一帧报 [onWindowClosed]）；
     *   - 继续游戏 → [resolveWindowClose]`(false)`，游戏从原处接着跑。
     */
    private val onWindowCloseRequested: () -> Unit = {},
) {
    companion object {
        private const val TAG = "KrKr2Next/Engine"
        private const val THREAD_NAME = "krkr-render"
        private const val LOG_BUFFER_SIZE = 8 * 1024

        /** 启动状态轮询间隔（毫秒），与 tick 同频即可 */
        private const val STARTUP_POLL_FRAMES = 6

        /** 单帧最大 tick 步长，避免切回前台时 delta 过大导致脚本时间跳变 */
        private const val MAX_DELTA_MS = 100L
        /** 性能采样日志间隔。 */
        private const val PERF_LOG_INTERVAL_NANOS = 5_000_000_000L

        /**
         * `engineTick` 失败日志的最小间隔。tick 是每帧调用的，失败时逐帧记录会刷爆日志；
         * 5 秒一条既能看出持续失败，也不会淹没别的信息。
         */
        private const val TICK_FAILURE_LOG_INTERVAL_MS = 5_000L

        /** FPS 统计窗口长度；窗口越短越跟手，越容易被单帧抖动带偏。 */
        private const val FPS_WINDOW_NANOS = 500_000_000L

        /**
         * 帧时间分位数的统计窗口。AetherKiri 的性能叠加层就是每秒结算一次（对窗口内
         * 原始帧时间排序后取分位数），这里对齐它，detail 档的 P50/P95/P99/Max 才有
         * 可比性。
         */
        private const val PERF_WINDOW_NANOS = 1_000_000_000L

        /** 分位数窗口的样本上限：1 秒 @240fps 也才 240 个，512 足够且不会增长。 */
        private const val PERF_SAMPLE_CAPACITY = 512

        /** 引擎统计（内存/渲染器）的采样间隔：叠加层按 4Hz 读缓存，采样 2Hz 足够。 */
        private const val ENGINE_INFO_SAMPLE_NANOS = 500_000_000L

        /** 渲染器信息缓冲区大小（当前实现返回几十字节的 key=value 串）。 */
        private const val RENDERER_INFO_BUFFER_SIZE = 1024
    }

    private var thread: HandlerThread? = null
    private var handler: Handler? = null
    private var choreographer: Choreographer? = null

    /**
     * 主线程 Handler。引擎回调发生在渲染线程，但消费方（Compose 状态）要求主线程，
     * 这里统一做一次切换，免得每个调用方各自记得切。
     */
    private val mainHandler = Handler(Looper.getMainLooper())

    private fun postToMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else mainHandler.post(block)
    }

    /**
     * 引擎句柄。只在渲染线程写入，但 [sendInput] 会从 UI 线程读它，
     * 因此必须是 volatile 以保证 64 位读写原子可见。
     */
    @Volatile private var handle: Long = 0L

    private var lastFrameNanos = 0L
    private var frameCounter = 0L

    /**
     * 最近一个统计窗口内的 tick 速率（帧/秒）。渲染线程写、任意线程读——只有 FPS
     * 叠加层会读它，读到上一窗口的值无关紧要。
     *
     * 它是**调用 `engineTick` 的频率**：默认（`fps_limit=0`）每个 tick 都会渲染，
     * 数字就等于渲染帧率；设了上限时引擎会跳过一部分 tick，数字是引擎节拍而不是
     * 实际上屏帧数。
     */
    @Volatile
    var measuredFps: Float = 0f
        private set

    /** FPS 统计窗口的起点与窗口内 tick 数。只在渲染线程写。 */
    private var fpsWindowStartNanos = 0L
    private var fpsWindowTicks = 0

    /**
     * 上一帧的原始帧间隔（毫秒，**未平滑**）。与 AetherKiri 的 `Frame: %.2f ms`
     * 同义：它显示的就是 delta，不做平均，抖动要能直接看见。
     */
    @Volatile
    var frameMs: Float = 0f
        private set

    /** 上一次 `engineTick` 的耗时（毫秒）。对应 AetherKiri detail 档的 `Tick`。 */
    @Volatile
    var tickMs: Float = 0f
        private set

    /**
     * 性能采样日志的下一次到期时刻（纳秒）。
     *
     * 为什么要有它：release 构建里引擎的渲染探针全部关着，`engine.log` 里**没有任何
     * 帧率信息**，于是"这个游戏卡"只能靠人肉盯叠加层。这条日志让壳每 5 秒把同一组
     * 数字写进 `app.log`，事后翻日志就能定位是哪一段慢（帧时 / tick / 宿主）。
     * 5 秒一条，量级可忽略。
     */
    private var nextPerfLogNanos = 0L

    /** 单位时间窗口内的帧时间分位数（毫秒）。对应 AetherKiri detail 档的 P50/P95/P99/Max。 */
    @Volatile
    var frameP50Ms: Float = 0f
        private set

    @Volatile
    var frameP95Ms: Float = 0f
        private set

    @Volatile
    var frameP99Ms: Float = 0f
        private set

    @Volatile
    var frameMaxMs: Float = 0f
        private set

    /** 累计 tick 失败次数；叠加层的 `Errors` 用它。只在渲染线程写。 */
    val tickFailureCount: Long get() = tickFailures

    /** 分位数窗口的样本与起点。只在渲染线程读写。 */
    private val frameSamples = FloatArray(PERF_SAMPLE_CAPACITY)
    private var frameSampleCount = 0
    private var perfWindowStartNanos = 0L

    /** 渲染器信息的缓存与取用时间戳；方法带同步，故缓冲区可以复用。 */
    @Volatile
    private var rendererInfoCache: String = ""
    private val rendererInfoBuffer = ByteArray(RENDERER_INFO_BUFFER_SIZE)

    /**
     * 引擎统计的**采样缓存**：`engineGetMemoryStats` / `engineGetRendererInfo` 会拿
     * 引擎帧锁，而帧锁在模态对话框（`Window.showModal`）与 CG 视频这类长帧期间可能
     * 被持有几秒到几十秒。叠加层是按 4Hz 从 UI 线程轮询的 —— 主线程在那把锁上等
     * 超过 5s 就是系统 ANR（真机 2026-09-18 两次：模态 13:48:31 进入、13:48:38 被判
     * ANR；CG 视频 13:49:2x 同样）。所以改成**渲染线程定期采样、UI 线程只读缓存**。
     */
    @Volatile
    private var cachedMemoryStats: MemoryStats? = null
    private var lastEngineInfoSampleNanos = 0L
    private val memoryStatsScratch = LongArray(NativeEngine.MEMORY_STATS_FIELDS)

    /** 累计 tick 失败次数，供限频日志带出"偶发还是彻底坏了"。只在渲染线程写。 */
    private var tickFailures = 0L

    /**
     * 累计输入被拒次数。输入在调用线程（UI 线程）直接投递，读它的是渲染线程的
     * 性能日志，所以必须是 volatile。
     */
    @Volatile
    private var sendFailures = 0L

    /** 上次上报过的启动状态，用于抑制重复上报（见 [pollStartupState]）。只在渲染线程写。 */
    private var lastReportedState = -1

    /**
     * 是否已收到 surface。attach/detach 允许从任意线程调用，因此是 volatile；
     * 它只用于那条"启动成功但没画面"的提示。
     */
    @Volatile
    private var surfaceAttached = false

    @Volatile private var paused = false
    @Volatile private var running = false
    @Volatile private var destroyed = false

    /**
     * 游戏是否已请求退出。只用来把"请宿主收尾"收敛成一次（引擎会一直返回
     * `RESULT_GAME_TERMINATED`）。只在渲染线程读写。
     */
    private var gameTerminated = false

    /**
     * 游戏是否已请求关窗。同样只为把"请宿主弹确认框"收敛成一次（挂起期间引擎会一直
     * 返回 `RESULT_WINDOW_CLOSE_REQUESTED`）。[resolveWindowClose] 之外只在渲染线程读写。
     */
    private var windowCloseRequested = false

    private val logBuffer = ByteArray(LOG_BUFFER_SIZE)

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!running) return

            val deltaMs = if (lastFrameNanos == 0L) {
                16L
            } else {
                ((frameTimeNanos - lastFrameNanos) / 1_000_000L).coerceIn(1L, MAX_DELTA_MS)
            }
            lastFrameNanos = frameTimeNanos

            if (!paused && handle != 0L) {
                trackFps(frameTimeNanos)
                trackFrameTime(frameTimeNanos, deltaMs)

                // tick 耗时：叠加层 detail 档要区分「引擎慢」还是「宿主调度慢」
                val tickStartNanos = System.nanoTime()
                val rc = NativeEngine.engineTick(handle, deltaMs.toInt())
                tickMs = (System.nanoTime() - tickStartNanos) / 1_000_000f
                logPerfIfDue(tickStartNanos)
                // 引擎统计采样（渲染线程）：UI 线程只读缓存，绝不自己去拿引擎帧锁。
                sampleEngineInfoIfDue(frameTimeNanos)
                if (rc != NativeEngine.RESULT_OK) {
                    if (rc == NativeEngine.RESULT_GAME_TERMINATED) {
                        // 游戏自己要求退出（TJS `System.exit()`）。这**不是**错误：
                        // 引擎此后不再渲染，只会一直返回本码。
                        // 停掉帧循环，请宿主**弹确认框问用户**：确认退出就走
                        // exitToLauncher()；选"继续"就调 cancelGameTermination()
                        // 撤销终止标志并恢复帧循环（幂等：只上报一次）。
                        if (!gameTerminated) {
                            gameTerminated = true
                            running = false
                            AppLog.i(TAG, "游戏请求退出（engineTick 返回 GAME_TERMINATED），等宿主确认")
                            postToMain { onGameExitRequested() }
                            // 直接结束本帧：不再 post 下一帧，也不轮到
                            // pollStartupState —— 否则"启动期退出"会被它报成启动失败
                            // （onFatal），宿主会多弹一个错误框。
                            return
                        }
                    } else if (rc == NativeEngine.RESULT_WINDOW_CLOSED) {
                        // 游戏关掉了自己的窗口（不是脚本 System.exit()）。窗口已经
                        // 没了，问用户"要不要继续"没有意义：撤销后只是在空场景上继续
                        // 跑（真机实测画面彻底不动）。直接离开游戏界面，并且不再
                        // 回调确认框。
                        if (!gameTerminated) {
                            gameTerminated = true
                            running = false
                            AppLog.i(TAG, "游戏关窗退出（engineTick 返回 WINDOW_CLOSED），直接收尾")
                            postToMain { onWindowClosed() }
                            return
                        }
                    } else if (rc == NativeEngine.RESULT_WINDOW_CLOSE_REQUESTED) {
                        // 游戏**请求**关窗（KAG 退出菜单）：引擎把关闭挂起、什么都没拆，
                        // 等宿主确认。这不是错误（不计 tickFailures），帧循环也**不停**
                        // —— 用户选"继续游戏"后 resolveWindowClose(false) 一清，下一帧
                        // 就照常跑，不需要再接回帧循环。只上报一次，避免每帧弹框。
                        if (!windowCloseRequested) {
                            windowCloseRequested = true
                            AppLog.i(TAG, "游戏请求关闭窗口（engineTick 返回 WINDOW_CLOSE_REQUESTED），等宿主确认")
                            postToMain { onWindowCloseRequested() }
                        }
                    } else if (rc == NativeEngine.RESULT_STARTUP_PENDING) {
                        // 游戏仍在启动（StartApplication 在 worker 线程里跑）。
                        // 这**不是**错误：以前它走下面的分支，于是每次开游戏都会记下
                        // 300+ 次"失败"（真机 app.log: engineTick failed x301/x304/x373，
                        // err=engine startup is still running），叠加层的错误数就是这么
                        // 涨起来的。启动状态本身由 pollStartupState 上报，这里什么都不做。
                    } else {
                        // 每帧都能失败，逐帧记录会把日志刷爆（60 行/秒）。限频到 5 秒一条，
                        // 并把次数带上——次数本身是判断"偶发一次"还是"彻底坏了"的关键。
                        tickFailures++
                        AppLog.wLimited(
                            TAG,
                            "engineTick",
                            TICK_FAILURE_LOG_INTERVAL_MS,
                        ) { "engineTick failed x$tickFailures (最近一次 rc=$rc err=${lastError()})" }
                    }
                }
                if (++frameCounter % STARTUP_POLL_FRAMES == 0L) {
                    pollStartupState()
                }
                // 启动成功却始终没有 surface，表现是"日志说成功、屏幕全黑"。
                // 少了这条提示，这种情况只能靠反查日志里有没有 attachSurface 才发现
                // ——真实踩过一次。限频记录，避免每帧刷屏。
                if(lastReportedState == NativeEngine.STARTUP_SUCCEEDED &&
                   !surfaceAttached) {
                    AppLog.wLimited(TAG, "noSurface", 3_000L) {
                        "引擎已启动成功，但还没有收到 surface（画面会是黑的）：" +
                            "检查 GameScreen 是否真的被组合、SurfaceView 是否回调了 surfaceChanged"
                    }
                }
            }

            choreographer?.postFrameCallback(this)
        }
    }

    /**
     * 每 [PERF_LOG_INTERVAL_NANOS] 打一条性能采样。
     *
     * 字段口径与叠加层一致（见 [PerfSnapshot]）：`frame` 是原始帧间隔，`tick` 是
     * `engineTick` 的耗时，两者相减就是宿主自己的开销。`fps` 是 1 秒窗的均值。
     * 只在引擎真的在跑的时候打——暂停时打出来全是 0，只会干扰判断。
     */
    private fun logPerfIfDue(nowNanos: Long) {
        if (nextPerfLogNanos == 0L) {
            nextPerfLogNanos = nowNanos + PERF_LOG_INTERVAL_NANOS
            return
        }
        if (nowNanos < nextPerfLogNanos) return
        nextPerfLogNanos = nowNanos + PERF_LOG_INTERVAL_NANOS
        AppLog.i(
            TAG,
            "perf: fps=${"%.1f".format(measuredFps)} frame=${"%.2f".format(frameMs)}ms " +
                "tick=${"%.2f".format(tickMs)}ms update=${"%.2f".format(frameMs - tickMs)}ms " +
                "p95=${"%.2f".format(frameP95Ms)}ms errors=$tickFailures",
        )
    }

    // ── 生命周期 ──────────────────────────────────────────────────────────

    /** 启动渲染线程并创建引擎。返回 false 表示线程创建成功但引擎创建失败。 */
    fun start() {
        if (thread != null) return

        val ht = HandlerThread(THREAD_NAME).also { it.start() }
        thread = ht
        val h = Handler(ht.looper)
        handler = h

        h.post {
            // Choreographer 在构造它的线程上绑定 Looper，所以必须在渲染线程内取
            choreographer = Choreographer.getInstance()

            handle = NativeEngine.engineCreate(writablePath, cachePath)
            if (handle == 0L) {
                AppLog.e(TAG, "engineCreate failed (writable=$writablePath cache=$cachePath)")
                postToMain { onFatal("引擎初始化失败") }
                return@post
            }
            AppLog.i(TAG, "engineCreate ok, apiVersion=0x${NativeEngine.engineGetRuntimeApiVersion().toString(16)}")

            applyOption("fps_limit", fpsLimit.toString())
            applyOption("font_fallback_mode", fontFallbackMode)
            // 手动档位（非 off）优先下发：引擎侧也是"显式值优先于判档结果"。
            // off 视为"没手动指定"，交给 openGame() 的兼容档判档决定。
            if (oglDrawDeviceCompat != "off") {
                applyOption("ogldrawdevice_compat", oglDrawDeviceCompat)
            }

            running = true
            choreographer?.postFrameCallback(frameCallback)
        }
    }

    /** 异步打开游戏。进度通过 [onStartupStateChanged] 回调。 */
    fun openGame(gameRootPath: String, startupScript: String? = null) {
        post {
            lastFrameNanos = 0L
            tickFailures = 0L
            gameTerminated = false
            // 换了游戏（或改了档位）：让叠加层下次采样重新从引擎取解析结果
            compatProfileCache = ""
            // 兼容档必须在开游戏**之前**下发：krkrgles 是在 StartApplication
            // （插件 post-regist）里读 ogldrawdevice_compat 的，而 auto 判档要
            // 游戏根目录才能按血脉标记决定。
            applyOption("game_compat_profile", gameCompatProfile)
            applyOption("game_compat_game_root", gameRootPath)
            val rc = NativeEngine.engineOpenGameAsync(handle, gameRootPath, startupScript)
            if (rc != NativeEngine.RESULT_OK) {
                AppLog.e(TAG, "engineOpenGameAsync failed: rc=$rc err=${lastError()}")
                val msg = lastError()
                postToMain { onFatal("打开游戏失败：$msg") }
            } else {
                AppLog.i(TAG, "engineOpenGameAsync queued for $gameRootPath")
            }
        }
    }

    /**
     * Surface 就绪。**可在任意线程调用**——JNI 侧只保存 ANativeWindow，
     * 不触碰 EGL；引擎会在下一次 tick（渲染线程）自动 attach。
     *
     * @param surface 必须是 SurfaceView 系（`SurfaceHolder.getSurface()`）
     */
    fun attachSurface(surface: Surface, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        AppLog.i(TAG, "attachSurface ${width}x$height")
        surfaceAttached = true
        NativeEngine.nativeSetSurface(surface, width, height)
        post { NativeEngine.engineSetSurfaceSize(handle, width, height) }
    }

    /** Surface 销毁。可在任意线程调用。 */
    fun detachSurface() {
        AppLog.i(TAG, "detachSurface")
        surfaceAttached = false
        NativeEngine.nativeDetachSurface()
    }

    /** Surface 尺寸变化（旋转/分屏）。可在任意线程调用。 */
    fun resizeSurface(surface: Surface, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        AppLog.i(TAG, "resizeSurface ${width}x$height")
        NativeEngine.nativeSetSurface(surface, width, height)
        post { NativeEngine.engineSetSurfaceSize(handle, width, height) }
    }

    /** 退出到后台。 */
    fun pause() {
        paused = true
        post {
            val rc = NativeEngine.enginePause(handle)
            if (rc != NativeEngine.RESULT_OK) AppLog.w(TAG, "enginePause rc=$rc")
        }
    }

    /**
     * 用户在"游戏请求退出"确认框里选了**继续游戏**：撤销引擎的终止标志并恢复帧
     * 循环。**可在任意线程调用**（内部切到渲染线程，与 engineTick 同线程）。
     *
     * 幂等：不在终止挂起状态时调用无副作用。会话已关闭时直接忽略（那时退出流程
     * 已经走完了）。
     */
    fun cancelGameTermination(onRefused: () -> Unit = {}) {
        post {
            if (handle == 0L) {
                postToMain { onRefused() }
                return@post
            }
            val rc = NativeEngine.engineCancelTermination(handle)
            if (rc != NativeEngine.RESULT_OK) {
                // 引擎拒绝撤销（真机情形：**游戏已经关掉了自己的窗口**，撤销只会让
                // 它在空场景上继续跑）。绝不能把用户留在一个已经死掉的画面上 ——
                // 交给宿主直接退出。见 MainActivity.keepPlaying。
                AppLog.w(TAG, "engineCancelTermination 被拒绝 rc=$rc err=${lastError()}：交给宿主退出")
                postToMain { onRefused() }
                return@post
            }
            gameTerminated = false
            lastFrameNanos = 0L
            // 帧循环在检测到终止时就停了（running=false）。这里把它接回去；
            // 若此刻正好在后台（paused），resume() 里会再挂一次。
            if (!running && !destroyed) {
                running = true
                if (!paused) choreographer?.postFrameCallback(frameCallback)
            }
            AppLog.i(TAG, "宿主确认继续游戏：已撤销终止标志，恢复帧循环")
        }
    }

    /**
     * 答复"游戏请求关窗"（[onWindowCloseRequested]）。**可在任意线程调用**（内部切到
     * 渲染线程，与 engineTick 同线程）。
     *
     * @param allowClose true=退出（下一帧 tick 返回 [onWindowClosed]）；false=继续游戏，
     *   游戏从原处接着跑。幂等；会话已关闭时空操作。
     * @param onRefused 引擎拒绝时回调（例如挂起已被兜底超时关掉）——宿主应直接退出到库
     *   界面，绝不能把用户留在死画面上。
     */
    fun resolveWindowClose(allowClose: Boolean, onRefused: () -> Unit = {}) {
        post {
            if (handle == 0L) {
                if (!allowClose) postToMain { onRefused() }
                return@post
            }
            val rc = NativeEngine.engineResolveWindowClose(handle, if (allowClose) 1 else 0)
            if (rc != NativeEngine.RESULT_OK) {
                AppLog.w(TAG, "engineResolveWindowClose(allow=$allowClose) rc=$rc err=${lastError()}")
                if (!allowClose) {
                    postToMain { onRefused() }
                }
                return@post
            }
            windowCloseRequested = false
            if (allowClose) {
                // 引擎已执行关窗终止：下一帧 tick 返回 RESULT_WINDOW_CLOSED，由那里的
                // 分支走 exitToLauncher()。这里只记一笔。
                AppLog.i(TAG, "宿主确认退出：已请求引擎执行关窗终止")
            } else {
                lastFrameNanos = 0L
                AppLog.i(TAG, "宿主选择继续游戏：已取消关窗请求，游戏继续运行")
            }
        }
    }

    /** 回到前台。 */
    fun resume() {
        post {
            lastFrameNanos = 0L
            val rc = NativeEngine.engineResume(handle)
            if (rc != NativeEngine.RESULT_OK) AppLog.w(TAG, "engineResume rc=$rc")
            paused = false
            // 切后台时若正好发生"游戏请求退出→用户选继续"，帧循环会停在
            // running=true 但没挂回调的状态，这里补挂一次。
            if (running && !destroyed) choreographer?.postFrameCallback(frameCallback)
        }
    }

    /** 销毁引擎并结束渲染线程。调用后本对象不可再用。 */
    fun shutdown() {
        if (destroyed) return
        destroyed = true
        running = false

        val ht = thread
        val h = handler
        if (ht == null || h == null) return

        h.post {
            choreographer?.removeFrameCallback(frameCallback)
            if (handle != 0L) {
                NativeEngine.engineDestroy(handle)
                handle = 0L
                AppLog.i(TAG, "engineDestroy done")
            }
            ht.quitSafely()
        }
        thread = null
        handler = null
    }

    // ── 输入 ──────────────────────────────────────────────────────────────

    /**
     * 发送输入事件。**可在任意线程调用**，直接在调用线程调引擎入队。
     *
     * 引擎侧 `engine_send_input` 只把事件压进受互斥锁保护的队列（真正派发在
     * tick 线程做），所以允许任意线程投递。**不能**再切到渲染线程：模态对话框
     * （KAG 的 `Window.showModal()`）期间渲染线程被 core 的嵌套循环占住，
     * 若还靠 `post{}` 排队，点击会一直卡在队列里 —— 对话框永远等不到用户操作。
     * 见 `HostWindowLayer::ShowWindowAsModal` 与 `PumpModalInputOnTickThread`。
     *
     * @param keyCode **Windows VK 码**（见 [VkCodes]），不是 Android KEYCODE
     * @param x,y 视图坐标（物理像素）；不要乘 density
     */
    fun sendInput(
        type: Int,
        x: Double = 0.0,
        y: Double = 0.0,
        deltaX: Double = 0.0,
        deltaY: Double = 0.0,
        pointerId: Int = 0,
        button: Int = 0,
        keyCode: Int = 0,
        modifiers: Int = 0,
        unicodeCodepoint: Int = 0,
        timestampMicros: Long = System.nanoTime() / 1_000L,
    ) {
        val h = handle
        if (h == 0L || destroyed) return
        val rc = NativeEngine.engineSendInput(
            h, type, x, y, deltaX, deltaY, pointerId, button,
            keyCode, modifiers, unicodeCodepoint, timestampMicros,
        )
        if (rc != NativeEngine.RESULT_OK &&
            rc != NativeEngine.RESULT_STARTUP_PENDING
        ) {
            // 触摸是高频事件，输入若被持续拒绝会逐条刷屏——限频并带上次数。
            // 启动期（STARTUP_PENDING）被拒是正常的，不计数也不记日志。
            sendFailures++
            AppLog.wLimited(TAG, "sendInput", 5_000L) {
                "engineSendInput failed x$sendFailures (最近一次 type=$type rc=$rc err=${lastError()})"
            }
        }
    }

    /**
     * 合成一次"返回/Esc"序列。
     *
     * 引擎约定：BACK 事件被当作 Esc 按下处理，但**必须**配合显式的 keyDown/keyUp
     * 三个事件，否则部分游戏收不到普通键事件，或缺 keyUp 导致虚拟 Esc 被当作一直按住。
     * 见 `README.md`「硬约束」。
     */
    fun sendBack() {
        sendInput(InputEvent.KEY_DOWN, keyCode = VkCodes.ESCAPE)
        sendInput(InputEvent.BACK, keyCode = VkCodes.ESCAPE)
        sendInput(InputEvent.KEY_UP, keyCode = VkCodes.ESCAPE)
    }

    // ── 内部 ──────────────────────────────────────────────────────────────

    /** 每个 tick 调一次；窗口满了就结算一次速率。只在渲染线程调用。 */
    private fun trackFps(frameTimeNanos: Long) {
        if (fpsWindowStartNanos == 0L) {
            fpsWindowStartNanos = frameTimeNanos
            return
        }
        fpsWindowTicks++
        val elapsed = frameTimeNanos - fpsWindowStartNanos
        if (elapsed >= FPS_WINDOW_NANOS) {
            measuredFps = fpsWindowTicks * 1_000_000_000f / elapsed
            fpsWindowTicks = 0
            fpsWindowStartNanos = frameTimeNanos
        }
    }

    /**
     * 记录本帧帧时间，并在窗口满时结算分位数。只在渲染线程调用。
     *
     * 采样的是**原始 delta**（已 clamp 到 [1, MAX_DELTA_MS]）且不做平滑：分位数的
     * 意义就是让尖峰露出来——所以它与 [measuredFps] 的口径不同，后者是窗口平均。
     */
    private fun trackFrameTime(frameTimeNanos: Long, deltaMs: Long) {
        frameMs = deltaMs.toFloat()
        if (frameSampleCount < frameSamples.size) {
            frameSamples[frameSampleCount++] = deltaMs.toFloat()
        }
        if (perfWindowStartNanos == 0L) {
            perfWindowStartNanos = frameTimeNanos
            return
        }
        if (frameTimeNanos - perfWindowStartNanos < PERF_WINDOW_NANOS) return

        // 排序副本：原数组要留给下一个窗口继续写入
        val sorted = frameSamples.copyOf(frameSampleCount).apply { sort() }
        if (sorted.isNotEmpty()) {
            frameP50Ms = percentile(sorted, 0.50f)
            frameP95Ms = percentile(sorted, 0.95f)
            frameP99Ms = percentile(sorted, 0.99f)
            frameMaxMs = sorted[sorted.size - 1]
        }
        frameSampleCount = 0
        perfWindowStartNanos = frameTimeNanos
    }

    /** 与 AetherKiri 同一取法：samples[floor((count-1)*q)]。 */
    private fun percentile(sorted: FloatArray, q: Float): Float =
        sorted[((sorted.size - 1) * q).toInt().coerceIn(0, sorted.size - 1)]

    /**
     * 引擎内存/缓存统计快照。字段顺序与 `NativeEngine.engineGetMemoryStats` 写入的
     * 数组一一对应（见 `engine_api_android_jni.cpp` 的 `kFieldCount`）。
     *
     * 注意 KiriNext 的 `engine_memory_stats_t` **没有** AetherKiri 的进程物理内存字段
     * （`process_*_bytes`），所以叠加层拿不到"App 实际占用"，只能显示引擎自己的缓存
     * 账目与系统内存——移植时不能照抄那边的字段名。
     */
    data class MemoryStats(
        val selfUsedMb: Long,
        val systemFreeMb: Long,
        val systemTotalMb: Long,
        val graphicCacheBytes: Long,
        val graphicCacheLimitBytes: Long,
        val xp3SegmentCacheBytes: Long,
        val psbCacheBytes: Long,
        val psbCacheEntries: Long,
        val psbCacheEntryLimit: Long,
        val psbCacheHits: Long,
        val psbCacheMisses: Long,
        val archiveCacheEntries: Long,
        val archiveCacheLimit: Long,
        val autopathCacheEntries: Long,
        val autopathCacheLimit: Long,
        val autopathTableEntries: Long,
    ) {
        /** 叠加层 `Memory: ... Cache` 那一项：三块缓存之和。 */
        val cacheBytes: Long
            get() = graphicCacheBytes + xp3SegmentCacheBytes + psbCacheBytes
    }

    /**
     * 当前生效的兼容档（`<profile> <mode>`，例如 `krkrz-kag kag`）。
     *
     * 档位解析在**引擎侧**完成（`auto` 要看游戏目录里的血脉标记，见
     * `engine_options.h`），所以壳只做缓存：`openGame()` 时失效、叠加层按 4Hz
     * 采样时再向引擎取一次。任意线程可调。
     */
    @Volatile
    private var compatProfileCache: String = ""
    private val compatProfileBuffer = ByteArray(NativeEngine.COMPAT_PROFILE_BUFFER_SIZE)

    fun compatProfile(): String {
        val cached = compatProfileCache
        if (cached.isNotEmpty()) return cached
        val n = NativeEngine.engineGetCompatProfile(compatProfileBuffer)
        if (n <= 0) return ""
        val text = String(compatProfileBuffer, 0,
                          minOf(n, compatProfileBuffer.size), Charsets.UTF_8).trim()
        compatProfileCache = text
        return text
    }

    /**
     * 内存统计快照。**只读渲染线程采样好的缓存**：直接调引擎 API 会拿引擎帧锁，
     * 模态对话框/CG 视频这类长帧期间主线程会阻塞到 ANR（见 [cachedMemoryStats]）。
     * 句柄未起来或还没采到返回 null。可从任意线程调用。
     */
    fun memoryStats(): MemoryStats? {
        if (handle == 0L) return null
        return cachedMemoryStats
    }

    /** 渲染线程：按固定间隔采样引擎统计，填 [cachedMemoryStats] / [rendererInfoCache]。 */
    private fun sampleEngineInfoIfDue(frameTimeNanos: Long) {
        if (lastEngineInfoSampleNanos != 0L &&
            frameTimeNanos - lastEngineInfoSampleNanos < ENGINE_INFO_SAMPLE_NANOS
        ) {
            return
        }
        lastEngineInfoSampleNanos = frameTimeNanos

        val h = handle
        if (h == 0L) return

        if (NativeEngine.engineGetMemoryStats(h, memoryStatsScratch) >=
            memoryStatsScratch.size
        ) {
            cachedMemoryStats = MemoryStats(
                selfUsedMb = memoryStatsScratch[0],
                systemFreeMb = memoryStatsScratch[1],
                systemTotalMb = memoryStatsScratch[2],
                graphicCacheBytes = memoryStatsScratch[3],
                graphicCacheLimitBytes = memoryStatsScratch[4],
                xp3SegmentCacheBytes = memoryStatsScratch[5],
                psbCacheBytes = memoryStatsScratch[6],
                psbCacheEntries = memoryStatsScratch[7],
                psbCacheEntryLimit = memoryStatsScratch[8],
                psbCacheHits = memoryStatsScratch[9],
                psbCacheMisses = memoryStatsScratch[10],
                archiveCacheEntries = memoryStatsScratch[11],
                archiveCacheLimit = memoryStatsScratch[12],
                autopathCacheEntries = memoryStatsScratch[13],
                autopathCacheLimit = memoryStatsScratch[14],
                autopathTableEntries = memoryStatsScratch[15],
            )
        }

        val written = NativeEngine.engineGetRendererInfo(h, rendererInfoBuffer)
        if (written > 0) {
            rendererInfoCache = String(rendererInfoBuffer, 0, written, Charsets.UTF_8)
        }
    }

    /**
     * 渲染器信息（key=value 串，含 backend / fallback 等）。**只读渲染线程采样好的
     * 缓存**（见 [sampleEngineInfoIfDue]）：从 UI 线程直接调引擎 API 会在模态对话框/
     * 长帧期间被引擎帧锁挡住 —— 那就是叠加层开着时的 ANR 来源。可从任意线程调用。
     */
    fun rendererInfo(): String = rendererInfoCache

    private fun post(block: () -> Unit) {
        val h = handler
        if (h == null) {
            AppLog.w(TAG, "post ignored: session not started")
            return
        }
        h.post(block)
    }

    private fun applyOption(key: String, value: String) {
        val rc = NativeEngine.engineSetOption(handle, key, value)
        if (rc != NativeEngine.RESULT_OK) {
            AppLog.w(TAG, "engineSetOption($key=$value) rc=$rc err=${lastError()}")
        }
    }

    private fun lastError(): String =
        if (handle == 0L) "<no handle>" else NativeEngine.engineGetLastError(handle)

    /**
     * 排空引擎启动日志并转发给 [onLog]，同时上报启动状态。
     *
     * 日志每轮都要排空（那是引擎启动日志的唯一出口），但**状态只在变化时上报**：
     * 这个方法是按帧轮询的，无条件上报会让宿主每 100ms 收到一次相同状态——真机日志
     * 里因此出现过连续几十行一模一样的 `startup state -> 2`。高频日志的要求就是
     * 去重/仅边沿。
     */
    private fun pollStartupState() {
        val written = NativeEngine.engineDrainStartupLogs(handle, logBuffer)
        if (written > 0) {
            onLog(String(logBuffer, 0, written, Charsets.UTF_8))
        }
        val state = NativeEngine.engineGetStartupState(handle)
        if (state >= 0 && state != lastReportedState) {
            lastReportedState = state
            val msg = if (state == NativeEngine.STARTUP_FAILED) lastError() else null
            postToMain {
                onStartupStateChanged(state)
                if (msg != null) onFatal("游戏启动失败：$msg")
            }
        }
    }
}

/** `engine_input_event_t.type` 取值 */
object InputEvent {
    const val POINTER_DOWN = 1
    const val POINTER_MOVE = 2
    const val POINTER_UP = 3
    const val POINTER_SCROLL = 4
    const val KEY_DOWN = 5
    const val KEY_UP = 6
    const val TEXT_INPUT = 7
    const val BACK = 8
}
