package org.dpdns.clevebitr.ui

import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.AppLog
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.core.BuildInfo
import org.dpdns.clevebitr.core.LogFiles

private const val TAG = "KrKr2Next/Settings"

/** 叠加层三档的用户可见名字；值与 AetherKiri 的 off/summary/detail 一致。 */
private val PERF_OVERLAY_CHOICES = listOf(
    "off" to "关闭",
    "summary" to "简要",
    "detail" to "详细",
)

/** 主题三档。值即 `AppPrefs.THEME_MODES`。 */
private val THEME_CHOICES = listOf(
    "system" to "跟随系统",
    "light" to "浅色",
    "dark" to "深色",
)

/**
 * 引擎字体回退策略。两种解析器实现都保留，遇到缺字（黑方块）时切到另一种对比：
 *  - 自动：按字面/字形能力自选
 *  - 原版：原版派系的单一 fallback 字面实现
 *  - 链式：AetherKiri 派系的多字面逐字回退实现
 */
private val FONT_FALLBACK_CHOICES = listOf(
    "auto" to "自动",
    "legacy" to "原版",
    "chain" to "链式",
)

/**
 * krkrz 的 OGLDrawDevice 兼容档位。krkrgles 系游戏（吉里吉里Z）会先看
 * `Window.OGLDrawDevice` 在不在，再决定要不要加载 GPU 层脚本；缺了它游戏不报错、
 * 只是静静降级：
 *  - 关闭：不提供（保持既有行为）
 *  - 别名：挂上 Window.OGLDrawDevice / Window.GLESAdaptor
 *  - 接管：再接管 KAGWindow_createDrawDevice（千恋万花实测可加载立绘/背景动态）
 */
private val OGLDRAWDEVICE_COMPAT_CHOICES = listOf(
    "off" to "关闭",
    "alias" to "别名",
    "kag" to "接管",
)

/**
 * 设置页。目前只有调试相关的东西——这是给排障用的壳，设置项也都服务于
 * "把问题现场原样带出来"：日志怎么收、怎么导出、引擎跑多快、画面上叠什么。
 */
