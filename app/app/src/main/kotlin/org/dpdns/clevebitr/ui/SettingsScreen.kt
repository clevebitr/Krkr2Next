package org.dpdns.clevebitr.ui

import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Slider
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.util.Locale
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.RunMode
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.BuildInfo
import org.dpdns.clevebitr.core.GraphicsConfig
import org.dpdns.clevebitr.core.LogFiles

private const val TAG = "KrKr2Next/Settings"

/** 主题三档。值即 `AppPrefs.THEME_MODES`。 */
private val THEME_CHOICES = listOf(
    "system" to "跟随系统",
    "light" to "浅色",
    "dark" to "深色",
)

/**
 * 设置页：**全局默认值**与调试开关。
 *
 * 分界很清楚——这里改的是"所有游戏默认怎么跑"，某个游戏要单独一套就去它的
 * 「游戏设置」页（`GameSettingsScreen`）；只读信息（作者/仓库/协议/版本）在
 * 「关于」页。三类内容混在一页时，用户分不清哪个开关会影响别的游戏。
 *
 * 设置项都服务于"把问题现场原样带出来"：日志怎么收、怎么导出、引擎跑多快、
 * 画面上叠什么。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    logDirPath: String,
    onBack: () -> Unit,
    onShareLogs: () -> Unit,
    /** 当前**全局默认**叠加层配置（每游戏覆盖在各自的 `krkr2next.json` 里）。 */
    overlayConfig: OverlayConfig = OverlayConfig.default(),
    /**
     * 叠加层配置变化时回调。壳层据此立刻应用——设置页也能在游戏里打开
     * （悬浮菜单 -> 设置），光写偏好设置要退出重进才看得到，那就等于没生效。
     */
    onOverlayConfigChanged: (OverlayConfig) -> Unit = {},
    /** 当前**全局默认**自定义按键浮层（每游戏覆盖在各自的 `krkr2next.json` 里）。 */
    keyPadProfile: KeyPadProfile = KeyPadProfile.default(),
    /** 按键浮层全局默认变化时回调；壳层据此立刻应用到没有独立配置的游戏。 */
    onKeyPadProfileChanged: (KeyPadProfile) -> Unit = {},
    /** 具名按键模板表（全局）。 */
    keyPadTemplates: Map<String, KeyPadProfile> = emptyMap(),
    onSaveKeyPadTemplate: ((String, KeyPadProfile) -> Unit)? = null,
    onDeleteKeyPadTemplate: ((String) -> Unit)? = null,
    /** 全局默认光标触控板模式。 */
    touchpadMode: Boolean = false,
    onTouchpadModeChange: (Boolean) -> Unit = {},
    touchpadSensitivity: Float = AppPrefs.TOUCHPAD_SENSITIVITY_DEFAULT,
    onTouchpadSensitivityChange: (Float) -> Unit = {},
    /** 全局默认"加载游戏时自动显示运行时日志浮层"（默认关）。 */
    autoLogOnLaunch: Boolean = false,
    onAutoLogOnLaunchChange: (Boolean) -> Unit = {},
    /** 当前**全局默认**图形设置（每游戏覆盖在各自的 `krkr2next.json` 里）。 */
    graphicsConfig: GraphicsConfig = GraphicsConfig.default(),
    onGraphicsConfigChanged: (GraphicsConfig) -> Unit = {},
    /** 游戏中右下角是否显示「引擎菜单」按钮（§4 侧边栏入口）。 */
    engineMenuButton: Boolean = true,
    onEngineMenuButtonChange: (Boolean) -> Unit = {},
    /** 主题档位；改完立刻换肤，所以要回调给壳层（与叠加层同理）。 */
    themeMode: String = "system",
    onThemeModeChanged: (String) -> Unit = {},
    /** 引擎字体回退策略；改完**下次开游戏**生效（引擎侧只在初始化时读一次）。 */
    fontFallbackMode: String = "auto",
    onFontFallbackModeChanged: (String) -> Unit = {},
    /** krkrz 的 OGLDrawDevice 兼容档位；同样**下次开游戏**生效。 */
    oglDrawDeviceCompat: String = "off",
    onOglDrawDeviceCompatChanged: (String) -> Unit = {},
    /**
     * 游戏兼容档（`auto` / `kirikiri2-classic` / `krkrz-gpu` / `krkrz-ogl` /
     * `aetherkiri`）。`auto` 由引擎按血脉标记判档；同样**下次开游戏**生效。
     */
    gameCompatProfile: String = "auto",
    onGameCompatProfileChanged: (String) -> Unit = {},
    /**
     * 当前有游戏在跑。引擎档位都是"下次开游戏生效"，页面上要写清这一点——用户改完
     * 发现画面没变时，得知道这不是没生效而是还没重启。
     */
    runningGame: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // 首帧从 SharedPreferences 读一次，之后以本地状态为准（写入是 apply()，异步落盘）
    var logcatCapture by remember { mutableStateOf(AppPrefs.logcatCapture(context)) }
    var overlay by remember { mutableStateOf(overlayConfig) }
    var fpsLimit by remember { mutableStateOf(AppPrefs.fpsLimit(context)) }
    var fontMode by remember { mutableStateOf(fontFallbackMode) }
    var runMode by remember { mutableStateOf(AppPrefs.runMode(context)) }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("设置") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回")
                    }
                },
            )
        },
    ) { scaffoldPadding ->
        // 宽屏（平板/横屏）下限制正文宽度并与内容居中：设置项都是"标签 + 一排单选"，
        // 拉满 1000dp 时标签与选项会隔着半个屏幕，读起来要来回扫。
        val wide = LocalConfiguration.current.screenWidthDp >= 600
        Column(
            modifier = scaffoldPadding.let {
                Modifier
                    .padding(it)
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
            },
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .then(if (wide) Modifier.widthIn(max = 640.dp) else Modifier),
            ) {
            SectionTitle("外观")

            // 主题：写进壳的偏好并**立刻**回调给 Activity 换肤（不用退出重进）。
            // 之所以必须有这一项：`themes.xml` 的 windowBackground 是黑的，若固定用
            // 浅色方案的深色文字，在某些设备/系统深浅色下就会黑字黑底看不清。
            ChoiceRow(
                title = "主题",
                subtitle = "跟随系统之外还能手动锁定浅色或深色。" +
                    "设置页/启动页的文字与图标颜色都取自当前配色，" +
                    "若觉得文字看不清或图标不见了，先在这里切一档试试。",
                choices = THEME_CHOICES,
                selected = themeMode,
                onSelected = { mode ->
                    AppPrefs.setThemeMode(context, mode)
                    onThemeModeChanged(mode)
                    AppLog.i(TAG, "theme = $mode")
                },
            )

            SectionTitle("字体")

            ChoiceRow(
                title = "字体回退策略",
                subtitle = "引擎里保留了两套字体解析实现：原版派系（单一回退字面）与" +
                    "AetherKiri 派系（把已注册字面逐个按字回退，并对齐基线）。" +
                    "文字出现黑方块/大小不一的方框就是缺字，切到另一档对比即可。" +
                    "下次开游戏生效。",
                choices = FONT_FALLBACK_CHOICES,
                selected = fontMode,
                onSelected = { mode ->
                    fontMode = mode
                    AppPrefs.setFontFallbackMode(context, mode)
                    onFontFallbackModeChanged(mode)
                    AppLog.i(TAG, "font fallback = $mode（下次开游戏生效）")
                },
            )

            SectionTitle("渲染兼容（krkrz）")

            // 两条血脉（老 KiriKiri2 / krkrz-AetherKiri）的差异收敛在这里：档位由引擎
            // 按**血脉标记**判定（只看游戏目录里有没有 krkrgles/krkrlive2d/motionplayer，
            // 不看游戏名字），再映射到下面的 OGLDrawDevice 档；手动档位仍然优先。
            // 兼容层与渲染器**合成一个旋钮**：两个独立下拉能选出一堆纸面组合，
            // 实测选出来的效果既不是 A 也不是 B（引擎里"显式选项优先"会让档位失效）。
            // 这里只给固定组合，见 RunMode。
            ChoiceRow(
                title = "运行模式",
                subtitle = "兼容层与渲染器设置的固定组合。「逐游戏自动」按目录里的插件标记判档；" +
                    "其余四条是试过的组合，按不下去就换一条试。" +
                    "**改完要重启游戏才生效**（引擎在插件注册时读一次）。",
                choices = RunMode.entries.map { it.key to it.label },
                selected = runMode.key,
                onSelected = { key ->
                    val mode = RunMode.fromKey(key)
                    runMode = mode
                    AppPrefs.setRunMode(context, mode)
                    onGameCompatProfileChanged(mode.compatProfile)
                    onOglDrawDeviceCompatChanged(mode.oglDrawDeviceCompat)
                    AppLog.i(TAG, "run mode = ${mode.key}（${mode.compatProfile}/${mode.oglDrawDeviceCompat}，需重启游戏）")
                },
            )
            Text(
                text = runMode.summary,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp),
            )

            if (runningGame) {
                Text(
                    text = "正在游戏中：引擎兼容与字体回退都是**下次启动游戏**生效，改完退出重进即可。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                )
            }

            SectionTitle("图形")

            Text(
                text = "这里改的是**默认值**；某个游戏想单独一套，去它的详情页打开" +
                    "「使用独立配置」。这些项都对应引擎已有的渲染配置，换游戏即生效。",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            GraphicsConfigEditor(
                config = graphicsConfig,
                onConfigChange = {
                    onGraphicsConfigChanged(it)
                    AppLog.i(
                        TAG,
                        "graphics: compress=${it.textureCompression.key} " +
                            "accurate=${it.accurateRender} maxTex=${it.maxTextureSize} " +
                            "mem=${it.memoryUsage.key}",
                    )
                },
            )

            SectionTitle("调试")

            SwitchRow(
                title = "加载游戏时自动显示日志",
                subtitle = "从启动到游戏出第一帧期间自动弹出运行时日志浮层，进游戏后自动关闭；" +
                    "手动关掉后本局不再弹。游戏在启动阶段就黑屏/卡住时，用它把那段日志" +
                    "直接摊在屏幕上（否则那时还没机会去点开日志）。",
                checked = autoLogOnLaunch,
                onCheckedChange = onAutoLogOnLaunchChange,
            )

            SwitchRow(
                title = "采集 logcat",
                subtitle = "把本进程的 logcat 也写到 logcat.log。" +
                    "引擎里只走 __android_log_print 的那部分日志、以及系统替我们打的崩溃墓碑" +
                    "（tombstone）只存在于 logcat，关掉就只剩引擎自己的 engine.log。",
                checked = logcatCapture,
                onCheckedChange = {
                    logcatCapture = it
                    AppPrefs.setLogcatCapture(context, it)
                    AppLog.i(TAG, "logcat capture = $it（下次启动生效）")
                },
            )

            // 旧版是三档（off/summary/detail），新版是"字段集合 + 外观"：档位永远少一项，
            // 而用户要的是"这个游戏我想看 tick，那个游戏只想看 FPS"。旧档位仍然可读
            // （AppPrefs 负责翻译），但设置页只写新配置。
            Text(
                text = "性能叠加层（全局默认）",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp),
            )
            Text(
                text = "这里改的是**默认值**；某个游戏想单独一套，去它的详情页打开" +
                    "「使用独立配置」。",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            OverlayConfigEditor(
                config = overlay,
                onConfigChange = {
                    overlay = it
                    AppPrefs.setOverlayConfig(context, it)
                    onOverlayConfigChanged(it)
                    AppLog.i(TAG, "overlay: enabled=${it.enabled} fields=${it.orderedFields.map { f -> f.key }}")
                },
            )

            Text(
                text = "自定义按键浮层（全局默认）",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(start = 16.dp, top = 16.dp),
            )
            Text(
                text = "在游戏画面上叠一组按钮，点它等于按键盘上的对应键。" +
                    "这里改的是**默认值**；某个游戏想单独一套，去它的详情页打开" +
                    "「使用独立配置」。游戏中可从悬浮菜单进「编辑自定义按键」直接拖拽定位。",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            KeyPadConfigEditor(
                profile = keyPadProfile,
                onProfileChange = {
                    onKeyPadProfileChanged(it)
                    AppLog.i(TAG, "keypad: enabled=${it.enabled} buttons=${it.buttons.size}")
                },
                templates = keyPadTemplates,
                onSaveTemplate = onSaveKeyPadTemplate,
                onDeleteTemplate = onDeleteKeyPadTemplate,
            )

            SectionTitle("输入")

            SwitchRow(
                title = "光标触控板模式",
                subtitle = "手指变成触控板：相对拖动驱动一个虚拟光标，单指轻点=左键、" +
                    "双指轻点=右键、双指上下拖=滚轮。适合需要鼠标的游戏（悬停高亮、" +
                    "右键菜单、精确点选）；普通触屏操作请保持关闭。游戏中也可从悬浮菜单切换。",
                checked = touchpadMode,
                onCheckedChange = onTouchpadModeChange,
            )
            if (touchpadMode) {
                Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                    Text(
                        text = "灵敏度：${String.format(Locale.US, "%.1fx", touchpadSensitivity)}",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    Slider(
                        value = touchpadSensitivity,
                        onValueChange = onTouchpadSensitivityChange,
                        valueRange = AppPrefs.TOUCHPAD_SENSITIVITY_RANGE,
                    )
                }
            }

            SwitchRow(
                title = "显示引擎菜单按钮",
                subtitle = "游戏画面右下角的小按钮，点开是游戏注册的窗口菜单（Windows 版标题栏" +
                    "下方那一栏：全屏、配置等）。游戏没注册菜单项时侧边栏会明确说明。",
                checked = engineMenuButton,
                onCheckedChange = onEngineMenuButtonChange,
            )

            Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
                Text("引擎帧率上限", style = MaterialTheme.typography.bodyLarge)
                Text(
                    text = "下次启动游戏时生效。不限速时由 vsync 决定节拍。",
                    style = MaterialTheme.typography.bodySmall,
                )
                FPS_OPTIONS.forEach { (value, label) ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .selectable(
                                selected = fpsLimit == value,
                                onClick = {
                                    fpsLimit = value
                                    AppPrefs.setFpsLimit(context, value)
                                    AppLog.i(TAG, "fps_limit = $value（下次启动游戏生效）")
                                },
                            )
                            .padding(vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = fpsLimit == value, onClick = null)
                        Text(text = label, modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }

            SectionTitle("日志")

            ListItem(
                headlineContent = { Text("日志目录") },
                supportingContent = {
                    SelectionContainer {
                        Text(
                            text = logDirPath,
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 3,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                },
            )
            Text(
                text = "引擎日志**按游戏分开**：每个游戏在 games/ 下有自己的一份，" +
                    "文件名为 engine-<时间戳>.log，最近几轮的现场都在那里。" +
                    "分享日志会带上最近玩过的几个游戏各自最新的一份。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
            RecentGameLogs()

            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                OutlinedButton(onClick = onShareLogs) { Text("分享日志") }
                OutlinedButton(
                    onClick = {
                        val n = LogFiles.clearAll(context)
                        AppLog.i(TAG, "清空日志：删除 $n 个文件")
                        Toast.makeText(context, "已清空 $n 个日志文件", Toast.LENGTH_SHORT).show()
                    },
                ) { Text("清空日志") }
            }

            SectionTitle("关于")

            ListItem(
                headlineContent = { Text("KrKr2-Next-Compose") },
                supportingContent = {
                    Column {
                        Text(
                            text = "版本 ${BuildInfo.appVersion(context)}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(
                            text = "包名 ${context.packageName}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(
                            text = "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(
                            text = "ABI ${Build.SUPPORTED_ABIS.joinToString()}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Text(
                            text = "${Build.MANUFACTURER} ${Build.MODEL}",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                },
            )

            Column(modifier = Modifier.padding(bottom = 24.dp)) {}
            }
        }
    }
}

/**
 * 最近玩过的游戏各自的日志落点。
 *
 * 为什么要在设置页列出来：引擎日志按游戏分开之后，"日志在哪"不再是一个固定路径，
 * 而排障时第一个问题恰恰是"这个游戏那份在哪"。这里直接列出**最近一次**的路径，
 * 用户能照着找，也能整段复制。
 */
@Composable
private fun RecentGameLogs() {
    val context = LocalContext.current
    val entries = remember { LogFiles.recentGameLogs(context, limit = 3) }
    if (entries.isEmpty()) return
    ListItem(
        headlineContent = { Text("最近玩过的游戏日志") },
        supportingContent = {
            SelectionContainer {
                Column {
                    entries.forEach { (game, log) ->
                        Text(
                            text = "${game.substringAfterLast('/')}：${log.absolutePath}",
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
        },
    )
}

private val FPS_OPTIONS = listOf(
    AppPrefs.FPS_LIMIT_UNLIMITED to "不限（跟随 vsync）",
    30 to "30 FPS",
    60 to "60 FPS",
)

@Composable
private fun SectionTitle(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.titleSmall,
        modifier = Modifier.padding(start = 16.dp, top = 20.dp, bottom = 4.dp),
    )
}
