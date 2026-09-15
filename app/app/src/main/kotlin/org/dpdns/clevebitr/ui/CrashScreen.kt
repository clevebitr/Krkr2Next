package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp

/**
 * 崩溃/异常退出界面。
 *
 * 两个入口共用：崩溃时由 `CrashActivity`（`:crash` 进程）即时展示，以及下次启动
 * 发现上次是原生崩溃时由 `MainActivity` 弹出来回放。
 *
 * 只读传入的字符串，**不依赖任何进程内状态**——它经常运行在一个刚起来的独立进程里。
 */
@Composable
fun CrashScreen(
    title: String,
    summary: String,
    detail: String?,
    logDirPath: String,
    onRestart: () -> Unit,
    onShareLogs: (() -> Unit)?,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(text = title, style = MaterialTheme.typography.headlineSmall)
        Text(
            text = summary,
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(top = 12.dp),
        )

        if (!detail.isNullOrBlank()) {
            Text(
                text = detail,
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier
                    .padding(top = 16.dp)
                    .fillMaxWidth()
                    .weight(1f, fill = false)
                    .verticalScroll(rememberScrollState()),
            )
        }

        Text(
            text = "日志目录：$logDirPath",
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = 16.dp),
        )
        Text(
            text = "把这里的文件拷出来即可定位问题；也可以直接用「分享日志」。",
            style = MaterialTheme.typography.bodySmall,
        )

        Row(
            modifier = Modifier.padding(top = 24.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Button(onClick = onRestart) { Text("重新启动") }
            if (onShareLogs != null) {
                TextButton(onClick = onShareLogs) { Text("分享日志") }
            }
            TextButton(onClick = onExit) { Text("退出") }
        }
    }
}
