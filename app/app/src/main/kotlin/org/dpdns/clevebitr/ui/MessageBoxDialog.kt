package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import org.dpdns.clevebitr.core.MessageBoxHost

/**
 * 宿主机消息框对话框：引擎的 `System.inform` / 致命错误框 / 输入框走这里显示。
 *
 * **模态**：不允许返回键或点击外部关闭——引擎线程此刻正阻塞等这个结果，
 * 桌面的模态消息框同样不能无按钮关闭。按钮为 0 个时按单个 "OK" 处理
 * （引擎对空按钮数组的约定与桌面一致，返回下标 0）。
 */
@Composable
fun MessageBoxDialog(
    request: MessageBoxHost.Request,
    onReply: (index: Int, text: String?) -> Unit,
) {
    var input by remember(request) { mutableStateOf(request.initialInput) }
    val buttons = request.buttons.ifEmpty { listOf("OK") }

    Dialog(
        onDismissRequest = { },
        properties = DialogProperties(
            dismissOnBackPress = false,
            dismissOnClickOutside = false,
        ),
    ) {
        Surface(
            shape = MaterialTheme.shapes.large,
            color = MaterialTheme.colorScheme.surface,
            tonalElevation = 6.dp,
        ) {
            Column(
                modifier = Modifier
                    .widthIn(min = 280.dp, max = 520.dp)
                    .padding(horizontal = 24.dp, vertical = 20.dp),
            ) {
                if (request.title.isNotBlank()) {
                    Text(request.title, style = MaterialTheme.typography.titleLarge)
                }
                if (request.text.isNotBlank()) {
                    Text(
                        text = request.text,
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier
                            .padding(top = if (request.title.isNotBlank()) 12.dp else 0.dp)
                            .heightIn(max = 320.dp)
                            .verticalScroll(rememberScrollState()),
                    )
                }
                request.inputPrompt?.let { prompt ->
                    OutlinedTextField(
                        value = input,
                        onValueChange = { input = it },
                        label = { Text(prompt.ifBlank { "输入" }) },
                        modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
                    horizontalArrangement = Arrangement.End,
                ) {
                    buttons.forEachIndexed { index, label ->
                        TextButton(onClick = {
                            onReply(index, if (request.inputPrompt != null) input else null)
                        }) { Text(label) }
                    }
                }
            }
        }
    }
}
