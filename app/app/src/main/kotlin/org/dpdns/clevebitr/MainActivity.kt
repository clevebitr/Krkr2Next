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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import androidx.navigation.compose.rememberNavController
import java.io.File
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.CrashTracker
import org.dpdns.clevebitr.core.EngineExitHost
import org.dpdns.clevebitr.core.EngineSession
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GameConfigStore
import org.dpdns.clevebitr.core.GameLibrary
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.InputEvent
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.LogFiles
import org.dpdns.clevebitr.core.MessageBoxHost
import org.dpdns.clevebitr.core.NativeEngine
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.VkCodes
import org.dpdns.clevebitr.core.scrape.CoverStore
import org.dpdns.clevebitr.core.scrape.ScoredCandidate
import org.dpdns.clevebitr.core.scrape.ScrapeService
import org.dpdns.clevebitr.core.resolve
import org.dpdns.clevebitr.ui.GameScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.MessageBoxDialog
import org.dpdns.clevebitr.ui.SettingsScreen
import org.dpdns.clevebitr.ui.ShellNavHost
import org.dpdns.clevebitr.ui.ShellNavParams
import org.dpdns.clevebitr.ui.resolveDarkTheme

/**
 * 单 Activity 壳。
 *
 * 三块职责，顺序就是它们的依赖关系：
 * 1. **引擎会话**（[EngineSession]）：生命周期 + 渲染线程 + 输入转发。
 * 2. **游戏库**（[GameLibrary]）：列表、每游戏配置、刮削结果的宿主。
 * 3. **导航**（[ShellNavHost]）：库 / 添加游戏 / 详情 / 刮削 / 设置。
 *
 * 界面状态一律提在 Activity 层（而不是 `setContent` 内部）：导航回调、引擎回调
 * （`onFatal`、`onStartupStateChanged`）都要写它们，而 Compose 的 `remember`
 * 在这些回调里够不着。
 *
 * **游戏画面不在导航图里**：它是一层覆盖在导航图之上的会话界面。放进去的话，
 * 游戏内打开设置页会销毁那个目的地的 `SurfaceView`，引擎 surface 被 detach →
 * 引擎重启。见 [ShellNavHost] 的注释。
 */
