package org.dpdns.clevebitr

import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.CrashTracker
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.InputEvent
import org.dpdns.clevebitr.core.LogFiles
import org.dpdns.clevebitr.core.NativeEngine
import org.dpdns.clevebitr.core.VkCodes
import org.dpdns.clevebitr.ui.GameScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.LauncherScreen
import org.dpdns.clevebitr.ui.SettingsScreen
import org.dpdns.clevebitr.ui.resolveDarkTheme

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

    /** 启动器或游戏内是否停在设置页。 */
    private var showSettings by mutableStateOf(false)

    /**
     * 性能叠加层档位。**放在 Activity 而不是 GameScreen 里**：设置页现在也能从游戏内
     * 悬浮菜单打开，改完必须立刻生效；`remember { AppPrefs... }` 只在首次组合时读一次，
     * 那样改了得退出重进才看得到。初值在 onCreate 里读（字段初始化时 Context 还没 attach）。
     */
    private var perfOverlayMode by mutableStateOf("")

    /**
     * 主题档位（`system` / `light` / `dark`）。与 [perfOverlayMode] 同理提到 Activity：
     * 设置页改完要**立刻**换肤，不能退出重进。初值在 onCreate 里读。
     */
    private var themeMode by mutableStateOf("system")

    /** 引擎字体回退策略（`auto` / `legacy` / `chain`）；改完下次开游戏生效。 */
    private var fontFallbackMode by mutableStateOf("auto")

    /** krkrz 的 OGLDrawDevice 兼容档位（`off` / `alias` / `kag`）；改完下次开游戏生效。 */
    private var oglDrawDeviceCompat by mutableStateOf("off")

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

        perfOverlayMode = AppPrefs.perfOverlayMode(this)
        themeMode = AppPrefs.themeMode(this)
        fontFallbackMode = AppPrefs.fontFallbackMode(this)
        oglDrawDeviceCompat = AppPrefs.oglDrawDeviceCompat(this)

        setContent {
            KrKr2NextTheme(darkTheme = resolveDarkTheme(themeMode)) {
                // 根 Surface 不能省。`themes.xml` 的 windowBackground 是黑的，而 Compose
                // 只画自己覆盖到的像素——**没有 Surface 的区域会直接露出窗口底色**。
                // 启动页/设置页此前正是这样：浅色方案下发黑字、窗口又是黑的，于是设置页
                // 黑字黑底看不见、启动页右上角齿轮（取 onSurfaceVariant）也被吞掉。
                // 这里铺一层 colorScheme.background，把所有分支都盖住。
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                val activeSession = session
                val path = gamePath
                if (activeSession == null || path == null) {
                    if (showSettings) {
                        SettingsScreen(
                            logDirPath = logDirPath,
                            onBack = { showSettings = false },
                            onShareLogs = ::shareLogs,
                            onPerfOverlayModeChanged = { perfOverlayMode = it },
                            themeMode = themeMode,
                            onThemeModeChanged = { themeMode = it },
                            fontFallbackMode = fontFallbackMode,
                            onFontFallbackModeChanged = { fontFallbackMode = it },
                            oglDrawDeviceCompat = oglDrawDeviceCompat,
                            onOglDrawDeviceCompatChanged = { oglDrawDeviceCompat = it },
                        )
                    } else {
                        LauncherScreen(
                            onLaunchGame = ::launchGame,
                            onOpenSettings = { showSettings = true },
                        )
                    }
                } else {
                    // 设置页盖在游戏**之上**而不是替换它：替换会让 SurfaceView 被销毁，
                    // 引擎的 surface 得重新 attach。浮层画在窗口里、SurfaceView 的 surface
                    // 在窗口之下，所以盖上去是安全的（游戏内悬浮菜单本来就是这个道理）。
                    Box(modifier = Modifier.fillMaxSize()) {
                        GameScreen(
                            session = activeSession,
                            startupState = startupState,
                            statusText = statusText,
                            perfMode = perfOverlayMode,
                            onOpenSettings = { showSettings = true },
                            onExit = ::exitToLauncher,
                        )
                        if (showSettings) {
                            Surface(
                                modifier = Modifier.fillMaxSize(),
                                color = MaterialTheme.colorScheme.background,
                            ) {
                                SettingsScreen(
                                    logDirPath = logDirPath,
                                    onBack = { showSettings = false },
                                    onShareLogs = ::shareLogs,
                                    onPerfOverlayModeChanged = { perfOverlayMode = it },
                                )
                            }
                        }
                    }
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
        showSettings = false

        // 必须先落这个状态：GameScreen（内含 SurfaceView）只在 gamePath 非空时才被
        // 组合，而 SurfaceView 的 surfaceChanged 是引擎拿到渲染目标的**唯一**途径。
        // 少了这一行，引擎照样能启动到 SUCCEEDED，但永远等不到 attachSurface——
        // 表现就是"日志说启动成功、屏幕却停在启动器上不动，也没有任何状态提示"。
        gamePath = path
        // 游戏目录是排障必需信息（哪个游戏、哪份存档），按已确认的边界记录它本身，
        // 不记录游戏内的任何文本
        AppLog.i(TAG, "launchGame path=$path cache=${cacheDir.absolutePath}")
        // 游戏内「运行时日志」浮层读的是内存环形缓冲；清一次，让它只显示本局的日志
        AppLog.clearRecent()

        val s = EngineSession(
            // 引擎把存档写到 writablePath，缓存写到 cachePath
            writablePath = path,
            cachePath = cacheDir.absolutePath,
            // 0 = 不限速，由 Choreographer 的 vsync 决定节拍（默认）
            fpsLimit = AppPrefs.fpsLimit(this),
            fontFallbackMode = AppPrefs.fontFallbackMode(this),
            oglDrawDeviceCompat = AppPrefs.oglDrawDeviceCompat(this),
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
        // 设置页现在也能从游戏内打开。退出游戏后必须复位，否则（比如从崩溃恢复路径回来）
        // 会落在设置页而不是启动器上。
        showSettings = false
        AppLog.i(TAG, "exitToLauncher")
    }

    // ── 按键 ──────────────────────────────────────────────────────────────

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val s = session
        if (s == null) {
            // 没有引擎会话时返回键交还给系统，除了"设置页 → 启动器"这一层自己处理
            if (event.keyCode == KeyEvent.KEYCODE_BACK &&
                event.action == KeyEvent.ACTION_DOWN && showSettings
            ) {
                showSettings = false
                return true
            }
            return super.dispatchKeyEvent(event)
        }

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
