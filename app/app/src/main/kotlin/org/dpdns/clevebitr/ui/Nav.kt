package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.navArgument
import java.io.File
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.scrape.ScoredCandidate

/**
 * 路由表。
 *
 * 只有详情页 / 游戏设置页 / 刮削页带参数，都是 `gameId`（路径哈希，纯十六进制）
 * ——**不放真实路径**：路径里可能有空格、`#`、中文与斜杠，塞进路由字符串要额外转义，
 * 出问题时表现为"页面打不开"，而这类 bug 在真机上很难查。gameId 查一次库就能拿到路径。
 */
object Routes {
    const val LIBRARY = "library"
    const val PICKER = "picker"
    const val SETTINGS = "settings"
    const val ABOUT = "about"
    const val GAME_ID = "gameId"
    const val DETAIL = "detail/{$GAME_ID}"
    const val GAME_SETTINGS = "game-settings/{$GAME_ID}"
    const val SCRAPE = "scrape/{$GAME_ID}"

    fun detail(gameId: String) = "detail/$gameId"
    fun gameSettings(gameId: String) = "game-settings/$gameId"
    fun scrape(gameId: String) = "scrape/$gameId"
}

/**
 * 导航需要的数据与回调。
 *
 * 集中成一个类而不是给 `NavHost` 传二十个参数：界面状态都提在 `MainActivity`
 * （见那里的注释），这里只是一个"传话人"，把状态与回调按屏幕分发下去。
 */
class ShellNavParams(
    val games: List<LibraryGame>,
    val coversDir: File,
    val librarySort: String,
    val globalDefaults: GlobalDefaults,
    val onLibrarySortChange: (String) -> Unit,
    val onLaunchGame: (LibraryGame) -> Unit,
    val onLaunchPath: (File) -> Unit,
    /** 返回「是否真的新加入」（库会去重），批量扫描据此统计。 */
    val onAddToLibrary: (File) -> Boolean,
    val onScanFinished: (Int, Int) -> Unit,
    val onRemoveFromLibrary: (LibraryGame) -> Unit,
    val onSaveGame: (LibraryGame, GameConfig) -> Unit,
    /** 只改记录（收藏 / 分组）：不碰配置文件，省掉一次写盘。 */
    val onToggleFavorite: (LibraryGame) -> Unit,
    val onSetGroup: (LibraryGame, String) -> Unit,
    val onApplyScrape: (String, ScoredCandidate) -> Unit,
    /** 读某游戏的配置 + 配置文件是否落在游戏目录。读盘只有几 KB，按需读即可。 */
    val loadGameConfig: (LibraryGame) -> Pair<GameConfig, Boolean>,
)

/**
 * 启动器侧（库 / 添加游戏 / 详情 / 游戏设置 / 刮削 / 设置 / 关于）的导航图。
 *
 * **游戏画面不在图里**：引擎会话与 `SurfaceView` 由 `MainActivity` 以覆盖层的方式
 * 盖在导航图之上。放进图里的话，一旦用户从游戏内菜单开了设置页，导航会销毁
 * 游戏目的地的 `SurfaceView`，引擎的 surface 就得重新 attach——实测那会让
 * 引擎重启（`surfaceDestroyed` → `detachSurface`）。
 */
