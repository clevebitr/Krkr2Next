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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Label
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.FavoriteBorder
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.SuggestionChipDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
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
 * - 单击卡片 = 进**详情页**（[onOpenDetail]）。启动是详情页里最显眼的那个按钮。
 *   此前单击直接启动，代价是"改一项配置"与"看一眼简介"都必须先长按出菜单，
 *   而配置恰恰是遇到跑不动的游戏时最常做的事；把启动放进详情页，两个动作都只差一屏。
 * - 长按 / 右上角按钮 = 出菜单（启动/详情/刮削/收藏/分组/移出库）。
 * - 封面左上角的星标 = 收藏，库页置顶。
 * - 封面缺失时显示占位块而不是默认图：默认图会让"没刮到"和"刮到了但图挂了"看起来一样。
 *
 * 分组是**便签式**的单个标签（见 `LibraryGame.group`）：筛选条上按现分组平铺，
 * 不在库页里做树形结构。
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
    onToggleFavorite: (LibraryGame) -> Unit,
    /**
     * 改分组。**对话框由调用方（导航层）统一提供**：详情页也能改分组，两处必须是同一个
     * 对话框、同一套建议值，否则用户会在两个页面看到两种行为。
     */
    onEditGroup: (LibraryGame) -> Unit,
    onAddGame: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var sortMenuOpen by remember { mutableStateOf(false) }
    // 筛选只影响显示，不落盘：它是"这一眼想看什么"，不是用户的长期设置。
    var onlyFavorites by remember { mutableStateOf(false) }
    var groupFilter by remember { mutableStateOf<String?>(null) }

    val groups = remember(games) {
        games.map { it.group }.filter { it.isNotBlank() }.distinct().sorted()
    }
    // 分组被改空之后筛选条上不该再留着它，否则筛出来永远是空列表。用副作用而不是
    // 直接在组合里赋值：组合可能被丢弃重来，副作用才是有保证的写入时机。
    LaunchedEffect(groups) {
        if (groupFilter != null && groupFilter !in groups) groupFilter = null
    }

    val shown = remember(games, onlyFavorites, groupFilter) {
        games.filter { game ->
            (!onlyFavorites || game.favorite) && (groupFilter == null || game.group == groupFilter)
        }
    }
    val favoriteCount = remember(games) { games.count { it.favorite } }

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
                                    text = { Text(label) },
                                    // 当前项用尾部对勾标记，而不是在文本前面拼 "✓ "：
                                    // 拼字符串会让未选中项的起始位置与选中项差一个字宽。
                                    trailingIcon = if (key == sort) {
                                        { Icon(Icons.Filled.Check, contentDescription = "当前排序") }
                                    } else {
                                        null
                                    },
                                    onClick = {
                                        sortMenuOpen = false
                                        onSortChange(key)
                                    },
                                )
                            }
                        }
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
            Column(modifier = Modifier.padding(padding).fillMaxSize()) {
                // 筛选条：收藏 + 分组。一个分组都没有、也没有收藏时不占高度。
                if (groups.isNotEmpty() || favoriteCount > 0) {
                    LazyRow(
                        modifier = Modifier.fillMaxWidth(),
                        contentPadding = PaddingValues(horizontal = 12.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        item(key = "__all__") {
                            FilterChip(
                                selected = !onlyFavorites && groupFilter == null,
                                onClick = {
                                    onlyFavorites = false
                                    groupFilter = null
                                },
                                label = { Text("全部 ${games.size}") },
                            )
                        }
                        if (favoriteCount > 0) {
                            item(key = "__fav__") {
                                FilterChip(
                                    selected = onlyFavorites,
                                    onClick = { onlyFavorites = !onlyFavorites },
                                    label = { Text("收藏 $favoriteCount") },
                                    leadingIcon = {
                                        Icon(
                                            Icons.Filled.Favorite,
                                            contentDescription = null,
                                            modifier = Modifier.padding(0.dp),
                                        )
                                    },
                                )
                            }
                        }
                        items(groups, key = { "g:$it" }) { group ->
                            FilterChip(
                                selected = groupFilter == group,
                                onClick = {
                                    groupFilter = if (groupFilter == group) null else group
                                },
                                label = { Text(group) },
                            )
                        }
                    }
                }

                if (shown.isEmpty()) {
                    FilteredEmpty(modifier = Modifier.fillMaxSize())
                } else {
                    // 宽屏（平板/横屏）下把最小卡片尺寸抬高：`Adaptive` 会尽量多塞列，
                    // 手机上的 150dp 在 1200dp 平板上会变成 7–8 列的小豆腐块。
                    val wide = LocalConfiguration.current.screenWidthDp >= 600
                    LazyVerticalGrid(
                        columns = GridCells.Adaptive(minSize = if (wide) 200.dp else 150.dp),
                        modifier = Modifier.fillMaxSize(),
                        // 底部留出一整个 FAB 的高度：否则最后一行卡片会被右下角的
                        // 「添加游戏」按钮盖住，点不到也看不全。
                        contentPadding = PaddingValues(
                            start = 12.dp,
                            end = 12.dp,
                            top = 12.dp,
                            bottom = 96.dp,
                        ),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        items(shown, key = { it.id }) { game ->
                            LibraryCard(
                                game = game,
                                coversDir = coversDir,
                                onLaunch = { onLaunch(game) },
                                onOpenDetail = { onOpenDetail(game) },
                                onScrape = { onScrape(game) },
                                onRemove = { onRemove(game) },
                                onToggleFavorite = { onToggleFavorite(game) },
                                onEditGroup = { onEditGroup(game) },
                            )
                        }
                    }
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

/** 库页给"新分组"的常用建议。分组是自由文本，这几个只是省打字。 */
private val GROUP_SUGGESTIONS = listOf("在玩", "待玩", "已通关", "搁置")

/**
 * 分组便签底边与标题栏顶边的间距。
 */
private val GROUP_CHIP_TITLE_GAP = 5.dp

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun LibraryCard(
    game: LibraryGame,
    coversDir: File,
    onLaunch: () -> Unit,
    onOpenDetail: () -> Unit,
    onScrape: () -> Unit,
    onRemove: () -> Unit,
    onToggleFavorite: () -> Unit,
    onEditGroup: () -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(onClick = onOpenDetail, onLongClick = { menuOpen = true }),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainer,
        ),
    ) {
        Box {
            Column {
                Box(modifier = Modifier.fillMaxWidth()) {
                    CoverImage(
                        file = game.coverFile.takeIf { it.isNotBlank() }
                            ?.let { File(coversDir, it) },
                        title = game.title,
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(0.72f)
                            .clip(RoundedCornerShape(topStart = 12.dp, topEnd = 12.dp)),
                    )
                    if (game.group.isNotBlank()) {
                        SuggestionChip(
                            onClick = onEditGroup,
                            label = {
                                Text(
                                    text = game.group,
                                    style = MaterialTheme.typography.labelSmall,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                            },
                            icon = {
                                Icon(
                                    Icons.AutoMirrored.Filled.Label,
                                    contentDescription = null,
                                    modifier = Modifier.padding(0.dp),
                                )
                            },
                            border = SuggestionChipDefaults.suggestionChipBorder(
                                enabled = true,
                                borderColor = MaterialTheme.colorScheme.outline,
                            ),
                            modifier = Modifier
                                .align(Alignment.BottomStart)
                                .padding(start = 6.dp, bottom = GROUP_CHIP_TITLE_GAP),
                        )
                    }
                }
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

            IconButton(
                onClick = onToggleFavorite,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(6.dp)
                    .background(Color(0x66000000), CircleShape),
            ) {
                Icon(
                    imageVector = if (game.favorite) {
                        Icons.Filled.Favorite
                    } else {
                        Icons.Filled.FavoriteBorder
                    },
                    contentDescription = if (game.favorite) "取消收藏" else "收藏",
                    tint = if (game.favorite) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        Color.White
                    },
                )
            }

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
                    text = { Text("详情") },
                    onClick = {
                        menuOpen = false
                        onOpenDetail()
                    },
                )
                DropdownMenuItem(
                    text = { Text(if (game.favorite) "取消收藏" else "收藏") },
                    leadingIcon = {
                        Icon(
                            if (game.favorite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                            contentDescription = null,
                        )
                    },
                    onClick = {
                        menuOpen = false
                        onToggleFavorite()
                    },
                )
                DropdownMenuItem(
                    text = { Text(if (game.group.isBlank()) "设置分组" else "分组：${game.group}") },
                    leadingIcon = { Icon(Icons.AutoMirrored.Filled.Label, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        onEditGroup()
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
 * 分组编辑器。
 */
@Composable
internal fun GroupEditorDialog(
    game: LibraryGame,
    knownGroups: List<String>,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var text by remember(game.id) { mutableStateOf(game.group) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("分组") },
        text = {
            Column {
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    label = { Text("分组名（留空 = 不分组）") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                val suggestions = (knownGroups + GROUP_SUGGESTIONS).distinct()
                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    suggestions.take(4).forEach { group ->
                        AssistChip(
                            onClick = { text = group },
                            label = { Text(group, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(text.trim()) }) {
                Text("保存")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("取消") }
        },
    )
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

/** 筛选之后空了：库不是空的，别让用户以为记录丢了。 */
@Composable
private fun FilteredEmpty(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "没有符合条件的游戏",
            style = MaterialTheme.typography.titleMedium,
        )
        Text(
            text = "换一个分组，或者取消筛选。",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 8.dp),
        )
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
            Icons.Filled.SportsEsports,
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
            // 这里用普通按钮而不是第二个 FAB：Scaffold 已经有「添加游戏」FAB，
            // 同屏两个 FAB 违反 MD3 的单一主操作原则，也会让人不知道点哪个。
            OutlinedButton(onClick = onAddGame) {
                Icon(Icons.Filled.Add, contentDescription = null)
                Text("浏览并添加", modifier = Modifier.padding(start = 6.dp))
            }
        }
    }
}