class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "KrKr2Next/Main"
        private const val ENGINE_LOG_TAG = "KrKr2Next/Engine"
        private const val DOUBLE_BACK_MS = 2_000L
    }

    private var session: EngineSession? = null
    private var lastBackAt = 0L

    // ── 引擎会话状态 ──
    private var gamePath by mutableStateOf<String?>(null)
    private var startupState by mutableStateOf(NativeEngine.STARTUP_IDLE)
    private var statusText by mutableStateOf("正在打开游戏…")

    /** 上次异常退出的提示文本；null 表示这次不需要提示。 */
    private var recoveryNotice by mutableStateOf<String?>(null)

    /**
     * 游戏请求退出时置起：弹出确认框。两种来路：
     *  - [ExitPromptKind.TERMINATED]：脚本 `System.exit()`（引擎已终止挂起，可撤销）；
     *  - [ExitPromptKind.WINDOW_CLOSE_REQUESTED]：KAG 退出菜单请求关窗（引擎把关闭挂起，
     *    什么都没拆，选"继续游戏"能真的接着玩）。
     * 选"退出游戏"→ exitToLauncher()；选"继续游戏"→ cancelGameTermination() /
     * resolveWindowClose(false)。
     */
    private var gameExitPrompt by mutableStateOf<ExitPromptKind?>(null)

    /** 确认框的两种来路，文案与"继续"的处理不同。 */
    private enum class ExitPromptKind { TERMINATED, WINDOW_CLOSE_REQUESTED }

    /** 游戏内悬浮菜单打开的设置页（覆盖在游戏画面之上）。 */
    private var inGameSettings by mutableStateOf(false)

    /**
     * 本次会话生效的叠加层配置：启动时把"全局默认 + 该游戏覆盖"合并好。
     * 全局设置在游戏内被改动时，只有**没有独立配置**的游戏才跟着变。
     */
    private var sessionOverlay by mutableStateOf(OverlayConfig.default())
    private var sessionOverlayIsPerGame = false

    // ── 游戏库与设置状态 ──
    private lateinit var library: GameLibrary
    private var games by mutableStateOf<List<LibraryGame>>(emptyList())
    private var librarySort by mutableStateOf("lastPlayed")
    private var overlayConfig by mutableStateOf(OverlayConfig.default())

    private var themeMode by mutableStateOf("system")
    private var fontFallbackMode by mutableStateOf("auto")
    private var oglDrawDeviceCompat by mutableStateOf("off")
    private var gameCompatProfile by mutableStateOf("auto")

    private val coversDir: File by lazy { CoverStore.dir(this) }
    private val logDirPath: String by lazy { LogFiles.logsDir(this).absolutePath }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        // 顺序不能反：inspectPrevious 读的正是上次留下的标记文件，beginSession 会覆盖它
        val previous = CrashTracker.inspectPrevious(this)
        CrashTracker.beginSession(this)
        // 接管引擎消息框：从这一刻起 System.inform / 致命错误框会走本 Activity 的
        // 对话框（引擎线程在弹出期间阻塞等回复）。onDestroy 里交还。
        MessageBoxHost.attachUi()
        // 接管"游戏内退出游戏"：引擎的 TVPExitApplication 会回调 KR2Activity.exit()，
        // 这里把它接到与返回键连按两次相同的退出流程上。没有这一步时游戏请求退出后
        // 引擎每帧返回 INVALID_STATE，表现为卡住 + 叠加层错误数暴涨。
        EngineExitHost.attachUi { exitToLauncher() }
        if (previous.kind != CrashTracker.ExitKind.CLEAN) {
            AppLog.w(TAG, "上次未正常退出：${previous.kind} / ${previous.detail}")
            if (previous.kind != CrashTracker.ExitKind.JAVA_CRASH) {
                recoveryNotice = previous.detail ?: "上次未正常退出"
            }
        }
        AppLog.i(TAG, "onCreate (recovery=$previous)")

        library = GameLibrary(this)
        // 先读一次全局设置，再读库：库排序要用到 librarySort
        librarySort = AppPrefs.librarySort(this)
        overlayConfig = AppPrefs.overlayConfig(this)
        themeMode = AppPrefs.themeMode(this)
        fontFallbackMode = AppPrefs.fontFallbackMode(this)
        oglDrawDeviceCompat = AppPrefs.oglDrawDeviceCompat(this)
        gameCompatProfile = AppPrefs.gameCompatProfile(this)
        // 重装后库是空的、重新扫描也只拿到目录名；这里从各游戏目录的
        // krkr2next.json 把刮削过的标题/厂商/封面等补回来，省掉重刮一遍。
        // 放在 refreshLibrary() 之前，列表首帧就是补好的结果。
        runCatching { library.restoreMetadataFromGameDirs() }
            .onFailure { AppLog.w(TAG, "从游戏目录恢复元数据失败：$it") }
        refreshLibrary()

        setContent {
            KrKr2NextTheme(darkTheme = resolveDarkTheme(themeMode)) {
                // 根 Surface 不能省：`themes.xml` 的 windowBackground 是黑的，而 Compose
                // 只画自己覆盖到的像素——没有 Surface 的区域会直接露出窗口底色。
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                    val activeSession = session
                    val path = gamePath
                    if (activeSession == null || path == null) {
                        val navController = rememberNavController()
                        ShellNavHost(
                            navController = navController,
                            params = navParams(
                                // 启动器里的设置页：返回就是弹栈
                                settingsContent = { SettingsContent(onBack = { navController.popBackStack() }) },
                            ),
                        )
                    } else {
                        Box(modifier = Modifier.fillMaxSize()) {
                            GameScreen(
                                session = activeSession,
                                startupState = startupState,
                                statusText = statusText,
                                overlayConfig = sessionOverlay,
                                onOpenSettings = { inGameSettings = true },
                                onExit = ::exitToLauncher,
                            )
                            if (inGameSettings) {
                                Surface(
                                    modifier = Modifier.fillMaxSize(),
                                    color = MaterialTheme.colorScheme.background,
                                ) {
                                    SettingsContent(onBack = { inGameSettings = false })
                                }
                            }
                        }
                    }

                    // 宿主机消息框：引擎线程正阻塞等待，必须经 MessageBoxHost.reply
                    // 回传下标（内部走 JNI 回调唤醒引擎）。用轮询而不是回调，是因为
                    // 引擎→壳没有反向通道，入队发生在引擎线程，这里只读队列。
                    var messageBox by remember { mutableStateOf<MessageBoxHost.Request?>(null) }
                    LaunchedEffect(Unit) {
                        while (true) {
                            val head = MessageBoxHost.peek()
                            if (head !== messageBox) messageBox = head
                            delay(200)
                        }
                    }
                    messageBox?.let { request ->
                        MessageBoxDialog(
                            request = request,
                            onReply = { index, text ->
                                MessageBoxHost.reply(request, index, text)
                                messageBox = null
                            },
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

                    // 游戏内"退出游戏"确认框：此刻引擎已把终止/关窗挂起（不再渲染，但
                    // 什么都没拆），这里问一句。选"继续游戏"会撤销挂起、游戏从当前进度
                    // 接着跑；点外部/返回键等同"继续游戏"，避免误触直接退出。
                    gameExitPrompt?.let { kind ->
                        AlertDialog(
                            onDismissRequest = { keepPlaying() },
                            title = { Text("游戏请求退出") },
                            text = {
                                Text(
                                    when (kind) {
                                        ExitPromptKind.TERMINATED ->
                                            "游戏内的退出操作请求结束游戏。\n" +
                                                "选择「继续游戏」会回到游戏当前进度。"
                                        ExitPromptKind.WINDOW_CLOSE_REQUESTED ->
                                            "游戏请求关闭窗口（游戏内的退出菜单）。\n" +
                                                "选择「继续游戏」会留在游戏里继续玩。"
                                    }
                                )
                            },
                            confirmButton = {
                                TextButton(onClick = {
                                    gameExitPrompt = null
                                    AppLog.i(TAG, "用户确认退出游戏（kind=$kind）")
                                    if (kind == ExitPromptKind.WINDOW_CLOSE_REQUESTED) {
                                        // 引擎执行真正的关窗；下一帧 tick 会返回
                                        // WINDOW_CLOSED，由 onWindowClosed 走 exitToLauncher()。
                                        session?.resolveWindowClose(allowClose = true)
                                    } else {
                                        exitToLauncher()
                                    }
                                }) { Text("退出游戏") }
                            },
                            dismissButton = {
                                TextButton(onClick = { keepPlaying() }) { Text("继续游戏") }
                            },
                        )
                    }
                }
            }
        }
    }

    /**
     * 用户在"游戏请求退出"确认框里选了继续游戏：按来路请引擎撤销挂起。
     * 幂等；会话已关闭时是空操作。
     */
    private fun keepPlaying() {
        val kind = gameExitPrompt
        gameExitPrompt = null
        AppLog.i(TAG, "用户选择继续游戏（kind=$kind）：已请求撤销退出流程")
        if (kind == ExitPromptKind.WINDOW_CLOSE_REQUESTED) {
            // KAG 退出菜单这条路：引擎把关窗挂起了，什么都没拆，撤销后游戏能真的
            // 接着玩。引擎若已兜底关窗（20s 无人确认）则拒绝 —— 那时只能退出。
            session?.resolveWindowClose(allowClose = false, onRefused = {
                AppLog.w(TAG, "撤销关窗请求被拒（引擎已关窗），直接退出游戏界面")
                Toast.makeText(this, "游戏已关闭窗口，无法继续", Toast.LENGTH_LONG).show()
                exitToLauncher()
            })
            return
        }
        session?.cancelGameTermination(onRefused = {
            // 引擎拒绝撤销（游戏已经关掉自己的窗口）时不能把用户留在死画面上：
            // 提示一句并直接退出到库界面。
            AppLog.w(TAG, "撤销被拒（游戏已关窗），直接退出游戏界面")
            Toast.makeText(this, "游戏已关闭窗口，无法继续", Toast.LENGTH_LONG).show()
            exitToLauncher()
        })
    }

    /** 导航图需要的状态与回调。每次重组都会新建，成本只是几个引用。 */
    private fun navParams(settingsContent: @Composable () -> Unit): ShellNavParams =
        ShellNavParams(
            games = games,
            coversDir = coversDir,
            librarySort = librarySort,
            globalDefaults = globalDefaults(),
            onLibrarySortChange = { sort ->
                librarySort = sort
                AppPrefs.setLibrarySort(this, sort)
                refreshLibrary()
            },
            onLaunchGame = ::launchGame,
            onLaunchPath = { dir -> launchPath(dir.absolutePath) },
            onAddToLibrary = ::addToLibrary,
            onScanFinished = { added, skipped ->
                val text = if (added == 0 && skipped == 0) {
                    "没有找到新的游戏入口"
                } else {
                    "新增 $added 个，已在库中 $skipped 个"
                }
                Toast.makeText(this, text, Toast.LENGTH_LONG).show()
            },
            onRemoveFromLibrary = ::removeFromLibrary,
            onSaveGame = ::saveGame,
            onApplyScrape = ::applyScrape,
            loadGameConfig = { game ->
                val config = GameConfigStore.load(this, game.dir)
                val inGameDir = GameConfigStore.hasGameDirFile(game.dir) ||
                    GameConfigStore.isWritable(game.dir)
                config to inGameDir
            },
            settingsContent = settingsContent,
        )

    /** 设置页内容。启动器与游戏内共用同一个 Composable，行为不会分叉。 */
    @Composable
    private fun SettingsContent(onBack: () -> Unit) {
        val activePath = gamePath
        SettingsScreen(
            logDirPath = logDirPath,
            onBack = onBack,
            onShareLogs = ::shareLogs,
            overlayConfig = overlayConfig,
            onOverlayConfigChanged = { updated ->
                overlayConfig = updated
                // 该游戏没有独立配置时跟随全局；有独立配置就不动它
                if (!sessionOverlayIsPerGame) sessionOverlay = updated
            },
            themeMode = themeMode,
            onThemeModeChanged = { themeMode = it },
            fontFallbackMode = fontFallbackMode,
            onFontFallbackModeChanged = { fontFallbackMode = it },
            oglDrawDeviceCompat = oglDrawDeviceCompat,
            onOglDrawDeviceCompatChanged = { oglDrawDeviceCompat = it },
            gameCompatProfile = gameCompatProfile,
            onGameCompatProfileChanged = { gameCompatProfile = it },
            // 「可靠 + 看得见」：档位改完不重启就不生效，所以这里直接给一键重启
            runningGame = activePath != null,
            onRestartGame = activePath?.let { path -> { restartGame(path) } },
        )
    }

    /**
     * 用**当前游戏目录**重开一局。
     *
     * 引擎在插件注册时读运行模式，所以换档必须重开会话；而让用户自己"退出→再点进来"
     * 既慢又容易点错游戏。这里复用同一条启动路径（[launchPath]），
     * 顺带把库里的 lastPlayed/playCount 也正常记一次。
     */
    private fun restartGame(path: String) {
        AppLog.i(TAG, "restartGame path=$path（运行模式改动需要重开会话）")
        launchPath(path)
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

    // ── 游戏库 ──────────────────────────────────────────────────────────────

    private fun refreshLibrary() {
        val all = library.games()
        games = when (librarySort) {
            "title" -> all.sortedBy { it.title.lowercase() }
            "added" -> all.sortedByDescending { it.addedAt }
            else -> all.sortedByDescending { it.lastPlayedAt }
        }
    }

    /** @return 是否真的新增（调用方据此统计批量扫描的结果）。 */
    private fun addToLibrary(dir: File): Boolean {
        val result = library.add(dir)
        if (result.added) {
            AppLog.i(TAG, "加入游戏库：${dir.absolutePath}")
            refreshLibrary()
        }
        return result.added
    }

    private fun removeFromLibrary(game: LibraryGame) {
        if (library.remove(game.id)) {
            AppLog.i(TAG, "移出游戏库：${game.path}")
            refreshLibrary()
        }
    }

    private fun saveGame(game: LibraryGame, config: GameConfig) {
        library.update(game.id) { game }
        // 配置写进游戏目录（不可写则回退应用私有），并把元数据/备注一起同步过去
        val target = ScrapeService.syncRecordToGameDir(this, game, config)
        AppLog.i(TAG, "保存游戏配置：${game.title} -> ${target?.target} ${target?.file?.absolutePath}")
        refreshLibrary()
        Toast.makeText(
            this,
            if (target?.target == GameConfigStore.Target.GAME_DIR) {
                "已保存到游戏目录 krkr2next.json"
            } else {
                "已保存（游戏目录不可写，存在应用私有目录）"
            },
            Toast.LENGTH_SHORT,
        ).show()
    }

    private fun applyScrape(gameId: String, candidate: ScoredCandidate) {
        lifecycleScope.launch {
            try {
                val updated = ScrapeService.apply(this@MainActivity, library, gameId, candidate)
                if (updated != null) {
                    refreshLibrary()
                    Toast.makeText(
                        this@MainActivity,
                        "已应用：${updated.title}",
                        Toast.LENGTH_SHORT,
                    ).show()
                }
            } catch (t: Throwable) {
                AppLog.e(TAG, "刮削落库失败", t)
                Toast.makeText(
                    this@MainActivity,
                    "刮削失败：${t.message ?: t.javaClass.simpleName}",
                    Toast.LENGTH_LONG,
                ).show()
            }
        }
    }

    /** 全局默认：每游戏配置里没写的项都用它。 */
    private fun globalDefaults(): GlobalDefaults = GlobalDefaults(
        compatProfile = gameCompatProfile,
        oglDrawDeviceCompat = oglDrawDeviceCompat,
        fpsLimit = AppPrefs.fpsLimit(this),
        fontFallbackMode = fontFallbackMode,
        overlay = overlayConfig,
    )

    // ── 引擎会话 ────────────────────────────────────────────────────────────

    private fun launchGame(game: LibraryGame) {
        library.touch(game.id)
        refreshLibrary()
        launchPath(game.path)
    }

    /** 选定目录后创建引擎会话。库记录可选：目录页的"直接启动"走的就是这条路。 */
    private fun launchPath(path: String) {
        closeSession()
        inGameSettings = false

        // 必须先落这个状态：GameScreen（内含 SurfaceView）只在 gamePath 非空时才被
        // 组合，而 SurfaceView 的 surfaceChanged 是引擎拿到渲染目标的**唯一**途径。
        gamePath = path

        // 每游戏配置：读游戏目录里的 krkr2next.json（不可写则读私有回退），
        // 没写的项继承全局默认。**合并结果在启动时算好**，游戏内改全局不影响本局。
        val dir = File(path)
        val config = GameConfigStore.load(this, dir)
        val resolved = config.resolve(globalDefaults())
        sessionOverlay = resolved.overlay
        sessionOverlayIsPerGame = config.overlay != null

        AppLog.i(
            TAG,
            "launchGame path=$path cache=${cacheDir.absolutePath} " +
                "compat=${resolved.compatProfile} ogl=${resolved.oglDrawDeviceCompat} " +
                "fps=${resolved.fpsLimit} overlay=${resolved.overlay.enabled}",
        )
        AppLog.clearRecent()

        val s = EngineSession(
            writablePath = path,
            cachePath = cacheDir.absolutePath,
            fpsLimit = resolved.fpsLimit,
            fontFallbackMode = resolved.fontFallbackMode,
            oglDrawDeviceCompat = resolved.oglDrawDeviceCompat,
            gameCompatProfile = resolved.compatProfile,
            onLog = { log ->
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
            // 游戏内"退出游戏"（TJS System.exit()）：先弹确认框问用户，而不是直接退出。
            // 引擎此刻只是"终止挂起"（不再渲染、什么都没拆），所以可以取消。
            onGameExitRequested = {
                AppLog.i(TAG, "game requested exit -> 弹确认框")
                gameExitPrompt = ExitPromptKind.TERMINATED
            },
            // 游戏**请求关窗**（KAG 退出菜单：kag.close() → Window.close()）：引擎把关闭
            // 挂起，窗口没拆、脚本状态完好，所以"继续游戏"能真的接着玩 —— 这正是
            // 用户要的"游戏请求退出时先问一句"。
            onWindowCloseRequested = {
                AppLog.i(TAG, "game requested window close -> 弹确认框")
                gameExitPrompt = ExitPromptKind.WINDOW_CLOSE_REQUESTED
            },
            // 游戏关掉了自己的窗口：窗口已经没了，给"继续游戏"也是骗人的（撤销后只是
            // 在空场景上继续跑）。直接收尾，不弹确认框。
            onWindowClosed = {
                AppLog.i(TAG, "game closed its window -> exitToLauncher")
                Toast.makeText(this, "游戏已退出", Toast.LENGTH_SHORT).show()
                exitToLauncher()
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
        // 会话没了，退出确认框不该再挂着（否则退出后还会再弹一次）。
        gameExitPrompt = null
    }

    private fun exitToLauncher() {
        closeSession()
        gamePath = null
        startupState = NativeEngine.STARTUP_IDLE
        inGameSettings = false
        AppLog.i(TAG, "exitToLauncher")
    }

    // ── 按键 ──────────────────────────────────────────────────────────────

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val s = session
        if (s == null) {
            // 没有引擎会话时返回键交还给系统（导航栈自己处理返回）
            return super.dispatchKeyEvent(event)
        }

        // 返回键：短按转发给游戏（游戏用它打开自己的菜单），2 秒内连按两次退出。
        // 直接吞掉返回键会让玩家无法开菜单；直接退出又会丢失游戏内菜单入口。
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_DOWN) {
                if (inGameSettings) {
                    // 设置页盖在游戏上时，返回键先关它——否则玩家一按就直接退出游戏
                    inGameSettings = false
                    return true
                }
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
        // 交还消息框接管权（仅真正退出时）：未回复的请求按取消回掉，不让引擎卡在
        // 等待里。配置变更重建不交还——新实例会重新接管，队列里待回复的请求不丢。
        if (finishing) MessageBoxHost.detachUi()
        // 同上：配置变更重建不解除，新实例会重新接管（避免退出请求落在窗口里丢掉）。
        if (finishing) EngineExitHost.detachUi()
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
