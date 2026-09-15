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

/**
 * 设置页。目前只有调试相关的东西——这是给排障用的壳，设置项也都服务于
 * "把问题现场原样带出来"：日志怎么收、怎么导出、引擎跑多快、画面上叠什么。
 */
@Composable
fun SettingsScreen(
    logDirPath: String,
    onBack: () -> Unit,
    onShareLogs: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // 首帧从 SharedPreferences 读一次，之后以本地状态为准（写入是 apply()，异步落盘）
    var logcatCapture by remember { mutableStateOf(AppPrefs.logcatCapture(context)) }
    var showFps by remember { mutableStateOf(AppPrefs.showFps(context)) }
    var fpsLimit by remember { mutableStateOf(AppPrefs.fpsLimit(context)) }

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

            SwitchRow(
                title = "显示 FPS",
                subtitle = "在游戏画面左上角叠加帧率，用于判断卡顿是渲染慢还是逻辑慢。",
                checked = showFps,
                onCheckedChange = {
                    showFps = it
                    AppPrefs.setShowFps(context, it)
                    AppLog.i(TAG, "show fps = $it")
                },
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