@Composable
fun SettingsScreen(
    logDirPath: String,
    onBack: () -> Unit,
    onShareLogs: () -> Unit,
    /**
     * 叠加层档位变化时回调。壳层据此立刻切换档位——设置页现在也能在游戏里打开
     * （悬浮菜单 -> 设置），光写偏好设置要退出重进才看得到，那就等于没生效。
     */
    onPerfOverlayModeChanged: (String) -> Unit = {},
    /** 主题档位；改完立刻换肤，所以要回调给壳层（与叠加层同理）。 */
    themeMode: String = "system",
    onThemeModeChanged: (String) -> Unit = {},
    /** 引擎字体回退策略；改完**下次开游戏**生效（引擎侧只在初始化时读一次）。 */
    fontFallbackMode: String = "auto",
    onFontFallbackModeChanged: (String) -> Unit = {},
    /** krkrz 的 OGLDrawDevice 兼容档位；同样**下次开游戏**生效。 */
    oglDrawDeviceCompat: String = "off",
    onOglDrawDeviceCompatChanged: (String) -> Unit = {},
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // 首帧从 SharedPreferences 读一次，之后以本地状态为准（写入是 apply()，异步落盘）
    var logcatCapture by remember { mutableStateOf(AppPrefs.logcatCapture(context)) }
    var perfMode by remember { mutableStateOf(AppPrefs.perfOverlayMode(context)) }
    var fpsLimit by remember { mutableStateOf(AppPrefs.fpsLimit(context)) }
    var fontMode by remember { mutableStateOf(fontFallbackMode) }
    var oglCompatMode by remember { mutableStateOf(oglDrawDeviceCompat) }

    Column(modifier = modifier.fillMaxSize()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Filled.ArrowBack, contentDescription = "返回")
            }
            Text(text = "设置", style = MaterialTheme.typography.titleLarge)
        }

        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
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

            // krkrgles 系（吉里吉里Z）游戏的 Initialize.tjs 会先看 Window.OGLDrawDevice
            // 在不在，再决定要不要加载 GPU 层脚本。实测缺了它就静默跳过
            // GPULayer.tjs / GPUAffineLayer.tjs，而挂上别名后两者都会加载。
            ChoiceRow(
                title = "OGLDrawDevice 兼容",
                subtitle = "吉里吉里Z 的游戏会先看 Window.OGLDrawDevice 在不在，" +
                    "再决定要不要加载 GPU 层脚本（GPULayer.tjs / GPUAffineLayer.tjs）。" +
                    "缺了它游戏不报错、只是静静降级。" +
                    "别名档把该名字挂上；接管档再接管窗口的绘制设备工厂" +
                    "（千恋万花实测可正常加载立绘与背景动态）。" +
                    "两档逐游戏试：G2 上接管档会把主机 FBO 弄成 INCOMPLETE。" +
                    "改完下次开游戏生效。",
                choices = OGLDRAWDEVICE_COMPAT_CHOICES,
                selected = oglCompatMode,
                onSelected = { mode ->
                    oglCompatMode = mode
                    AppPrefs.setOglDrawDeviceCompat(context, mode)
                    onOglDrawDeviceCompatChanged(mode)
                    AppLog.i(TAG, "ogldrawdevice compat = $mode（下次开游戏生效）")
                },
            )

            SectionTitle("调试")

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

            // 三档而不是开关：与 AetherKiri 的 off/summary/detail 对齐（detail 只多第三行）
            Column(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            ) {
                Text("性能叠加层", style = MaterialTheme.typography.bodyLarge)
                Text(
                    text = "游戏画面左上角叠加实时性能摘要：简要档给帧率、帧时间、内存与缓存" +
                        "账目，详细档再加 tick 耗时与 1 秒窗分位数。下次启动游戏时生效。",
                    style = MaterialTheme.typography.bodySmall,
                )
                // 单选的 SegmentedButton 而不是 MD3E 的 ToggleButton：后者在稳定的
                // material3 里不存在（只有 1.5.0-alpha 有），而 alpha 版本的 AGP 门槛
                // 是 9.1.0，见 app/build.gradle.kts 的版本说明。两者观感与语义一致。
                SingleChoiceSegmentedButtonRow(
                    modifier = Modifier.padding(top = 8.dp),
                ) {
                    PERF_OVERLAY_CHOICES.forEachIndexed { index, (value, label) ->
                        SegmentedButton(
                            selected = perfMode == value,
                            onClick = {
                                perfMode = value
                                AppPrefs.setPerfOverlayMode(context, value)
                                onPerfOverlayModeChanged(value)
                                AppLog.i(TAG, "perf overlay = $value")
                            },
                            shape = SegmentedButtonDefaults.itemShape(
                                index = index,
                                count = PERF_OVERLAY_CHOICES.size,
                            ),
                        ) {
                            Text(label)
                        }
                    }
                }
            }

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

            TextButton(
                onClick = onBack,
                modifier = Modifier.padding(start = 8.dp, top = 8.dp, bottom = 24.dp),
            ) { Text("返回") }
        }
    }
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

@Composable
private fun SwitchRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = {
            Text(text = subtitle, style = MaterialTheme.typography.bodySmall)
        },
        trailingContent = { Switch(checked = checked, onCheckedChange = onCheckedChange) },
        modifier = Modifier.fillMaxWidth(),
    )
}

/**
 * 标题 + 说明 + 一排单选按钮。三档以上的枚举用它，比 SegmentedButton 好放长中文标签
 * （后者等宽分格，长标签会被挤成两行）。
 */
@Composable
private fun ChoiceRow(
    title: String,
    subtitle: String,
    choices: List<Pair<String, String>>,
    selected: String,
    onSelected: (String) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        Text(text = subtitle, style = MaterialTheme.typography.bodySmall)
        choices.forEach { (value, label) ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .selectable(
                        selected = value == selected,
                        onClick = { onSelected(value) },
                    )
                    .padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RadioButton(selected = value == selected, onClick = { onSelected(value) })
                Text(text = label, modifier = Modifier.padding(start = 8.dp))
            }
        }
    }
}
