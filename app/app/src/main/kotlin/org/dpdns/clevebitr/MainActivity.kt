package org.dpdns.clevebitr

import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.InputEvent
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
        private const val DOUBLE_BACK_MS = 2_000L
    }

    private var session: EngineSession? = null
    private var lastBackAt = 0L

    // Compose 可观察的界面状态
    private var gamePath by mutableStateOf<String?>(null)
    private var startupState by mutableStateOf(NativeEngine.STARTUP_IDLE)
    private var statusText by mutableStateOf("正在打开游戏…")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        setContent {
            KrKr2NextTheme {
                val activeSession = session
                val path = gamePath
                if (activeSession == null || path == null) {
                    LauncherScreen(onLaunchGame = ::launchGame)
                } else {
                    GameScreen(
                        session = activeSession,
                        startupState = startupState,
                        statusText = statusText,
                    )
                }
            }
        }
    }

    /** 选定游戏目录后创建引擎会话。 */
    private fun launchGame(path: String) {
        closeSession()

        // 引擎日志落盘，便于离线排查
        NativeEngine.engineSetLogFilePath("${cacheDir.absolutePath}/krkr2_engine.log")

        val s = EngineSession(
            // 引擎把存档写到 writablePath，缓存写到 cachePath
            writablePath = path,
            cachePath = cacheDir.absolutePath,
            onLog = { log ->
                log.lines().forEach { if (it.isNotBlank()) Log.i("KrKr2Next/Engine", it) }
            },
            onStartupStateChanged = { state -> startupState = state },
            onFatal = { msg ->
                statusText = msg
                startupState = NativeEngine.STARTUP_FAILED
                Log.e(TAG, "fatal: $msg")
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
            it.detachSurface()
            it.shutdown()
        }
        session = null
    }

    private fun exitToLauncher() {
        closeSession()
        gamePath = null
        startupState = NativeEngine.STARTUP_IDLE
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
    }

    override fun onPause() {
        // 先暂停引擎再走默认流程：后台时不应继续烧 CPU/GPU
        session?.pause()
        super.onPause()
    }

    override fun onDestroy() {
        closeSession()
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
