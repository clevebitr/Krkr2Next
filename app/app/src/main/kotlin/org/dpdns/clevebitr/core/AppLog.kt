package org.dpdns.clevebitr.core

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

/**
 * 壳侧日志门面：logcat 与文件**双写**。
 *
 * ## 为什么要有它
 *
 * 原来只有 `Log.*`，要排障必须连着 adb；而引擎的 spdlog 走的是 stdout（Android 上
 * 是 /dev/null），线下根本看不到。文件那份才是能带走的证据。
 *
 * ## 为什么不直接同步写
 *
 * 写文件是 IO，不该出现在 UI 线程与渲染线程的调用路径上。所有落盘都 post 到一条
 * 专用的 [HandlerThread] 上，串行执行——顺带也让限频计数不需要加锁。
 *
 * ## 限频
 *
 * 高频日志必须采样/限频/去重/仅边沿——见 `README.md`「硬约束」。触屏 move、每帧失败
 * 之类的调用点必须走 [wLimited]：限频判定在调用线程做（`ConcurrentHashMap` 的读改写
 * 是无锁 CAS），但**消息字符串只在真正要记时才构造**，被限流掉的那些连字符串都不产生。
 */
object AppLog {

    private const val TAG = "KrKr2Next"
    private const val THREAD_NAME = "applog"
    private const val APP_LOG_LIMIT_BYTES = 2L * 1024 * 1024
    private const val APP_LOG_BACKUPS = 3

    /** 每写这么多行检查一次轮转，避免每行都 stat 一次文件。 */
    private const val ROTATE_CHECK_EVERY = 32

