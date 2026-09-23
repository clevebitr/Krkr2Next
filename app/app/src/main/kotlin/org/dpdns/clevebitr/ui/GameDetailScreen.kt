package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Label
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.FavoriteBorder
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GameMetadata
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.RunMode

/**
 * 游戏详情页
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun GameDetailScreen(
    game: LibraryGame,
    coversDir: File,
    config: GameConfig,
    /** 配置文件是否落在游戏目录（false = 游戏目录不可写，落在应用私有）。 */
    configInGameDir: Boolean,
    globalDefaults: GlobalDefaults,
    onSave: (LibraryGame, GameConfig) -> Unit,
    onLaunch: () -> Unit,
    onOpenSettings: () -> Unit,
    onScrape: () -> Unit,
    onToggleFavorite: () -> Unit,
    onEditGroup: () -> Unit,
    onRemove: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // 以 game.id 为 key：换一条记录必须重新初始化，否则会看到上一条的内容
    var editing by remember(game.id) { mutableStateOf(false) }
    var title by remember(game.id) { mutableStateOf(game.title) }
    var developer by remember(game.id) { mutableStateOf(game.developer) }
    var notes by remember(game.id) { mutableStateOf(game.notes) }
    var descriptionExpanded by remember(game.id) { mutableStateOf(false) }
    var menuOpen by remember(game.id) { mutableStateOf(false) }
    var confirmRemove by remember(game.id) { mutableStateOf(false) }

    fun editedGame(): LibraryGame = game.copy(
        title = title.trim().ifBlank { game.title },
        developer = developer.trim(),
        notes = notes.trim(),
    )

    /** 写回记录与配置。配置段原样保留，只把元数据/备注换成界面上的值。 */
    fun save() {
        val updatedGame = editedGame()
        onSave(
            updatedGame,
            config.copy(
                metadata = GameMetadata(
                    title = updatedGame.title.takeIf { it.isNotBlank() },
                    developer = updatedGame.developer.takeIf { it.isNotBlank() },
                    vndbId = game.vndbId.takeIf { it.isNotBlank() },
                    released = game.released.takeIf { it.isNotBlank() },
                    tags = game.tags,
                    description = game.description.takeIf { it.isNotBlank() },
                    coverFile = game.coverFile.takeIf { it.isNotBlank() },
                ),
                notes = notes.trim().ifBlank { null },
            ),
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("游戏详情", maxLines = 1, overflow = TextOverflow.Ellipsis) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
                actions = {
                    if (editing) {
                        IconButton(
                            onClick = {
                                save()
                                editing = false
                            },
                        ) {
                            Icon(Icons.Filled.Edit, contentDescription = "保存")
                        }
                    } else {
                        Box {
                            IconButton(onClick = { menuOpen = true }) {
                                Icon(Icons.Filled.MoreVert, contentDescription = "更多")
                            }
                            DropdownMenu(
                                expanded = menuOpen,
                                onDismissRequest = { menuOpen = false },
                            ) {
                                DropdownMenuItem(
                                    text = { Text("编辑信息") },
                                    leadingIcon = { Icon(Icons.Filled.Edit, contentDescription = null) },
                                    onClick = {
                                        menuOpen = false
                                        editing = true
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
                                    text = { Text(if (game.favorite) "取消收藏" else "收藏") },
                                    leadingIcon = {
                                        Icon(
                                            if (game.favorite) {
                                                Icons.Filled.Favorite
                                            } else {
                                                Icons.Filled.FavoriteBorder
                                            },
                                            contentDescription = null,
                                        )
                                    },
                                    onClick = {
                                        menuOpen = false
                                        onToggleFavorite()
                                    },
                                )
                                DropdownMenuItem(
                                    text = {
                                        Text(if (game.group.isBlank()) "设置分组" else "分组：${game.group}")
                                    },
                                    leadingIcon = { Icon(Icons.AutoMirrored.Filled.Label, contentDescription = null) },
                                    onClick = {
                                        menuOpen = false
                                        onEditGroup()
                                    },
                                )
                                DropdownMenuItem(
                                    text = { Text("移出游戏库") },
                                    leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = null) },
                                    onClick = {
                                        menuOpen = false
                                        confirmRemove = true
                                    },
                                )
                            }
                        }
                    }
                },
            )
        },
    ) { padding ->
        // 宽屏下限制正文宽度：平板横屏时一行 1200dp 的文字没人读得下去
        val wide = LocalConfiguration.current.screenWidthDp >= 600
        val contentWidth = if (wide) Modifier.widthIn(max = 560.dp) else Modifier
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Column(modifier = Modifier.fillMaxWidth().then(contentWidth)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, top = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(
                        if (wide) 20.dp else 12.dp,
                    ),
                ) {
                    // 左：封面。收藏星标压在封面左上角，与库页卡片的位置一致。
                    Box {
                        IconButton(onClick = onToggleFavorite) {
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
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                            )
                        }
                        CoverImage(
                            file = game.coverFile.takeIf { it.isNotBlank() }
                                ?.let { File(coversDir, it) },
                            title = title,
                            modifier = Modifier
                                .width(if (wide) 240.dp else 132.dp)
                                .padding(bottom = 20.dp)
                                .aspectRatio(0.72f)
                                .clip(RoundedCornerShape(12.dp)),
                        )
                        VerticalDivider()
                    }

                    // 右：标题/厂商/分组 + 主操作（编辑态就地替换标题区）
                    Column(modifier = Modifier.weight(1f)) {
                        if (editing) {
                            Column(modifier = Modifier.fillMaxWidth()) {
                                OutlinedTextField(
                                    value = title,
                                    onValueChange = { title = it },
                                    label = { Text("标题") },
                                    singleLine = true,
                                    modifier = Modifier.fillMaxWidth(),
                                )
                                OutlinedTextField(
                                    value = developer,
                                    onValueChange = { developer = it },
                                    label = { Text("厂商 / 汉化组") },
                                    singleLine = true,
                                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                                )
                                OutlinedTextField(
                                    value = notes,
                                    onValueChange = { notes = it },
                                    label = { Text("备注（版本、汉化、踩坑记录）") },
                                    maxLines = 4,
                                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                                )
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                ) {
                                    Button(
                                        onClick = {
                                            save()
                                            editing = false
                                        },
                                        modifier = Modifier.weight(1f),
                                    ) { Text("保存") }
                                    OutlinedButton(
                                        onClick = {
                                            // 放弃改动：把字段还原成进入编辑前的值
                                            title = game.title
                                            developer = game.developer
                                            notes = game.notes
                                            editing = false
                                        },
                                        modifier = Modifier.weight(1f),
                                    ) { Text("取消") }
                                }
                            }
                        } else {
                            Text(
                                text = game.title,
                                style = MaterialTheme.typography.headlineSmall,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.fillMaxWidth(),
                            )
                            val subtitle = listOf(game.developer, game.released)
                                .filter { it.isNotBlank() }
                                .joinToString(" · ")
                            if (subtitle.isNotBlank()) {
                                Text(
                                    text = subtitle,
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
                                )
                            }
                            if (game.group.isNotBlank()) {
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
                                ) {
                                    SuggestionChip(
                                        onClick = onEditGroup,
                                        label = { Text(game.group) },
                                        icon = {
                                            Icon(
                                                Icons.AutoMirrored.Filled.Label,
                                                contentDescription = null,
                                                modifier = Modifier.padding(0.dp),
                                            )
                                        },
                                    )
                                }
                            }
                        }

                        // ── 标签：在简介之上（两者都在封面右侧） ──
                        if (game.tags.isNotEmpty()) {
                            FlowRow(
                                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                                verticalArrangement = Arrangement.spacedBy(6.dp),
                            ) {
                                game.tags.take(12).forEach { tag -> TagPill(tag) }
                            }
                        }

                        // ── 简介：可折叠。右栏（标签/简介）可能比封面高，
                        // 所以默认只显示几行，需要时再展开。 ──
                        if (game.description.isNotBlank()) {
                            Text(
                                text = game.description,
                                style = MaterialTheme.typography.bodyMedium,
                                maxLines = if (descriptionExpanded) {
                                    Int.MAX_VALUE
                                } else {
                                    DESCRIPTION_COLLAPSED_LINES
                                },
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                            )
                            TextButton(onClick = { descriptionExpanded = !descriptionExpanded }) {
                                Text(if (descriptionExpanded) "收起简介" else "展开简介")
                            }
                        }
                    }
                }

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, top = 12.dp, bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = {
                            // 先把界面上的改动落盘再开：否则"改完标题直接点启动"会丢改动，
                            // 而用户完全看不出发生了什么。
                            if (editing) {
                                save()
                                editing = false
                            }
                            onLaunch()
                        },
                        modifier = Modifier.weight(1f),
                    ) {
                        Icon(Icons.Filled.PlayArrow, contentDescription = null)
                        Text("启动游戏", modifier = Modifier.padding(start = 6.dp))
                    }
                    FilledTonalButton(
                        onClick = onOpenSettings,
                        modifier = Modifier.weight(1f),
                    ) {
                        Icon(Icons.Filled.Settings, contentDescription = null)
                        Text("游戏设置", modifier = Modifier.padding(start = 6.dp))
                    }
                }

                // ── 当前生效的配置摘要：详情页只给结论，改去设置页 ──
                Card(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                    ),
                ) {
                    Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                        Text(
                            text = "当前生效",
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.primary,
                        )
                        val mode = if (config.engine.isEmpty) {
                            "跟随全局：${RunMode.fromConfig(globalDefaults.compatProfile, globalDefaults.oglDrawDeviceCompat).label}"
                        } else {
                            config.engine.resolvedRunMode().label
                        }
                        SummaryLine("运行模式", mode)
                        SummaryLine(
                            "帧率上限",
                            config.engine.fpsLimit?.toString()
                                ?: "跟随全局（${fpsLabel(globalDefaults.fpsLimit)}）",
                        )
                        SummaryLine(
                            "字体回退",
                            config.engine.fontFallbackMode
                                ?: "跟随全局（${globalDefaults.fontFallbackMode}）",
                        )
                        SummaryLine(
                            "性能叠加层",
                            when (val own = config.overlay) {
                                null ->
                                    if (globalDefaults.overlay.enabled) "跟随全局：开" else "跟随全局：关"

                                else -> if (own.enabled) "独立配置：开" else "独立配置：关"
                            },
                        )
                        SummaryLine(
                            "自定义按键",
                            when (val own = config.keypad) {
                                null ->
                                    if (globalDefaults.keypad.enabled) {
                                        "跟随全局：开（${globalDefaults.keypad.buttons.size} 个按钮）"
                                    } else {
                                        "跟随全局：关"
                                    }

                                else ->
                                    if (own.enabled) "独立配置：开（${own.buttons.size} 个按钮）" else "独立配置：关"
                            },
                        )
                    }
                }

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                // ── 信息行 ──
                InfoRow("路径", game.path)
                if (game.vndbId.isNotBlank()) InfoRow("VNDB", game.vndbId)
                InfoRow("来源", if (gameInLibrary(game)) "已加入游戏库" else "临时目录")
                InfoRow(
                    "启动次数",
                    if (game.playCount <= 0) "还没玩过" else "${game.playCount} 次",
                )
                InfoRow("最近启动", formatTime(game.lastPlayedAt))
                InfoRow(
                    "配置文件",
                    if (configInGameDir) {
                        "游戏目录下的 krkr2next.json（重装应用也在）"
                    } else {
                        "游戏目录不可写，已存到应用私有目录（卸载会丢）"
                    },
                )
                if (game.notes.isNotBlank() && !editing) {
                    InfoRow("备注", game.notes)
                }

                TextButton(
                    onClick = onOpenSettings,
                    modifier = Modifier.padding(start = 8.dp, top = 4.dp),
                ) {
                    Icon(Icons.Filled.Settings, contentDescription = null)
                    Text("打开游戏设置", modifier = Modifier.padding(start = 6.dp))
                }

                TextButton(
                    onClick = { confirmRemove = true },
                    modifier = Modifier.padding(start = 8.dp, bottom = 24.dp),
                ) {
                    Icon(
                        Icons.Filled.Delete,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.error,
                    )
                    Text(
                        text = "移出游戏库",
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.padding(start = 6.dp),
                    )
                }
            }
        }
    }

    if (confirmRemove) {
        AlertDialog(
            onDismissRequest = { confirmRemove = false },
            title = { Text("移出游戏库？") },
            text = {
                Text(
                    "《${game.title}》会从游戏库列表里移除。\n" +
                        "游戏文件不会被删除，之后还能重新添加。",
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        confirmRemove = false
                        onRemove()
                    },
                ) { Text("移出", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { confirmRemove = false }) { Text("取消") }
            },
        )
    }
}

/** 摘要一行：左标签右值，值可换行。 */
@Composable
private fun SummaryLine(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(end = 8.dp),
        )
        Text(text = value, style = MaterialTheme.typography.bodySmall)
    }
}

/** 简介默认收起的行数；展开后显示全部。 */
private const val DESCRIPTION_COLLAPSED_LINES = 5

/** 只读标签胶囊。与 `SuggestionChip` 区分：它没有涟漪、没有点击语义。 */
@Composable
private fun TagPill(text: String) {
    Surface(
        shape = RoundedCornerShape(50),
        color = MaterialTheme.colorScheme.secondaryContainer,
        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelMedium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
        )
    }
}

/**
 * 信息行：上标签下值。
 */
@Composable
private fun InfoRow(label: String, value: String) {
    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(text = value, style = MaterialTheme.typography.bodyMedium)
    }
}

/** 详情页始终来自游戏库（临时目录的启动不经过这里），保留这个判断只为让文案可读。 */
private fun gameInLibrary(game: LibraryGame): Boolean = game.addedAt > 0L

private val timeFormat = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)

private fun formatTime(millis: Long): String =
    if (millis <= 0L) "从未" else timeFormat.format(Date(millis))
