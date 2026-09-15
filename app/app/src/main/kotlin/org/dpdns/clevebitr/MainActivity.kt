package org.dpdns.clevebitr

import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.CrashTracker
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.InputEvent
import org.dpdns.clevebitr.core.LogFiles
import org.dpdns.clevebitr.core.NativeEngine
import org.dpdns.clevebitr.core.VkCodes
import org.dpdns.clevebitr.ui.GameScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.LauncherScreen

/**
 * 单 Activity 壳。
 *
 * 持有 [EngineSession]（引擎生命周期 + 渲染线程），并把按键、前台/后台事件转发给它。
 * Compose 只负责界面；引擎相关调用都不在 Compose 重组路径上。
 *
 * 界面状态提在 Activity 层（而不是 `setContent` 内部），因为 `launchGame` /
 * `onFatal` 等回调需要写它们。
 */
class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "KrKr2Next/Main"
        private const val ENGINE_LOG_TAG = "KrKr2Next/Engine"
        private const val DOUBLE_BACK_MS = 2_000L
    }

    private var session: EngineSession? = null
    private var lastBackAt = 0L

    // Compose 可观察的界面状态
    private var gamePath by mutableStateOf<String?>(null)
    private var startupState by mutableStateOf(NativeEngine.STARTUP_IDLE)
    private var statusText by mutableStateOf("正在打开游戏…")

    /** 上次异常退出的提示文本；null 表示这次不需要提示。 */
    private var recoveryNotice by mutableStateOf<String?>(null)

    private val logDirPath: String by lazy { LogFiles.logsDir(this).absolutePath }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        // 顺序不能反：inspectPrevious 读的正是上次留下的标记文件，beginSession 会覆盖它
        val previous = CrashTracker.inspectPrevious(this)
        CrashTracker.beginSession(this)
        if (previous.kind != CrashTracker.ExitKind.CLEAN) {
            AppLog.w(TAG, "上次未正常退出：${previous.kind} / ${previous.detail}")
            // Java 崩溃当时已经把崩溃界面给用户看过了，这里不再重复打扰；
            // 其余几种（原生崩溃 / ANR / 被系统结束）用户什么都没看到，需要补一次说明。
            if (previous.kind != CrashTracker.ExitKind.JAVA_CRASH) {
                recoveryNotice = previous.detail ?: "上次未正常退出"
            }
        }
        AppLog.i(TAG, "onCreate (recovery=$previous)")

        setContent {
            KrKr2NextTheme {
                val activeSession = session
                val path = gamePath
                if (activeSession == null || path == null) {
                    LauncherScreen(
                        onLaunchGame = ::launchGame,
                        onShareLogs = ::shareLogs,
                        logDirPath = logDirPath,
                    )
                } else {
                    GameScreen(
                        session = activeSession,
                        startupState = startupState,
                        statusText = statusText,
                    )
                }

                recoveryNotice?.let { notice ->
                    AlertDialog(
                        onDismissRequest = { recoveryNotice = null },
                        title = { Text("上次运行异常结束") },
                        text = {
                            Column {
                                Text(notice)
                                Text(
                                    text = "日志目录：$logDirPath",
                                    style = MaterialTheme.typography.bodySmall,
                                )
                            }
                        },
                        confirmButton = {
                            TextButton(onClick = {
                                shareLogs()
                                recoveryNotice = null
                            }) { Text("分享日志") }
                        },
                        dismissButton = {
                            TextButton(onClick = { recoveryNotice = null }) { Text("知道了") }
                        },
                    )
                }
            }
        }
    }

    private fun shareLogs() {
        val intent = LogFiles.buildShareIntent(this, LogFiles.collectForSharing(this))
        if (intent == null) {
            Toast.makeText(this, "没有可分享的日志", Toast.LENGTH_SHORT).show()
            return
        }
        try {
            startActivity(Intent.createChooser(intent, "分享日志"))
        } catch (t: Throwable) {
            AppLog.e(TAG, "share logs failed", t)
        }
    }

    /** 选定游戏目录后创建引擎会话。 */
    private fun launchGame(path: String) {
        closeSession()
        // 游戏目录是排障必需信息（哪个游戏、哪份存档），按已确认的边界记录它本身，
        // 不记录游戏内的任何文本
        AppLog.i(TAG, "launchGame path=$path cache=${cacheDir.absolutePath}")

        val s = EngineSession(
            // 引擎把存档写到 writablePath，缓存写到 cachePath
            writablePath = path,
            cachePath = cacheDir.absolutePath,
            onLog = { log ->
                // 引擎启动日志已经在 engine.log 里了，这里只做一次转发，
                // 顺带让连着 adb 的人也能看到
                log.lines().forEach { if (it.isNotBlank()) AppLog.i(ENGINE_LOG_TAG, it) }
            },
            onStartupStateChanged = { state ->
                startupState = state
                AppLog.i(TAG, "startup state -> $state")
            },
            onFatal = { msg ->
                statusText = msg
                startupState = NativeEngine.STARTUP_FAILED
                AppLog.e(TAG, "fatal: $msg")
            },
        )
        session = s
        startupState = NativeEngine.STARTUP_IDLE
        statusText = "正在打开游戏…"

        s.start()
        s.openGame(path)
    }

    private fun closeSession() {
        session?.let {
            AppLog.i(TAG, "closeSession")
            it.detachSurface()
            it.shutdown()
        }
        session = null
    }

    private fun exitToLauncher() {
        closeSession()
        gamePath = null
        startupState = NativeEngine.STARTUP_IDLE
        AppLog.i(TAG, "exitToLauncher")
    }

    // ── 按键 ──────────────────────────────────────────────────────────────

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val s = session ?: return super.dispatchKeyEvent(event)

        // 返回键：短按转发给游戏（游戏用它打开自己的菜单），2 秒内连按两次退出。
        // 直接吞掉返回键会让玩家无法开菜单；直接退出又会丢失游戏内菜单入口。
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_DOWN) {
                val now = System.currentTimeMillis()
                if (now - lastBackAt < DOUBLE_BACK_MS) {
                    lastBackAt = 0L
                    Toast.makeText(this, "已退出游戏", Toast.LENGTH_SHORT).show()
                    exitToLauncher()
                    return true
                }
                lastBackAt = now
                Toast.makeText(this, "再按一次返回退出游戏", Toast.LENGTH_SHORT).show()
                s.sendBack()
            }
            return true
        }

        // key_code 必须映射成 Windows VK 码，不能透传 Android KEYCODE_*
        val vk = VkCodes.fromAndroid(event.keyCode) ?: return super.dispatchKeyEvent(event)
        val type = when (event.action) {
            KeyEvent.ACTION_DOWN -> InputEvent.KEY_DOWN
            KeyEvent.ACTION_UP -> InputEvent.KEY_UP
            else -> return super.dispatchKeyEvent(event)
        }
        // 只记按下：抬起与按下成对出现，记两份没有额外信息，却会把日志量翻倍
        if (type == InputEvent.KEY_DOWN) {
            AppLog.i(TAG, "key down vk=0x${vk.toString(16)} modifiers=${event.metaState}")
        }
        s.sendInput(
            type,
            keyCode = vk,
            modifiers = VkCodes.modifiersFromAndroid(event.metaState),
        )
        return true
    }

    // ── 生命周期 ──────────────────────────────────────────────────────────

    override fun onResume() {
        super.onResume()
        applyImmersiveMode()
        session?.resume()
        AppLog.d(TAG, "onResume")
    }

    override fun onPause() {
        // 先暂停引擎再走默认流程：后台时不应继续烧 CPU/GPU
        session?.pause()
        super.onPause()
        AppLog.d(TAG, "onPause")
    }

    override fun onDestroy() {
        val finishing = isFinishing
        AppLog.i(TAG, "onDestroy finishing=$finishing")
        closeSession()
        if (finishing) {
            // 走到这里才算"正常退出"。没走到的话会话标记会停在 running，
            // 下次启动就会提示上次异常退出——这正是原生崩溃/被系统杀掉的判据。
            CrashTracker.endSession(this)
        }
        AppLog.flush()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyImmersiveMode()
    }

    /** 全屏沉浸：隐藏状态栏与导航栏，玩家手势可临时唤出。 */
    private fun applyImmersiveMode() {
        WindowInsetsControllerCompat(window, window.decorView).apply {
            systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
    }
}
