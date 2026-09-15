package org.dpdns.clevebitr.core

import android.content.Context
import android.util.Log
import java.io.File

/**
 * 把本进程的 logcat 也落到 `logs/logcat.log`。
 *
 * ## 为什么需要它
 *
 * 光有 `app.log` 与 `engine.log` 还漏掉两类东西：
 *
 * 1. 引擎里那十几处 `AndroidInfoLog`（`__android_log_print`）——它们只进 logcat，
 *    不受 `engine_set_log_file_path` 管；
 * 2. **debuggerd 的墓碑**。原生崩溃时 `libc: Fatal signal 11` 与随后的 tombstone
 *    是系统打的，只有 logcat 里有。引擎自己的 `CrashSignalHandler` 虽然也打了栈，
 *    但系统那份带着全部线程的栈，是排查卡死/多线程问题的唯一材料。
 *
 * ## 能拿到什么
 *
 * Jelly Bean 起内核按 uid 过滤：没有 `READ_LOGS`（三方应用根本申请不到）时，
 * 应用只能看到自己 uid 的日志。对本项目这刚好够用，而且 crash_dump 是以**被崩溃
 * 应用的 uid** 运行的，所以 Fatal signal 那行也在可见范围内。
 *
 * 正因为按 uid 过滤，这里**不能**加 `--pid=<pid>`：那样会把系统替我们打的崩溃行
 * 一起筛掉，恰好丢掉最想要的部分。
 *
 * ## 失败要能退化
 *
 * 部分 ROM 会限制应用执行 `logcat`。取不到就记一条 warning 然后什么都不做——
 * 日志功能不能因为它把一个能跑的游戏拖垮。
 */
object LogcatCapture {

    private const val TAG = "KrKr2Next/Logcat"

    /** 与 [LogFiles.LOGCAT_LOG_NAME] 同源：清理时会跳过这个名字，写歪了会被误删。 */
    private const val FILE_NAME = LogFiles.LOGCAT_LOG_NAME
    private const val LIMIT_BYTES = 2L * 1024 * 1024
    private const val BACKUPS = 2
    private const val ROTATE_CHECK_EVERY = 64

    @Volatile
    private var started = false

    /**
     * 在 `Application.onCreate` 调用一次。
     *
     * 采集线程是 daemon：进程退出时它自然消失，`logcat` 子进程也会因为管道读端关闭
     * 而收到 SIGPIPE 结束，不需要额外的收尾逻辑。
     */
    fun start(context: Context) {
        if (started) return
        synchronized(this) {
            if (started) return
            started = true
            val app = context.applicationContext
            Thread({ pump(app) }, "logcat").apply { isDaemon = true }.start()
        }
    }

    private fun pump(context: Context) {
        val file = File(LogFiles.logsDir(context), FILE_NAME)
        try {
            LogFiles.rotate(file, LIMIT_BYTES, BACKUPS)
            // -v threadtime：带日期/时间/pid/tid/tag，否则时间轴对不上
            // -b main -b crash：主缓冲区 + 崩溃缓冲区（tombstone 落在后者）
            // -t 2000：先补最近 2000 行再持续跟随，避免把整个历史缓冲区拉下来
            val process = ProcessBuilder(
                "logcat", "-v", "threadtime", "-b", "main", "-b", "crash", "-t", "2000",
            ).redirectErrorStream(true).start()

            var sinceRotate = 0
            process.inputStream.bufferedReader().useLines { lines ->
                for (line in lines) {
                    if (++sinceRotate >= ROTATE_CHECK_EVERY) {
                        sinceRotate = 0
                        LogFiles.rotate(file, LIMIT_BYTES, BACKUPS)
                    }
                    // 逐行 append 而不是长期持有一个 writer：轮转要换文件，
                    // 持有旧 fd 会把之后的日志写进已经 unlink 的 inode（凭空消失）。
                    // 日志量本就不大，这点 open/close 开销可以接受。
                    file.appendText(line)
                    file.appendText("\n")
                }
            }
            Log.i(TAG, "logcat stream ended")
        } catch (t: Throwable) {
            // 不反手记进 AppLog：那条路径本身会写文件，可能正是在出问题的地方
            Log.w(TAG, "logcat capture unavailable, continuing without it", t)
        }
    }
}
