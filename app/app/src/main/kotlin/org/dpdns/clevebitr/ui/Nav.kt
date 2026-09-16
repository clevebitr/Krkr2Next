package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.navArgument
import java.io.File
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.scrape.ScoredCandidate

/**
 * 路由表。
 *
 * 只有详情页与刮削页带参数，都是 `gameId`（路径哈希，纯十六进制）——**不放真实路径**：
 * 路径里可能有空格、`#`、中文与斜杠，塞进路由字符串要额外转义，出问题时表现为
 * "页面打不开"，而这类 bug 在真机上很难查。gameId 查一次库就能拿到路径。
 */
object Routes {
    const val LIBRARY = "library"
    const val PICKER = "picker"
    const val SETTINGS = "settings"
    const val GAME_ID = "gameId"
    const val DETAIL = "detail/{$GAME_ID}"
    const val SCRAPE = "scrape/{$GAME_ID}"

    fun detail(gameId: String) = "detail/$gameId"
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
    val onApplyScrape: (String, ScoredCandidate) -> Unit,
    /** 读某游戏的配置 + 配置文件是否落在游戏目录。读盘只有几 KB，按需读即可。 */
    val loadGameConfig: (LibraryGame) -> Pair<GameConfig, Boolean>,
    /** 设置页整页内容：它要的参数太多，用 slot 传进来比逐个透传清楚。 */
    val settingsContent: @Composable () -> Unit,
)

/**
 * 启动器侧（库 / 选择器 / 详情 / 刮削 / 设置）的导航图。
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
    modifier: Modifier = Modifier,
) {
    NavHost(
        navController = navController,
        startDestination = Routes.LIBRARY,
        modifier = modifier,
    ) {
        composable(Routes.LIBRARY) {
            LibraryScreen(
                games = params.games,
                coversDir = params.coversDir,
                sort = params.librarySort,
                onSortChange = params.onLibrarySortChange,
                onLaunch = params.onLaunchGame,
                onOpenDetail = { navController.navigate(Routes.detail(it.id)) },
                onScrape = { navController.navigate(Routes.scrape(it.id)) },
                onRemove = params.onRemoveFromLibrary,
                onAddGame = { navController.navigate(Routes.PICKER) },
                onOpenSettings = { navController.navigate(Routes.SETTINGS) },
            )
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
            params.settingsContent()
        }

        composable(
            route = Routes.DETAIL,
            arguments = listOf(navArgument(Routes.GAME_ID) { type = NavType.StringType }),
        ) { entry ->
            val gameId = entry.arguments?.getString(Routes.GAME_ID).orEmpty()
            val game = params.games.firstOrNull { it.id == gameId }
            if (game == null) {
                // 记录被删掉后仍可能通过返回栈回到这里（比如先删后按返回）
                MissingGame(onBack = { navController.popBackStack() })
            } else {
                val (config, inGameDir) = params.loadGameConfig(game)
                GameDetailScreen(
                    game = game,
                    coversDir = params.coversDir,
                    config = config,
                    configInGameDir = inGameDir,
                    globalDefaults = params.globalDefaults,
                    onSave = { updatedGame, updatedConfig ->
                        params.onSaveGame(updatedGame, updatedConfig)
                    },
                    onScrape = { navController.navigate(Routes.scrape(game.id)) },
                    onLaunch = { params.onLaunchGame(game) },
                    onRemove = {
                        params.onRemoveFromLibrary(game)
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
            if (game == null) {
                MissingGame(onBack = { navController.popBackStack() })
            } else {
                ScrapeScreen(
                    game = game,
                    onApply = { candidate ->
                        params.onApplyScrape(game.id, candidate)
                        navController.popBackStack()
                    },
                    onBack = { navController.popBackStack() },
                )
            }
        }
    }
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
