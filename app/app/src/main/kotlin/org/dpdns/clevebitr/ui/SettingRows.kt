package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.HelpOutline
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/**
 * 设置页与游戏详情页共用的几行控件。
 */

/**
 * 单选行。[choices] 是 `值 → 显示文案`，[selected] 用值比较。
 *
 * 值用字符串而不是枚举：设置项的值要落进 `SharedPreferences` 与 `krkr2next.json`，
 * 存字符串比存 ordinal 稳（枚举插项不会让旧配置串位）。
 *
 * 说明文案一律走 [onHelpClick] 的问号 → snackbar（见 [SnackbarController.showHelp]）；
 * [subtitle] 只留给拿不到 snackbar 宿主的场景（如游戏内浮层的属性面板）。
 */
@Composable
fun ChoiceRow(
    title: String,
    subtitle: String? = null,
    choices: List<Pair<String, String>>,
    selected: String,
    onSelected: (String) -> Unit,
    modifier: Modifier = Modifier,
    onHelpClick: (() -> Unit)? = null,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (onHelpClick != null) {
                HelpIconButton(onClick = onHelpClick)
            }
        }

        subtitle?.let { Text(text = it, style = MaterialTheme.typography.bodySmall) }

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
                RadioButton(
                    selected = value == selected,
                    onClick = { onSelected(value) },
                )
                Text(
                    text = label,
                    modifier = Modifier.padding(start = 8.dp),
                )
            }
        }
    }
}

/** 开关行：标题 + 说明 + 右侧开关。说明同 [ChoiceRow]，走问号 → snackbar；[subtitle] 只给没有 snackbar 宿主的场景。 */
@Composable
fun SwitchRow(
    title: String,
    subtitle: String? = null,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    onHelpClick: (() -> Unit)? = null,
) {
    Row(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.bodyLarge)
                if (onHelpClick != null) {
                    HelpIconButton(onClick = onHelpClick)
                }
            }
            subtitle?.let { Text(text = it, style = MaterialTheme.typography.bodySmall) }
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

class SnackbarController(
    val hostState: SnackbarHostState,
    private val scope: CoroutineScope,
) {
    fun show(
        message: String,
        actionLabel: String? = null,
        withDismissAction: Boolean = false,
        duration: SnackbarDuration = SnackbarDuration.Short,
        onResult: ((SnackbarResult) -> Unit)? = null,
    ) {
        scope.launch {
            val result = hostState.showSnackbar(
                message = message,
                actionLabel = actionLabel,
                withDismissAction = withDismissAction,
                duration = duration,
            )
            onResult?.invoke(result)
        }
    }

    /**
     * 行说明走问号时的统一呈现：这些说明都是整段话，比 [SnackbarDuration.Short] 长，
     * 所以用 Long 并带显式关闭按钮，免得用户还没读完就消失。
     */
    fun showHelp(message: String) {
        show(message = message, withDismissAction = true, duration = SnackbarDuration.Long)
    }
}

@Composable
fun rememberSnackbarController(): SnackbarController {
    val hostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    return remember(hostState, scope) {
        SnackbarController(hostState, scope)
    }
}

@Composable
fun SnackbarPost(
    controller: SnackbarController,
    modifier: Modifier = Modifier,
) {
    SnackbarHost(
        hostState = controller.hostState,
        modifier = modifier,
    ) { data ->
        Snackbar(
            snackbarData = data,
            containerColor = MaterialTheme.colorScheme.inverseSurface,
            contentColor = MaterialTheme.colorScheme.inverseOnSurface,
            actionContentColor = MaterialTheme.colorScheme.inversePrimary,
            dismissActionContentColor = MaterialTheme.colorScheme.inverseOnSurface,
            shape = MaterialTheme.shapes.medium,
        )
    }
}

/** 行说明的问号：所有说明文案都挂在它上面。 */
@Composable
internal fun HelpIconButton(onClick: () -> Unit) {
    IconButton(
        onClick = onClick,
        modifier = Modifier.size(24.dp),
    ) {
        Icon(
            imageVector = Icons.AutoMirrored.Filled.HelpOutline,
            contentDescription = "帮助",
            modifier = Modifier.size(16.dp),
        )
    }
}

/**
 * 非行控件的区块标题（如「显示哪些指标」「引擎帧率上限」）+ 同一个问号。
 * [onHelpClick] 为 null 时不排问号；[style] 让编辑器内的子标题保持 titleSmall。
 */
@Composable
fun RowTitleWithHelp(
    title: String,
    onHelpClick: (() -> Unit)? = null,
    modifier: Modifier = Modifier,
    style: TextStyle = MaterialTheme.typography.bodyLarge,
) {
    Row(modifier = modifier, verticalAlignment = Alignment.CenterVertically) {
        Text(title, style = style)
        if (onHelpClick != null) {
            HelpIconButton(onClick = onHelpClick)
        }
    }
}