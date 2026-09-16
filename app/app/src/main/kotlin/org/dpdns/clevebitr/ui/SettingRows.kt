package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/**
 * 设置页与游戏详情页共用的几行控件。
 *
 * 抽出来是因为**两处都要用同一套语义**：单选行在"全局默认"和"此游戏覆盖"里
 * 长得一样、行为一样，用户才不会以为它们是两件事。
 */

/**
 * 单选行。[choices] 是 `值 → 显示文案`，[selected] 用值比较。
 *
 * 值用字符串而不是枚举：设置项的值要落进 `SharedPreferences` 与 `krkr2next.json`，
 * 存字符串比存 ordinal 稳（枚举插项不会让旧配置串位）。
 */
@Composable
fun ChoiceRow(
    title: String,
    subtitle: String,
    choices: List<Pair<String, String>>,
    selected: String,
    onSelected: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
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

/** 开关行：标题 + 说明 + 右侧开关。说明必填，避免出现"这个开关到底管什么"的界面。 */
@Composable
fun SwitchRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(text = subtitle, style = MaterialTheme.typography.bodySmall)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
