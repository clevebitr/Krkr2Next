package org.dpdns.clevebitr.core

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Process
import android.util.DisplayMetrics
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/**
 * 崩溃捕获。这里其实有**两条完全不同的通路**，混在一起想会得出错误结论：
 *
 * ## A. Java 未捕获异常 —— 能立刻给出界面
 *
 * [install] 装的处理器在任意线程上被调用。它只做五件事：把报告同步写进
 * `crash/crash-<时间>.txt`、同样内容打 logcat、写会话标记、拉起
 * [org.dpdns.clevebitr.CrashActivity]、然后自杀。
 *
 * ## B. 原生崩溃（引擎里的 SIGSEGV 等）—— 只能下次启动再报
 *
 * 引擎的 `CrashSignalHandler`（`bridge/engine_api/src/engine_api.cpp`）打印完
 * `FATAL SIGNAL n` 与原生栈后会 `signal(SIG_DFL); raise(sig)` 把进程交还给系统，
 * **Kotlin 拦不到**；`libc: Fatal signal` 那行还是 system uid 打的，应用侧 logcat
 * 也看不到。所以原生崩溃只能靠：引擎日志（已落在 `logs/engine.log`）+ 会话标记
 * 没被改成 `clean` + API 30+ 的 [ActivityManager.getHistoricalProcessExitInfos]
 * 在**下次启动**时回放。
 *
 * ## 处理器里绝对不能做的事
 *
 * 崩溃可能发生在任何线程、任何持锁状态下，多做一个动作就多一分二次崩溃或死锁的风险：
 *
 * - 不碰 `NativeEngine` / `EngineSession.shutdown()`：C++ 的 registry 锁可能正被崩溃
 *   线程持有，等它就是死锁；渲染线程也可能已经死了，`post{}` 会永远排不到。
 * - 不 `Handler.post` / `runOnUiThread`：主线程 Looper 可能已经死了。
 * - 不 `Toast`（没有 Looper 会直接抛）、不碰 Compose、不 `SharedPreferences.apply`。
 * - 不调 `getExternalFilesDir` / `PackageManager`（binder + IO）：日志目录用
 *   [LogFiles.logsDir] 已经解析好的**缓存**。
 * - 不做目录遍历、删除、压缩、分享。
 * - 不 `System.exit`（会跑 shutdown hook，可能挂住），用 `Process.killProcess`。
 * - **不 chain 旧的处理器**：chain 会拉起系统那个"应用已停止"对话框，与我们要显示的
 *   界面抢焦点。
 */
object CrashTracker {

    private const val TAG = "KrKr2Next/Crash"

    const val EXTRA_REPORT_PATH = "org.dpdns.clevebitr.extra.CRASH_REPORT"
    const val EXTRA_SUMMARY = "org.dpdns.clevebitr.extra.CRASH_SUMMARY"

    private const val STACK_LIMIT_CHARS = 64 * 1024

    /** 上次运行是怎么结束的。 */
    enum class ExitKind {
        /** 首次启动，或上次退出得干干净净。 */
        CLEAN,

        /** Java 未捕获异常（已经在崩溃界面里报过一次）。 */
        JAVA_CRASH,

        /** 原生崩溃（SIGSEGV 等）。日志在 engine.log 里。 */
        NATIVE_CRASH,

        /** ANR。 */
        ANR,

        /** 被系统回收或被用户强杀。 */
        KILLED,

        /** 没有标记文件可读，或标记文件不完整（进程被 SIGKILL 掉时会这样）。 */
        UNCLEAN,
    }

    data class PreviousExit(val kind: ExitKind, val detail: String?)

    @Volatile
    private var installed = false

    /** 在 `Application.onCreate` 里调用一次。 */
    fun install(context: Context) {
        if (installed) return
        synchronized(this) {
            if (installed) return
            installed = true
            val app = context.applicationContext
            Thread.setDefaultUncaughtExceptionHandler { thread, error ->
                handle(app, thread, error)
            }
        }
    }

    // ── 会话标记 ──────────────────────────────────────────────────────────

    /** 引擎/界面开始工作前写下"运行中"。正常退出必须调 [endSession]。 */
    fun beginSession(context: Context) {
        writeMarker(context, "running")
    }