    /**
     * 多线程调用（[buildLine] 可能来自任意线程），`SimpleDateFormat` 本身不是线程安全的，
     * 必须同步访问。
     */
    private val timeFormat = SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US)

    @Volatile
    private var logFile: File? = null

    @Volatile
    private var handler: Handler? = null

    @Volatile
    private var dir: File? = null

    /** 限频表：key → 上次真正记录的时间戳。任意线程可读改写，靠 CAS 保证安全。 */
    private val lastEmit = ConcurrentHashMap<String, Long>()

    /** 只在 `applog` 线程上自增（见 [appendRaw]），不需要同步。 */
    private var writesSinceRotateCheck = 0

    /**
     * 在 `Application.onCreate` 里调用一次。可重复调用（会忽略）。
     *
     * 不在崩溃处理器路径上——那边只用 [crashLine] 之外的普通 `Log` 与独立文件。
     */
    fun init(context: Context) {
        if (handler != null) return
        synchronized(this) {
            if (handler != null) return
            val app = context.applicationContext
            val logDir = LogFiles.logsDir(app)
            dir = logDir
            logFile = File(logDir, LogFiles.APP_LOG_NAME)

            val ht = HandlerThread(THREAD_NAME).also { it.start() }
            val h = Handler(ht.looper)
            handler = h

            h.post {
                // 崩溃报告目录先建好：崩得早时崩溃处理器只做 mkdirs 兜底，能省一步是一步
                File(logDir, LogFiles.CRASH_DIR_NAME).mkdirs()
                LogFiles.rotate(
                    File(logDir, LogFiles.APP_LOG_NAME),
                    APP_LOG_LIMIT_BYTES,
                    APP_LOG_BACKUPS,
                )
                // 清理只在这条后台线程上做：绝不在启动路径上遍历目录
                LogFiles.pruneCrashReports(app)
                LogFiles.pruneTotal(app, PROTECTED_FILES)
                appendRaw(header(app))
            }
        }
    }

    fun d(tag: String, msg: String) = emit("D", tag, msg, null)

    fun i(tag: String, msg: String) = emit("I", tag, msg, null)

    fun w(tag: String, msg: String, t: Throwable? = null) = emit("W", tag, msg, t)

    fun e(tag: String, msg: String, t: Throwable? = null) = emit("E", tag, msg, t)

    /**
     * 限频版本：同一个 [rateKey] 在 [minIntervalMs] 内最多记一条。
     *
     * [build] 只在真正要记录时才求值——被限流掉的调用连字符串都不会构造，这一点对
     * 每帧都会触发的调用点很关键。
     */
    fun wLimited(tag: String, rateKey: String, minIntervalMs: Long, build: () -> String) {
        val now = System.currentTimeMillis()
        val prev = lastEmit[rateKey]
        if (prev != null && now - prev < minIntervalMs) return
        lastEmit[rateKey] = now
        emit("W", tag, build(), null)
    }

    /** 进程退出前尽量把队列排空。成功与否都不影响调用方。 */
    fun flush() {
        val h = handler ?: return
        val done = java.util.concurrent.CountDownLatch(1)
        if (!h.post { done.countDown() }) return
        try {
            done.await(500, java.util.concurrent.TimeUnit.MILLISECONDS)
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    private fun emit(level: String, tag: String, msg: String, t: Throwable?) {
        // logcat 始终写：连着 adb 时它比文件方便，且失败也没成本
        when (level) {
            "D" -> Log.d(tag, msg)
            "I" -> Log.i(tag, msg)
            "W" -> Log.w(tag, msg, t)
            else -> Log.e(tag, msg, t)
        }
        val line = buildLine(level, tag, msg, t)
        val h = handler
        if (h == null) return // 未初始化（例如崩溃后重启的 :crash 进程）就只留 logcat
        h.post { appendRaw(line) }
    }

    private fun buildLine(level: String, tag: String, msg: String, t: Throwable?): String {
        val sb = StringBuilder(160)
        synchronized(timeFormat) { sb.append(timeFormat.format(Date())) }
        sb.append(' ').append(level).append('/')
        sb.append(Thread.currentThread().name).append(' ')
        sb.append(tag).append(": ").append(msg)
        if (t != null) {
            sb.append('\n').append(Log.getStackTraceString(t))
        }
        sb.append('\n')
        return sb.toString()
    }

    /**
     * 只从 `applog` 这条线程调用（[init] 的 post 与 [emit] 的 post 都在它上面），
     * 所以 [_rotateIfNeeded] 的计数器不需要同步。
     */
    private fun appendRaw(text: String) {
        val file = logFile ?: return
        try {
            if (++writesSinceRotateCheck >= ROTATE_CHECK_EVERY) {
                writesSinceRotateCheck = 0
                LogFiles.rotate(file, APP_LOG_LIMIT_BYTES, APP_LOG_BACKUPS)
            }
            file.appendText(text)
        } catch (t: Throwable) {
            // 写不进去也不能把调用方拖垮（磁盘满、权限被撤等）
            Log.w(TAG, "append to ${file.name} failed", t)
        }
    }

    private fun header(context: Context): String {
        val sb = StringBuilder()
        sb.append("\n===== session start ")
        sb.append(LogFiles.timestamp()).append(" =====\n")
        sb.append("app=").append(BuildInfo.appVersion(context))
        sb.append(" android=").append(android.os.Build.VERSION.RELEASE)
        sb.append("(").append(android.os.Build.VERSION.SDK_INT).append(")")
        sb.append(" abi=").append(android.os.Build.SUPPORTED_ABIS.firstOrNull() ?: "?")
        sb.append('\n')
        sb.append("device=").append(android.os.Build.MANUFACTURER)
            .append(' ').append(android.os.Build.MODEL).append('\n')
        sb.append("logs=").append(dir?.absolutePath ?: "?").append('\n')
        return sb.toString()
    }

    private val PROTECTED_FILES = listOf(
        LogFiles.APP_LOG_NAME,
        LogFiles.ENGINE_LOG_NAME,
        LogFiles.LOGCAT_LOG_NAME,
    )
}

/** 版本号从 PackageManager 取，避免依赖 BuildConfig（多进程下也一致）。 */
internal object BuildInfo {
    @Suppress("DEPRECATION")
    fun appVersion(context: Context): String = try {
        val p = context.packageManager.getPackageInfo(context.packageName, 0)
        // 用已废弃的 versionCode 而不是 longVersionCode：后者是 API 28+ 的字段，
        // 在 24-27 上访问会直接 NoSuchFieldError
        "${p.versionName}(${p.versionCode})"
    } catch (e: Exception) {
        "?"
    }
}
