package org.dpdns.clevebitr.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil3.compose.AsyncImage
import java.io.File
import org.dpdns.clevebitr.core.LibraryGame

/**
 * 游戏库主页面。
 *
 * 与旧启动器（纯目录浏览器）的关系：**库是主入口，浏览器降级成"添加游戏"的一个动作**
 * （见 `PickerScreen`）。这样"玩哪个游戏"不再依赖"记住它在哪个目录里"。
 *
 * 交互取舍：
 * - 单击卡片 = 启动。玩家九成时间只做这一件事，不该再进一层详情页。
 * - 长按 = 出菜单（详情/刮削/移出库），与 Android 列表的习惯一致。
 * - 封面缺失时显示占位块而不是默认图：默认图会让"没刮到"和"刮到了但图挂了"看起来一样。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibraryScreen(
    games: List<LibraryGame>,
    coversDir: File,
    sort: String,
    onSortChange: (String) -> Unit,
    onLaunch: (LibraryGame) -> Unit,
    onOpenDetail: (LibraryGame) -> Unit,
    onScrape: (LibraryGame) -> Unit,
    onRemove: (LibraryGame) -> Unit,
    onAddGame: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var sortMenuOpen by remember { mutableStateOf(false) }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("游戏库") },
                actions = {
                    Box {
                        IconButton(onClick = { sortMenuOpen = true }) {
                            Icon(Icons.AutoMirrored.Filled.Sort, contentDescription = "排序")
                        }
                        DropdownMenu(
                            expanded = sortMenuOpen,
                            onDismissRequest = { sortMenuOpen = false },
                        ) {
                            SORT_LABELS.forEach { (key, label) ->
                                DropdownMenuItem(
                                    text = { Text(if (key == sort) "✓ $label" else label) },
                                    onClick = {
                                        sortMenuOpen = false
                                        onSortChange(key)
                                    },
                                )
                            }
                        }
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Filled.Settings, contentDescription = "设置")
                    }
                },
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = onAddGame,
                icon = { Icon(Icons.Filled.Add, contentDescription = null) },
                text = { Text("添加游戏") },
            )
        },
    ) { padding ->
        if (games.isEmpty()) {
            EmptyLibrary(
                onAddGame = onAddGame,
                modifier = Modifier.padding(padding).fillMaxSize(),
            )
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 150.dp),
                modifier = Modifier.padding(padding).fillMaxSize(),
                contentPadding = PaddingValues(12.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                items(games, key = { it.id }) { game ->
                    LibraryCard(
                        game = game,
                        coversDir = coversDir,
                        onLaunch = { onLaunch(game) },
                        onOpenDetail = { onOpenDetail(game) },
                        onScrape = { onScrape(game) },
                        onRemove = { onRemove(game) },
                    )
                }
            }
        }
    }
}

/** 排序键 → 中文标签。键名与 `AppPrefs.LIBRARY_SORTS` 一一对应。 */
private val SORT_LABELS = listOf(
    "lastPlayed" to "最近玩过",
    "title" to "按名称",
    "added" to "按加入时间",
)

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
private fun LibraryCard(
    game: LibraryGame,
    coversDir: File,
    onLaunch: () -> Unit,
    onOpenDetail: () -> Unit,
    onScrape: () -> Unit,
    onRemove: () -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(onClick = onLaunch, onLongClick = { menuOpen = true }),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainer,
        ),
    ) {
        Box {
            Column {
                CoverImage(
                    file = game.coverFile.takeIf { it.isNotBlank() }
                        ?.let { File(coversDir, it) },
                    title = game.title,
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(0.72f)
                        .clip(RoundedCornerShape(topStart = 12.dp, topEnd = 12.dp)),
                )
                Column(modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp)) {
                    Text(
                        text = game.title,
                        style = MaterialTheme.typography.titleSmall,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                    val subtitle = game.subtitle()
                    if (subtitle.isNotBlank()) {
                        Text(
                            text = subtitle,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
            // 右上角的"更多"按钮：长按也能出菜单，但按钮更易发现（长按没有视觉提示）
            Box(modifier = Modifier.align(Alignment.TopEnd)) {
                IconButton(onClick = { menuOpen = true }) {
                    Icon(
                        Icons.Filled.MoreVert,
                        contentDescription = "更多",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                DropdownMenuItem(
                    text = { Text("启动") },
                    leadingIcon = { Icon(Icons.Filled.PlayArrow, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        onLaunch()
                    },
                )
                DropdownMenuItem(
                    text = { Text("详情与配置") },
                    onClick = {
                        menuOpen = false
                        onOpenDetail()
                    },
                )
                DropdownMenuItem(
                    text = { Text("刮削信息") },
                    leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        onScrape()
                    },
                )
                DropdownMenuItem(
                    text = { Text("移出库") },
                    leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        onRemove()
                    },
                )
            }
        }
    }
}

/**
 * 封面。有文件就交给 Coil（它自己管内存/磁盘缓存与采样），没有就画占位块。
 *
 * 占位块用标题首字：一眼能分辨"这是哪一条"，比统一图标有用。
 */
@Composable
internal fun CoverImage(file: File?, title: String, modifier: Modifier = Modifier) {
    if (file != null && file.isFile) {
        AsyncImage(
            model = file,
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = modifier,
        )
    } else {
        Box(
            modifier = modifier.background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center,
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(
                    Icons.Filled.Image,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = title.take(1),
                    style = MaterialTheme.typography.headlineSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun EmptyLibrary(onAddGame: () -> Unit, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(
            Icons.Filled.Image,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            text = "游戏库是空的",
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.padding(top = 12.dp),
        )
        Text(
            text = "用右下角的「添加游戏」浏览到游戏目录加入，或者在目录页里一键扫描整棵目录树。",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 8.dp),
        )
        Row(modifier = Modifier.padding(top = 16.dp)) {
            ExtendedFloatingActionButton(
                onClick = onAddGame,
                icon = { Icon(Icons.Filled.Refresh, contentDescription = null) },
                text = { Text("浏览并添加") },
            )
        }
    }
}
