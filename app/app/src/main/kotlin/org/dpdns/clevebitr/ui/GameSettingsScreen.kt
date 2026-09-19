package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.EngineOverride
import org.dpdns.clevebitr.core.GameConfig
import org.dpdns.clevebitr.core.GlobalDefaults
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.RunMode
import org.dpdns.clevebitr.core.asEngineOverride

/**
 * 游戏设置页：**这个游戏单独怎么跑**。
 *
 * 为什么从详情页拆出来单独一页：详情页要回答的是"这是什么游戏、要不要现在开"，
 * 而这里全是"改了下次启动才生效"的档位。两类信息混在一屏时，启动按钮会被十来个
 * 单选行挤到屏幕外，用户每次开游戏都得先划过一堆自己没打算改的选项。
 *
 * 每项都遵循同一条约定：**留空 = 继承全局，并且写清继承到的具体值**（"继承全局
 * （60 FPS）"而不是"继承全局"）。用户不填时必须能预知会得到什么。
 *
 * 保存是显式的（顶部「保存」），不是改一下就写盘：这些档位是逐游戏试出来的，
 * 边改边写会在用户来回比较时留下一串半成品配置。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GameSettingsScreen(
    game: LibraryGame,
    config: GameConfig,
    globalDefaults: GlobalDefaults,
    onSave: (GameConfig) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // 界面只给"运行模式"一个旋钮（固定组合），两个原始值由模式展开——见 RunMode。
    var runMode by remember(game.id) {
        mutableStateOf(if (config.engine.isEmpty) INHERIT else config.engine.resolvedRunMode().key)
    }
    var fpsLimit by remember(game.id) {
        mutableStateOf(config.engine.fpsLimit?.toString() ?: INHERIT)
    }
    var fontFallback by remember(game.id) {
        mutableStateOf(config.engine.fontFallbackMode ?: INHERIT)
    }
    var useOwnOverlay by remember(game.id) { mutableStateOf(config.overlay != null) }
    var overlay by remember(game.id) { mutableStateOf(config.overlay ?: globalDefaults.overlay) }

    val globalMode = RunMode.fromConfig(
        globalDefaults.compatProfile,
        globalDefaults.oglDrawDeviceCompat,
    )

    fun buildConfig(): GameConfig = config.copy(
        engine = runMode.nullIfInherit()?.let { key ->
            RunMode.fromKey(key).asEngineOverride().copy(
                fpsLimit = fpsLimit.nullIfInherit()?.toIntOrNull(),
                fontFallbackMode = fontFallback.nullIfInherit(),
            )
        } ?: EngineOverride(
            fpsLimit = fpsLimit.nullIfInherit()?.toIntOrNull(),
            fontFallbackMode = fontFallback.nullIfInherit(),
        ),
        overlay = if (useOwnOverlay) overlay else null,
    )

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("游戏设置", maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(
                            text = game.title,
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
                actions = {
                    TextButton(onClick = { onSave(buildConfig()) }) { Text("保存") }
                },
            )
        },
    ) { padding ->
        val wide = LocalConfiguration.current.screenWidthDp >= 600
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .then(if (wide) Modifier.widthIn(max = 560.dp) else Modifier),
            ) {
                Text(
                    text = "这里的设置只影响《${game.title}》，保存后下次启动这个游戏生效。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                )

                SettingsSectionTitle("引擎兼容")

                ChoiceRow(
                    title = "运行模式",
                    subtitle = "兼容层与渲染器设置的固定组合。引擎按游戏目录里的插件标记判档，" +
                        "自动判档不理想时在这里手动指定。",
                    choices = listOf(INHERIT to inheritLabel("模式", globalMode.label)) +
                        RunMode.entries.map { it.key to it.label },
                    selected = runMode,
                    onSelected = { runMode = it },
                )
                if (runMode != INHERIT) {
                    Text(
                        text = RunMode.fromKey(runMode).summary,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp),
                    )
                }

                ChoiceRow(
                    title = "帧率上限",
                    subtitle = "0 = 不限速，跟随 vsync。设备吃不住时可以先限到 30。",
                    choices = listOf(INHERIT to inheritLabel("上限", fpsLabel(globalDefaults.fpsLimit))) +
                        listOf("0" to "不限速", "30" to "30 FPS", "60" to "60 FPS"),
                    selected = fpsLimit,
                    onSelected = { fpsLimit = it },
                )

                ChoiceRow(
                    title = "字体回退",
                    subtitle = "缺字（黑方块、方框大小不一）时在两种实现间切换对比，哪种正常用哪种。",
                    choices = listOf(
                        INHERIT to inheritLabel("策略", globalDefaults.fontFallbackMode),
                    ) + FONT_FALLBACK_CHOICES,
                    selected = fontFallback,
                    onSelected = { fontFallback = it },
                )

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("性能叠加层")

                SwitchRow(
                    title = "使用独立配置",
                    subtitle = "关掉则跟随全局默认（字号、字段、位置都用全局那一份）。",
                    checked = useOwnOverlay,
                    onCheckedChange = { useOwnOverlay = it },
                )
                if (useOwnOverlay) {
                    OverlayConfigEditor(
                        config = overlay,
                        onConfigChange = { overlay = it },
                    )
                } else {
                    Text(
                        text = "当前跟随全局默认：${if (globalDefaults.overlay.enabled) "已开启" else "已关闭"}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp),
                    )
                }

                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { onSave(buildConfig()) },
                        modifier = Modifier.weight(1f),
                    ) { Text("保存") }
                    OutlinedButton(
                        onClick = onBack,
                        modifier = Modifier.weight(1f),
                    ) { Text("取消") }
                }
            }
        }
    }
}

@Composable
private fun SettingsSectionTitle(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 4.dp),
    )
}

/** 「继承全局」在界面上的值。空串不是合法设置值，用它当哨兵不会与真实值冲突。 */
internal const val INHERIT = ""

internal fun String.nullIfInherit(): String? = takeIf { it != INHERIT }

internal fun inheritLabel(what: String, current: String): String = "继承全局（$what：$current）"

internal fun fpsLabel(limit: Int): String = if (limit <= 0) "不限速" else "$limit FPS"

/**
 * 引擎字体回退策略的显示名。值与 `AppPrefs.FONT_FALLBACK_MODES` 一一对应；
 * 全局设置页与游戏设置页共用同一套文案，改一处两边一致。
 */
internal val FONT_FALLBACK_CHOICES = listOf(
    "auto" to "自动",
    "legacy" to "原版",
    "chain" to "链式",
)
