package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.io.File
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.EngineOverride
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GameMetadata
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.OverlayConfig

/**
 * 游戏详情页：**这个游戏单独怎么跑**。
 *
 * 三块内容按"用户改动频率"排：封面与元数据（刮削或手填） → 引擎覆盖（逐游戏试出来的
 * 兼容档） → 叠加层覆盖（每个游戏想看的指标不一样）。每处都遵循同一条约定：
 * **留空 = 继承全局**，界面上写明"继承的是什么"，用户才知道不填会得到什么。
 *
 * 保存是显式的（底部按钮），不是改一下就写盘：引擎覆盖要"下次启动生效"，
 * 边改边写会在用户来回试的时候留下半成品配置。
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
    onScrape: () -> Unit,
    onLaunch: () -> Unit,
    onRemove: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // 以 game.id 为 key：换一条记录必须重新初始化，否则会看到上一条的内容
    var title by remember(game.id) { mutableStateOf(game.title) }
    var developer by remember(game.id) { mutableStateOf(game.developer) }
    var notes by remember(game.id) { mutableStateOf(game.notes) }
    var descriptionExpanded by remember(game.id) { mutableStateOf(false) }

    var compatProfile by remember(game.id) { mutableStateOf(config.engine.compatProfile ?: INHERIT) }
    var oglCompat by remember(game.id) {
        mutableStateOf(config.engine.oglDrawDeviceCompat ?: INHERIT)
    }
    var fpsLimit by remember(game.id) {
        mutableStateOf(config.engine.fpsLimit?.toString() ?: INHERIT)
    }
    var fontFallback by remember(game.id) {
        mutableStateOf(config.engine.fontFallbackMode ?: INHERIT)
    }

    var useOwnOverlay by remember(game.id) { mutableStateOf(config.overlay != null) }
    var overlay by remember(game.id) { mutableStateOf(config.overlay ?: globalDefaults.overlay) }

    var savedHint by remember(game.id) { mutableStateOf<String?>(null) }

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
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState()),
        ) {
            // ── 封面与元数据 ──
            Row(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
                CoverImage(
                    file = game.coverFile.takeIf { it.isNotBlank() }?.let { File(coversDir, it) },
                    title = title,
                    modifier = Modifier
                        .fillMaxWidth(0.34f)
                        .aspectRatio(0.72f)
                        .clip(RoundedCornerShape(8.dp)),
                )
                Column(modifier = Modifier.padding(start = 16.dp)) {
                    OutlinedTextField(
                        value = title,
                        onValueChange = {
                            title = it
                            savedHint = null
                        },
                        label = { Text("标题") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    OutlinedTextField(
                        value = developer,
                        onValueChange = {
                            developer = it
                            savedHint = null
                        },
                        label = { Text("厂商 / 汉化组") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                    )
                    if (game.released.isNotBlank() || game.vndbId.isNotBlank()) {
                        Text(
                            text = listOf(game.released, game.vndbId)
                                .filter { it.isNotBlank() }
                                .joinToString(" · "),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                }
            }

            if (game.tags.isNotEmpty()) {
                FlowRow(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    game.tags.forEach { tag ->
                        AssistChip(onClick = {}, label = { Text(tag) })
                    }
                }
            }

            if (game.description.isNotBlank()) {
                Text(
                    text = game.description,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = if (descriptionExpanded) Int.MAX_VALUE else 6,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                )
                TextButton(
                    onClick = { descriptionExpanded = !descriptionExpanded },
                    modifier = Modifier.padding(horizontal = 8.dp),
                ) {
                    Text(if (descriptionExpanded) "收起简介" else "展开简介")
                }
            }

            Text(
                text = "路径：${game.path}",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            )

            // ── 引擎覆盖 ──
            Text(
                text = "此游戏的引擎配置",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp),
            )
            Text(
                text = "留「继承全局」就用设置页里的默认值；改哪一项只影响这个游戏，" +
                    "下次启动游戏生效。",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp),
            )

            ChoiceRow(
                title = "游戏兼容档",
                subtitle = "决定按哪条血脉跑（krkiri2 经典 / krkrz GPU / KAG 接管）。",
                choices = listOf(INHERIT to inheritLabel("兼容档", globalDefaults.compatProfile)) +
                    AppPrefs.GAME_COMPAT_PROFILES.filter { it != "auto" }
                        .map { it to profileLabel(it) },
                selected = compatProfile,
                onSelected = {
                    compatProfile = it
                    savedHint = null
                },
            )
            ChoiceRow(
                title = "OGLDrawDevice 兼容层",
                subtitle = "挂上 Window.OGLDrawDevice 后游戏才加载 GPU 层脚本；逐游戏试。",
                choices = listOf(INHERIT to inheritLabel("档位", globalDefaults.oglDrawDeviceCompat)) +
                    AppPrefs.OGLDRAWDEVICE_COMPAT_MODES.map { it to it },
                selected = oglCompat,
                onSelected = {
                    oglCompat = it
                    savedHint = null
                },
            )
            ChoiceRow(
                title = "帧率上限",
                subtitle = "0 = 不限速，跟随 vsync。",
                choices = listOf(INHERIT to inheritLabel("上限", "${globalDefaults.fpsLimit}")) +
                    listOf("0" to "不限速", "30" to "30 FPS", "60" to "60 FPS"),
                selected = fpsLimit,
                onSelected = {
                    fpsLimit = it
                    savedHint = null
                },
            )
            ChoiceRow(
                title = "字体回退",
                subtitle = "缺字（黑方块）时可在两种实现间切换对比。",
                choices = listOf(INHERIT to inheritLabel("策略", globalDefaults.fontFallbackMode)) +
                    AppPrefs.FONT_FALLBACK_MODES.map { it to it },
                selected = fontFallback,
                onSelected = {
                    fontFallback = it
                    savedHint = null
                },
            )

            // ── 叠加层覆盖 ──
            Text(
                text = "此游戏的性能叠加层",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp),
            )
            SwitchRow(
                title = "使用独立配置",
                subtitle = "关掉则跟随全局默认（字号/字段/位置都用全局那份）。",
                checked = useOwnOverlay,
                onCheckedChange = {
                    useOwnOverlay = it
                    savedHint = null
                },
            )
            if (useOwnOverlay) {
                OverlayConfigEditor(
                    config = overlay,
                    onConfigChange = {
                        overlay = it
                        savedHint = null
                    },
                )
            }

            // ── 备注 ──
            OutlinedTextField(
                value = notes,
                onValueChange = {
                    notes = it
                    savedHint = null
                },
                label = { Text("备注（汉化组、版本、踩坑记录）") },
                modifier = Modifier.fillMaxWidth().padding(16.dp),
                maxLines = 4,
            )

            Text(
                text = if (configInGameDir) {
                    "配置文件：游戏目录下的 ${"krkr2next.json"}（重装应用也在）"
                } else {
                    "配置文件：游戏目录不可写，已存到应用私有目录" +
                        "（卸载应用会丢；需要跟游戏走请给该目录写权限）"
                },
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            )

            savedHint?.let { hint ->
                Text(
                    text = hint,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }

            Row(
                modifier = Modifier.fillMaxWidth().padding(16.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = onLaunch,
                    modifier = Modifier.weight(1f),
                ) {
                    Icon(Icons.Filled.PlayArrow, contentDescription = null)
                    Text("启动", modifier = Modifier.padding(start = 6.dp))
                }
                OutlinedButton(onClick = onScrape) {
                    Icon(Icons.Filled.Search, contentDescription = null)
                    Text("刮削", modifier = Modifier.padding(start = 6.dp))
                }
            }

            Button(
                onClick = {
                    val updatedGame = game.copy(
                        title = title.trim().ifBlank { game.title },
                        developer = developer.trim(),
                        notes = notes.trim(),
                    )
                    val updatedConfig = GameConfig(
                        engine = EngineOverride(
                            compatProfile = compatProfile.nullIfInherit(),
                            oglDrawDeviceCompat = oglCompat.nullIfInherit(),
                            fpsLimit = fpsLimit.nullIfInherit()?.toIntOrNull(),
                            fontFallbackMode = fontFallback.nullIfInherit(),
                        ),
                        overlay = if (useOwnOverlay) overlay else null,
                        // 元数据段保留刮削结果；标题/厂商以界面上的值为准
                        metadata = GameMetadata(
                            title = title.trim().ifBlank { null },
                            developer = developer.trim().ifBlank { null },
                            vndbId = game.vndbId.ifBlank { null },
                            released = game.released.ifBlank { null },
                            tags = game.tags,
                            description = game.description.ifBlank { null },
                            coverFile = game.coverFile.ifBlank { null },
                        ),
                        notes = notes.trim().ifBlank { null },
                    )
                    onSave(updatedGame, updatedConfig)
                    savedHint = "已保存（引擎配置下次启动游戏生效）"
                },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            ) {
                Text("保存")
            }

            TextButton(
                onClick = onRemove,
                modifier = Modifier.padding(start = 8.dp, bottom = 24.dp),
            ) {
                Icon(Icons.Filled.Delete, contentDescription = null)
                Text("移出游戏库", modifier = Modifier.padding(start = 6.dp))
            }

            // 底部留白：最后一个按钮贴着导航栏不好按
            Column(modifier = Modifier.height(24.dp)) {}
        }
    }
}

/** 「继承全局」在界面上的值。空串不是合法设置值，用它当哨兵不会与真实值冲突。 */
private const val INHERIT = ""

private fun String.nullIfInherit(): String? = takeIf { it != INHERIT }

private fun inheritLabel(what: String, current: String): String = "继承全局（$what：$current）"

private fun profileLabel(profile: String): String = when (profile) {
    "kirikiri2-classic" -> "kirikiri2-classic（经典）"
    "krkrz-gpu" -> "krkrz-gpu（GPU 层）"
    "krkrz-kag" -> "krkrz-kag（KAG 接管）"
    "krkrz-ogl" -> "krkrz-ogl（仅 OGLDrawDevice）"
    else -> profile
}