    /** 正常退出。下次启动就不会报"上次异常退出"。 */
    fun endSession(context: Context) {
        writeMarker(context, "clean")
    }

    /**
     * 判断上次运行是怎么结束的。只在启动时调用一次。
     *
     * 标记文件是 `running` 说明上次进程**没能走到任何收尾逻辑**——原生崩溃、被
     * SIGKILL、被系统回收都属于这一类。具体是哪种由 API 30+ 的
     * [ActivityManager.getHistoricalProcessExitInfos] 区分；24-29 上只能给一句
     * 笼统的"上次未正常退出"。
     */
    fun inspectPrevious(context: Context): PreviousExit {
        val marker = LogFiles.sessionMarker(context)
        val state = try {
            if (marker.isFile) marker.readText().trim() else ""
        } catch (e: Exception) {
            ""
        }

        // 没有标记文件：要么是首次启动，要么上次连 beginSession 都没走到（例如
        // 在 Application.onCreate 里就崩了）。按"未正常退出"处理更保守。
        if (state.isEmpty()) {
            marker.delete()
            return PreviousExit(ExitKind.CLEAN, null)
        }
        if (state.startsWith("clean")) {
            marker.delete()
            return PreviousExit(ExitKind.CLEAN, null)
        }
        if (state.startsWith("crashed")) {
            marker.delete()
            // Java 崩溃当时已经给用户看过界面了，这里不再重复打扰
            return PreviousExit(ExitKind.JAVA_CRASH, "上次会话因 Java 异常终止")
        }

        marker.delete()
        return classifyUncleanExit(context)
    }

