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
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.core.LibraryGame
import org.dpdns.clevebitr.core.RunMode
import org.dpdns.clevebitr.core.asEngineOverride

/**
 * 游戏设置页
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
    /** 全局按键模板表与增删回调（与设置页共用同一份）。 */
    keyPadTemplates: Map<String, KeyPadProfile> = emptyMap(),
    onSaveKeyPadTemplate: ((String, KeyPadProfile) -> Unit)? = null,
    onDeleteKeyPadTemplate: ((String) -> Unit)? = null,
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
    var useOwnKeypad by remember(game.id) { mutableStateOf(config.keypad != null) }
    var keypad by remember(game.id) { mutableStateOf(config.keypad ?: globalDefaults.keypad) }
    var useOwnTouchpad by remember(game.id) { mutableStateOf(config.touchpad != null) }
    var touchpad by remember(game.id) { mutableStateOf(config.touchpad ?: globalDefaults.touchpad) }
    var useOwnAutoLog by remember(game.id) { mutableStateOf(config.autoLogOnLaunch != null) }
    var autoLog by remember(game.id) {
        mutableStateOf(config.autoLogOnLaunch ?: globalDefaults.autoLogOnLaunch)
    }
    var useOwnGraphics by remember(game.id) { mutableStateOf(config.graphics != null) }
    var graphics by remember(game.id) {
        mutableStateOf(config.graphics ?: globalDefaults.graphics)
    }

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
        keypad = if (useOwnKeypad) keypad else null,
        touchpad = if (useOwnTouchpad) touchpad else null,
        autoLogOnLaunch = if (useOwnAutoLog) autoLog else null,
        graphics = if (useOwnGraphics) graphics else null,
    )

    val snackbar = rememberSnackbarController()

    Scaffold(
        modifier = modifier,
        snackbarHost = { SnackbarPost(snackbar) },
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
                    choices = listOf(INHERIT to inheritLabel("模式", globalMode.label)) +
                        RunMode.entries.map { it.key to it.label },
                    selected = runMode,
                    onSelected = { runMode = it },
                    onHelpClick = {
                        snackbar.showHelp(
                            "兼容层与渲染器设置的固定组合。引擎按游戏目录里的插件标记判档，" +
                                "自动判档不理想时在这里手动指定。",
                        )
                    },
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
                    choices = listOf(INHERIT to inheritLabel("上限", fpsLabel(globalDefaults.fpsLimit))) +
                        listOf("0" to "不限速", "30" to "30 FPS", "60" to "60 FPS"),
                    selected = fpsLimit,
                    onSelected = { fpsLimit = it },
                    onHelpClick = {
                        snackbar.showHelp("0 = 不限速，跟随 vsync。设备吃不住时可以先限到 30。")
                    },
                )

                ChoiceRow(
                    title = "字体回退",
                    choices = listOf(
                        INHERIT to inheritLabel("策略", globalDefaults.fontFallbackMode),
                    ) + FONT_FALLBACK_CHOICES,
                    selected = fontFallback,
                    onSelected = { fontFallback = it },
                    onHelpClick = {
                        snackbar.showHelp(
                            "缺字（黑方块、方框大小不一）时在两种实现间切换对比，哪种正常用哪种。",
                        )
                    },
                )

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("性能叠加层")

                SwitchRow(
                    title = "使用独立配置",
                    checked = useOwnOverlay,
                    onCheckedChange = { useOwnOverlay = it },
                    onHelpClick = {
                        snackbar.showHelp("关掉则跟随全局默认（字号、字段、位置都用全局那一份）。")
                    },
                )
                if (useOwnOverlay) {
                    OverlayConfigEditor(
                        config = overlay,
                        snackbar = snackbar,
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

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("自定义按键浮层")

                SwitchRow(
                    title = "使用独立配置",
                    checked = useOwnKeypad,
                    onCheckedChange = { useOwnKeypad = it },
                    onHelpClick = {
                        snackbar.showHelp("关掉则跟随全局默认（按钮布局与样式都用全局那一份）。")
                    },
                )
                if (useOwnKeypad) {
                    KeyPadConfigEditor(
                        profile = keypad,
                        snackbar = snackbar,
                        onProfileChange = { keypad = it },
                        templates = keyPadTemplates,
                        onSaveTemplate = onSaveKeyPadTemplate,
                        onDeleteTemplate = onDeleteKeyPadTemplate,
                    )
                } else {
                    Text(
                        text = "当前跟随全局默认：${if (globalDefaults.keypad.enabled) "已开启" else "已关闭"}" +
                            "（${globalDefaults.keypad.buttons.size} 个按钮）",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp),
                    )
                }

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("光标触控板")

                SwitchRow(
                    title = "使用独立配置",
                    checked = useOwnTouchpad,
                    onCheckedChange = { useOwnTouchpad = it },
                    onHelpClick = {
                        snackbar.showHelp("关掉则跟随全局默认。")
                    },
                )
                SwitchRow(
                    title = "触控板模式",
                    checked = if (useOwnTouchpad) touchpad else globalDefaults.touchpad,
                    onCheckedChange = { checked ->
                        if (!useOwnTouchpad) useOwnTouchpad = true
                        touchpad = checked
                    },
                    onHelpClick = {
                        snackbar.showHelp(
                            "手指当触控板：相对拖动驱动光标，轻点=左键、双指轻点=右键、" +
                                "双指上下拖=滚轮。适合需要鼠标的游戏。",
                        )
                    },
                )

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("图形")

                SwitchRow(
                    title = "使用独立配置",
                    checked = useOwnGraphics,
                    onCheckedChange = { useOwnGraphics = it },
                    onHelpClick = {
                        snackbar.showHelp(
                            "关掉则跟随全局默认（纹理压缩、精确渲染、纹理尺寸、内存档" +
                                "都用全局那一份）。",
                        )
                    },
                )
                if (useOwnGraphics) {
                    GraphicsConfigEditor(
                        config = graphics,
                        snackbar = snackbar,
                        onConfigChange = { graphics = it },
                    )
                } else {
                    Text(
                        text = "当前跟随全局默认：纹理压缩=" +
                            "${globalDefaults.graphics.textureCompression.label}、" +
                            "精确渲染=${if (globalDefaults.graphics.accurateRender) "开" else "关"}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp),
                    )
                }

                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp))

                SettingsSectionTitle("加载期日志")

                SwitchRow(
                    title = "使用独立配置",
                    checked = useOwnAutoLog,
                    onCheckedChange = { useOwnAutoLog = it },
                    onHelpClick = {
                        snackbar.showHelp(
                            "关掉则跟随全局默认（当前：" +
                                "${if (globalDefaults.autoLogOnLaunch) "已开启" else "已关闭"}）。",
                        )
                    },
                )
                SwitchRow(
                    title = "加载游戏时自动显示日志",
                    checked = if (useOwnAutoLog) autoLog else globalDefaults.autoLogOnLaunch,
                    onCheckedChange = { checked ->
                        if (!useOwnAutoLog) useOwnAutoLog = true
                        autoLog = checked
                    },
                    onHelpClick = {
                        snackbar.showHelp(
                            "从启动到游戏出第一帧期间自动弹出运行时日志，进游戏后自动关闭；" +
                                "手动关掉后本局不再弹。启动阶段就黑屏/卡住时用得上。",
                        )
                    },
                )

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
