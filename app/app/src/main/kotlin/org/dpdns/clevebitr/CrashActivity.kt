package org.dpdns.clevebitr

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.core.view.WindowCompat
import org.dpdns.clevebitr.core.LogFiles
import org.dpdns.clevebitr.ui.CrashScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import java.io.File

/**
 * 崩溃界面，**跑在独立进程 `:crash` 里**。
 *
 * 为什么必须独立进程：它是在主进程因未捕获异常即将被 `killProcess` 掉的时刻被拉起来
 * 的。同进程的话，主进程一死界面就跟着没了。独立进程还能保证它不继承任何坏状态——
 * 代价是它拿不到内存里的东西，所以一切都从崩溃报告文件读。
 */
class CrashActivity : ComponentActivity() {

    companion object {
        private const val TAG = "KrKr2Next/Crash"
        private const val DETAIL_LIMIT_CHARS = 8 * 1024
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        val summary = intent.getStringExtra(org.dpdns.clevebitr.core.CrashTracker.EXTRA_SUMMARY)
        val detail = readReport()

        setContent {
            KrKr2NextTheme {
                CrashScreen(
                    title = "出错了",
                    summary = summary ?: "应用发生了未处理的异常。",
                    detail = detail,
                    logDirPath = LogFiles.logsDir(this).absolutePath,
                    onRestart = { restart() },
                    onShareLogs = { shareLogs() },
                    onExit = { finish() },
                )
            }
        }
    }

    /**
     * 报告路径由崩溃处理器通过 Intent 传进来；万一是从别处（例如"上次异常退出"的
     * 回放）起来的，就退回到"取目录里最新的一份"。
     */
    private fun readReport(): String? {
        val path = intent.getStringExtra(org.dpdns.clevebitr.core.CrashTracker.EXTRA_REPORT_PATH)
        val file = path?.let { File(it) }
            ?: LogFiles.crashDir(this)
                .listFiles { f -> f.isFile && f.name.startsWith("crash-") }
                ?.maxByOrNull { it.lastModified() }
            ?: return null

        return try {
            val text = file.readText()
            if (text.length > DETAIL_LIMIT_CHARS) {
                text.take(DETAIL_LIMIT_CHARS) + "\n…（完整内容见 ${file.absolutePath}）"
            } else {
                text
            }
        } catch (t: Throwable) {
            Log.w(TAG, "read crash report failed: ${file.absolutePath}", t)
            null
        }
    }

    private fun restart() {
        try {
            startActivity(
                Intent(this, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                },
            )
        } catch (t: Throwable) {
            Log.e(TAG, "restart failed", t)
        }
        finish()
    }

    private fun shareLogs() {
        val intent = LogFiles.buildShareIntent(this, LogFiles.collectForSharing(this))
        if (intent == null) {
            Log.w(TAG, "nothing to share")
            return
        }
        try {
            startActivity(Intent.createChooser(intent, "分享日志"))
        } catch (t: Throwable) {
            Log.e(TAG, "share failed", t)
        }
    }
}
