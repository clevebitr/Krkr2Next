package dev.kirinext.core

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
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
 * 脱离 UI 线程获得 vsync 节拍。引擎侧 `fps_limit` 设 0，由 Choreographer 统一限速。
 */
class EngineSession(
    private val writablePath: String,
    private val cachePath: String,
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
) {
    companion object {
        private const val TAG = "KiriNext/Engine"
        private const val THREAD_NAME = "krkr-render"
        private const val LOG_BUFFER_SIZE = 8 * 1024

        /** 启动状态轮询间隔（毫秒），与 tick 同频即可 */
        private const val STARTUP_POLL_FRAMES = 6

        /** 单帧最大 tick 步长，避免切回前台时 delta 过大导致脚本时间跳变 */
        private const val MAX_DELTA_MS = 100L
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

    @Volatile private var paused = false
    @Volatile private var running = false
    @Volatile private var destroyed = false

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
                val rc = NativeEngine.engineTick(handle, deltaMs.toInt())
                if (rc != NativeEngine.RESULT_OK) {
                    Log.w(TAG, "engineTick failed: rc=$rc err=${lastError()}")
                }
                if (++frameCounter % STARTUP_POLL_FRAMES == 0L) {
                    pollStartupState()
                }
            }

            choreographer?.postFrameCallback(this)
        }
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
                Log.e(TAG, "engineCreate failed (writable=$writablePath cache=$cachePath)")
                postToMain { onFatal("引擎初始化失败") }
                return@post
            }
            Log.i(TAG, "engineCreate ok, apiVersion=0x${NativeEngine.engineGetRuntimeApiVersion().toString(16)}")

            // fps_limit=0：由 Choreographer 提供节拍，引擎不再自行限速
            applyOption("fps_limit", "0")

            running = true
            choreographer?.postFrameCallback(frameCallback)
        }
    }

    /** 异步打开游戏。进度通过 [onStartupStateChanged] 回调。 */
    fun openGame(gameRootPath: String, startupScript: String? = null) {
        post {
            lastFrameNanos = 0L
            val rc = NativeEngine.engineOpenGameAsync(handle, gameRootPath, startupScript)
            if (rc != NativeEngine.RESULT_OK) {
                Log.e(TAG, "engineOpenGameAsync failed: rc=$rc err=${lastError()}")
                val msg = lastError()
                postToMain { onFatal("打开游戏失败：$msg") }
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
        NativeEngine.nativeSetSurface(surface, width, height)
        post { NativeEngine.engineSetSurfaceSize(handle, width, height) }
    }

    /** Surface 销毁。可在任意线程调用。 */
    fun detachSurface() {
        NativeEngine.nativeDetachSurface()
    }

    /** Surface 尺寸变化（旋转/分屏）。可在任意线程调用。 */
    fun resizeSurface(surface: Surface, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        NativeEngine.nativeSetSurface(surface, width, height)
        post { NativeEngine.engineSetSurfaceSize(handle, width, height) }
    }

    /** 退出到后台。 */
    fun pause() {
        paused = true
        post {
            val rc = NativeEngine.enginePause(handle)
            if (rc != NativeEngine.RESULT_OK) Log.w(TAG, "enginePause rc=$rc")
        }
    }

    /** 回到前台。 */
    fun resume() {
        post {
            lastFrameNanos = 0L
            val rc = NativeEngine.engineResume(handle)
            if (rc != NativeEngine.RESULT_OK) Log.w(TAG, "engineResume rc=$rc")
            paused = false
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
                Log.i(TAG, "engineDestroy done")
            }
            ht.quitSafely()
        }
        thread = null
        handler = null
    }

    // ── 输入 ──────────────────────────────────────────────────────────────

    /**
     * 发送输入事件。可在 UI 线程调用——事件在引擎内部排队，下一次 tick 才派发。
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
    ) {
        if (handle == 0L) return
        val rc = NativeEngine.engineSendInput(
            handle, type, x, y, deltaX, deltaY, pointerId, button,
            keyCode, modifiers, unicodeCodepoint,
            System.nanoTime() / 1_000L,
        )
        if (rc != NativeEngine.RESULT_OK) {
            Log.w(TAG, "engineSendInput(type=$type) rc=$rc err=${lastError()}")
        }
    }

    /**
     * 合成一次"返回/Esc"序列。
     *
     * 引擎约定：BACK 事件被当作 Esc 按下处理，但**必须**配合显式的 keyDown/keyUp
     * 三个事件，否则部分游戏收不到普通键事件，或缺 keyUp 导致虚拟 Esc 被当作一直按住。
     * 见 `docs/dev/input-contract.md`。
     */
    fun sendBack() {
        sendInput(InputEvent.KEY_DOWN, keyCode = VkCodes.ESCAPE)
        sendInput(InputEvent.BACK, keyCode = VkCodes.ESCAPE)
        sendInput(InputEvent.KEY_UP, keyCode = VkCodes.ESCAPE)
    }

    // ── 内部 ──────────────────────────────────────────────────────────────

    private fun post(block: () -> Unit) {
        val h = handler
        if (h == null) {
            Log.w(TAG, "post ignored: session not started")
            return
        }
        h.post(block)
    }

    private fun applyOption(key: String, value: String) {
        val rc = NativeEngine.engineSetOption(handle, key, value)
        if (rc != NativeEngine.RESULT_OK) {
            Log.w(TAG, "engineSetOption($key=$value) rc=$rc err=${lastError()}")
        }
    }

    private fun lastError(): String =
        if (handle == 0L) "<no handle>" else NativeEngine.engineGetLastError(handle)

    /** 排空引擎启动日志并转发给 [onLog]，同时上报启动状态。 */
    private fun pollStartupState() {
        val written = NativeEngine.engineDrainStartupLogs(handle, logBuffer)
        if (written > 0) {
            onLog(String(logBuffer, 0, written, Charsets.UTF_8))
        }
        val state = NativeEngine.engineGetStartupState(handle)
        if (state >= 0) {
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