    private fun classifyUncleanExit(context: Context): PreviousExit {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            // API 24-29 没有 getHistoricalProcessExitInfos，只能给一句笼统的话
            return PreviousExit(ExitKind.UNCLEAN, "上次未正常退出（可能是引擎崩溃或被系统回收）")
        }
        return try {
            val am = context.getSystemService(ActivityManager::class.java)
                ?: return PreviousExit(ExitKind.UNCLEAN, "上次未正常退出")
            // 公开 API 是 getHistoricalProcessExitReasons（getHistoricalProcessExitInfos
            // 是 SystemApi，三方应用拿不到）。pid 传 0 = 不限进程，只按包名过滤。
            // 之后还要再筛一次 processName：:crash 进程也属于同一个包。
            val info = am.getHistoricalProcessExitReasons(context.packageName, 0, 8)
                ?.firstOrNull { it.processName == context.packageName }
                ?: return PreviousExit(ExitKind.UNCLEAN, "上次未正常退出")
            when (info.reason) {
                ApplicationExitInfo.REASON_CRASH_NATIVE ->
                    // status 就是信号号：SIGSEGV=11 / SIGABRT=6 / SIGBUS=7
                    PreviousExit(ExitKind.NATIVE_CRASH, "引擎原生崩溃（信号 ${info.status}）")

                ApplicationExitInfo.REASON_CRASH ->
                    PreviousExit(ExitKind.JAVA_CRASH, "上次会话崩溃：${info.description ?: "未知原因"}")

                ApplicationExitInfo.REASON_ANR ->
                    PreviousExit(ExitKind.ANR, "上次会话无响应（ANR）")

                ApplicationExitInfo.REASON_LOW_MEMORY, ApplicationExitInfo.REASON_OTHER,
                ApplicationExitInfo.REASON_USER_REQUESTED ->
                    PreviousExit(ExitKind.KILLED, "上次被系统结束（内存紧张或被手动清理）")

                else -> PreviousExit(ExitKind.UNCLEAN, "上次未正常退出（reason=${info.reason}）")
            }
        } catch (t: Throwable) {
            // 不同 ROM 对这块的支持程度不一，读不到就退回笼统提示
            Log.w(TAG, "getHistoricalProcessExitInfos failed", t)
            PreviousExit(ExitKind.UNCLEAN, "上次未正常退出")
        }
    }

    private fun writeMarker(context: Context, state: String) {
        try {
            val file = LogFiles.sessionMarker(context)
            val tmp = File(file.parentFile, "${file.name}.tmp")
            // 先写临时文件再改名：进程随时可能被杀，留下半截标记文件比没有更糟
            tmp.writeText("$state pid=${Process.myPid()} at=${LogFiles.timestamp()}\n")
            if (!tmp.renameTo(file)) {
                file.writeText(tmp.readText())
                tmp.delete()
            }
        } catch (t: Throwable) {
            Log.w(TAG, "write session marker failed", t)
        }
    }

    // ── Java 未捕获异常 ───────────────────────────────────────────────────

    private fun handle(context: Context, thread: Thread, error: Throwable) {
        var reportPath: String? = null
        try {
            // crash/ 子目录不保证已存在（正常情况下由 AppLog.init 的后台线程建，
            // 但崩得太早时它还没跑）。这里补一次 mkdirs，失败也只是报告写不进去。
            val dir = LogFiles.crashDir(context).apply { mkdirs() }
            val report = buildReport(context, thread, error)
            val file = File(dir, "crash-${LogFiles.timestamp()}.txt")
            file.writeText(report)
            reportPath = file.absolutePath
            Log.e(TAG, "uncaught exception on thread '${thread.name}'\n$report")
        } catch (t: Throwable) {
            // 处理器自己抛出去会变成真·静默崩溃，什么都不做也得吞掉
            Log.e(TAG, "failed to write crash report", t)
        }

        try {
            writeMarker(context, "crashed")
        } catch (t: Throwable) {
            // 忽略
        }

        try {
            val intent = Intent(context, org.dpdns.clevebitr.CrashActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                putExtra(EXTRA_REPORT_PATH, reportPath)
                putExtra(EXTRA_SUMMARY, "${error.javaClass.name}: ${error.message ?: ""}")
            }
            context.startActivity(intent)
        } catch (t: Throwable) {
            Log.e(TAG, "failed to start CrashActivity", t)
        }

        // 直接 SIGKILL 自己：不等 atexit / shutdown hook（引擎侧那些可能正处在
        // 崩溃线程持有的锁上）。startActivity 是同步 binder 调用，返回即 AMS 已受理，
        // 本进程随后消失不影响崩溃界面起来。
        Process.killProcess(Process.myPid())
    }

    private fun buildReport(context: Context, thread: Thread, error: Throwable): String {
        val sw = StringWriter()
        PrintWriter(sw).use { pw ->
            pw.println("===== KrKr2Next 崩溃报告 =====")
            pw.println("时间: ${LogFiles.timestamp()}")
            pw.println("线程: ${thread.name} (id=${thread.id})")
            pw.println("异常: ${error.javaClass.name}: ${error.message}")
            pw.println()
            pw.println("--- 环境（不含任何唯一标识符）---")
            pw.println("app: ${BuildInfo.appVersion(context)}")
            pw.println("device: ${Build.MANUFACTURER} ${Build.MODEL}")
            pw.println("android: ${Build.VERSION.RELEASE} (sdk ${Build.VERSION.SDK_INT})")
            pw.println("abi: ${Build.SUPPORTED_ABIS.joinToString()}")
            pw.println("屏幕: ${screenDescription(context)}")
            pw.println("日志目录: ${LogFiles.logsDir(context).absolutePath}")
            // 崩溃报告与引擎日志分居两处（引擎那份是按游戏切的），把这一局的落点也写上，
            // 否则拿到报告的人只有壳侧日志，得再去猜引擎日志在哪。
            LogFiles.lastGame(context)?.let { game ->
                pw.println("上局游戏: $game")
                pw.println("上局引擎日志: ${LogFiles.lastGameLogs(context).firstOrNull()?.absolutePath ?: "（无）"}")
            }
            pw.println("引擎库: ${org.dpdns.clevebitr.KrKr2NextApplication.engineLibraryLoaded}")
            pw.println()
            pw.println("--- 调用栈 ---")
            val trace = StringWriter()
            error.printStackTrace(PrintWriter(trace))
            val text = trace.toString()
            pw.println(
                if (text.length > STACK_LIMIT_CHARS) {
                    text.substring(0, STACK_LIMIT_CHARS) + "\n...(已截断)"
                } else {
                    text
                },
            )
        }
        return sw.toString()
    }

    private fun screenDescription(context: Context): String = try {
        val dm: DisplayMetrics = context.resources.displayMetrics
        "${dm.widthPixels}x${dm.heightPixels} @${dm.densityDpi}dpi"
    } catch (t: Throwable) {
        "?"
    }
}