@Composable
fun ShellNavHost(
    navController: NavHostController,
    params: ShellNavParams,
    settingsContent: @Composable () -> Unit,
    modifier: Modifier = Modifier,
) {
    NavHost(
        navController = navController,
        startDestination = Routes.LIBRARY,
        modifier = modifier,
    ) {
        composable(Routes.LIBRARY) {
            // 分组编辑走与详情页同一个对话框（见 GroupEditorDialog 的注释）
            var groupTarget by remember { mutableStateOf<LibraryGame?>(null) }
            LibraryScreen(
                games = params.games,
                coversDir = params.coversDir,
                sort = params.librarySort,
                onSortChange = params.onLibrarySortChange,
                onLaunch = params.onLaunchGame,
                onOpenDetail = { navController.navigate(Routes.detail(it.id)) },
                onScrape = { navController.navigate(Routes.scrape(it.id)) },
                onRemove = params.onRemoveFromLibrary,
                onToggleFavorite = params.onToggleFavorite,
                onEditGroup = { groupTarget = it },
                onAddGame = { navController.navigate(Routes.PICKER) },
            )
            groupTarget?.let { target ->
                GroupEditorDialog(
                    game = target,
                    knownGroups = knownGroups(params),
                    onDismiss = { groupTarget = null },
                    onConfirm = { group ->
                        params.onSetGroup(target, group)
                        groupTarget = null
                    },
                )
            }
        }

        composable(Routes.PICKER) {
            PickerScreen(
                inLibrary = { dir -> params.games.any { it.path == dir.absolutePath } },
                onAddToLibrary = params.onAddToLibrary,
                onScanFinished = params.onScanFinished,
                onLaunchPath = params.onLaunchPath,
                onBack = { navController.popBackStack() },
            )
        }

        composable(Routes.SETTINGS) {
            settingsContent()
        }

        composable(Routes.ABOUT) {
            AboutScreen(onBack = { navController.popBackStack() })
        }

        composable(
            route = Routes.DETAIL,
            arguments = listOf(navArgument(Routes.GAME_ID) { type = NavType.StringType }),
        ) { entry ->
            val gameId = entry.arguments?.getString(Routes.GAME_ID).orEmpty()
            val game = params.games.firstOrNull { it.id == gameId }
            GameDestination(game = game, onBack = { navController.popBackStack() }) {
                val (config, inGameDir) = params.loadGameConfig(it)
                // 详情页与设置页共用一个"分组编辑器"：两页都能改分组，行为必须一致。
                var groupTarget by remember(it.id) { mutableStateOf(false) }
                GameDetailScreen(
                    game = it,
                    coversDir = params.coversDir,
                    config = config,
                    configInGameDir = inGameDir,
                    globalDefaults = params.globalDefaults,
                    onSave = params.onSaveGame,
                    onLaunch = { params.onLaunchGame(it) },
                    onOpenSettings = { navController.navigate(Routes.gameSettings(it.id)) },
                    onScrape = { navController.navigate(Routes.scrape(it.id)) },
                    onToggleFavorite = { params.onToggleFavorite(it) },
                    onEditGroup = { groupTarget = true },
                    onRemove = {
                        params.onRemoveFromLibrary(it)
                        navController.popBackStack()
                    },
                    onBack = { navController.popBackStack() },
                )
                if (groupTarget) {
                    GroupEditorDialog(
                        game = it,
                        knownGroups = knownGroups(params),
                        onDismiss = { groupTarget = false },
                        onConfirm = { group ->
                            params.onSetGroup(it, group)
                            groupTarget = false
                        },
                    )
                }
            }
        }

        composable(
            route = Routes.GAME_SETTINGS,
            arguments = listOf(navArgument(Routes.GAME_ID) { type = NavType.StringType }),
        ) { entry ->
            val gameId = entry.arguments?.getString(Routes.GAME_ID).orEmpty()
            val game = params.games.firstOrNull { it.id == gameId }
            GameDestination(game = game, onBack = { navController.popBackStack() }) {
                val (config, _) = params.loadGameConfig(it)
                GameSettingsScreen(
                    game = it,
                    config = config,
                    globalDefaults = params.globalDefaults,
                    onSave = { updatedConfig ->
                        params.onSaveGame(it, updatedConfig)
                        navController.popBackStack()
                    },
                    onBack = { navController.popBackStack() },
                )
            }
        }

        composable(
            route = Routes.SCRAPE,
            arguments = listOf(navArgument(Routes.GAME_ID) { type = NavType.StringType }),
        ) { entry ->
            val gameId = entry.arguments?.getString(Routes.GAME_ID).orEmpty()
            val game = params.games.firstOrNull { it.id == gameId }
            GameDestination(game = game, onBack = { navController.popBackStack() }) {
                ScrapeScreen(
                    game = it,
                    onApply = { candidate ->
                        params.onApplyScrape(it.id, candidate)
                        navController.popBackStack()
                    },
                    onBack = { navController.popBackStack() },
                )
            }
        }
    }
}

/** 库里出现过的分组名，供分组对话框给建议值（与库页筛选条同源）。 */
private fun knownGroups(params: ShellNavParams): List<String> =
    params.games.map { it.group }.filter { it.isNotBlank() }.distinct().sorted()

/**
 * 带 `gameId` 的目的地共用的外壳：记录还在就渲染 [content]，不在就给一句话 + 返回。
 *
 * 记录被删掉后仍可能通过返回栈回到这里（比如先删后按返回），所以每个按 id 查记录的
 * 目的地都得处理"查不到"，否则会白屏或崩在空指针上。
 */
@Composable
private fun GameDestination(
    game: LibraryGame?,
    onBack: () -> Unit,
    content: @Composable (LibraryGame) -> Unit,
) {
    if (game == null) MissingGame(onBack = onBack) else content(game)
}

/** 记录不在了：给一句话和一个返回按钮，而不是空白页。 */
@Composable
private fun MissingGame(onBack: () -> Unit, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "这条记录已经不在游戏库里了。",
            style = MaterialTheme.typography.bodyMedium,
        )
        TextButton(onClick = onBack) { Text("返回") }
    }
}

/**
 * 启动器外层的自适应导航条。
 *
 * 为什么需要：库 / 添加游戏 / 设置 / 关于 是**四个平级顶层页**，此前只能从库页里的按钮
 * 逐层进去，平板与横屏下更是浪费了整块侧边空间。这里按窗口宽度分两种形态：
 *   - 窄（手机竖屏）：底部 [NavigationBar]；
 *   - 宽（平板、横屏，>= [WIDE_LAYOUT_MIN_DP]）：左侧 [NavigationRail]。
 *
 * 导航语义：点顶层项 = 跳到该目的地并把返回栈收敛到起点，避免"库→设置→库→设置…"越堆越深
 * （用户连点几次后按返回要按很多下才退出应用）。
 */
private const val WIDE_LAYOUT_MIN_DP = 600

@Composable
fun ShellScaffold(
    navController: NavHostController,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route

    val items = listOf(
        Triple(Routes.LIBRARY, "游戏库", Icons.Filled.Home),
        Triple(Routes.PICKER, "添加游戏", Icons.Filled.Add),
        Triple(Routes.SETTINGS, "设置", Icons.Filled.Settings),
        Triple(Routes.ABOUT, "关于", Icons.Filled.Info),
    )

    fun go(route: String) {
        if (currentRoute == route) return
        navController.navigate(route) {
            // 顶层页之间横跳：回到栈底再进，栈里始终只有"起点 + 当前页"。
            popUpTo(Routes.LIBRARY) { saveState = true }
            launchSingleTop = true
            restoreState = true
        }
    }

    val wide = LocalConfiguration.current.screenWidthDp >= WIDE_LAYOUT_MIN_DP

    if (wide) {
        Row(modifier = modifier.fillMaxSize()) {
            NavigationRail {
                items.forEach { (route, label, icon) ->
                    NavigationRailItem(
                        selected = currentRoute == route,
                        onClick = { go(route) },
                        icon = { Icon(icon, contentDescription = label) },
                        label = { Text(label) },
                    )
                }
            }
            Box(modifier = Modifier.weight(1f)) { content() }
        }
    } else {
        Scaffold(
            modifier = modifier,
            bottomBar = {
                NavigationBar {
                    items.forEach { (route, label, icon) ->
                        NavigationBarItem(
                            selected = currentRoute == route,
                            onClick = { go(route) },
                            icon = { Icon(icon, contentDescription = label) },
                            label = { Text(label) },
                        )
                    }
                }
            },
        ) { padding ->
            Box(modifier = Modifier.fillMaxSize().padding(padding)) { content() }
        }
    }
}
