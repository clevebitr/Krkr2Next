package org.dpdns.clevebitr.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.util.Locale
import java.util.UUID
import org.dpdns.clevebitr.core.KeyButton
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.core.VkCodes

/**
 * 自定义按键浮层的属性编辑器。全局默认、每游戏覆盖、模板应用共用同一个组件——
 * 三处的取值范围、颜色表、键位表必须完全一致。
 *
 * 编辑器只改 [KeyPadProfile] 数据，**不自己落盘**：落盘由调用方决定（全局写
 * `AppPrefs`，每游戏写 `krkr2next.json`）。这样"编辑中"与"已保存"不会互相打架。
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun KeyPadConfigEditor(
    profile: KeyPadProfile,
    onProfileChange: (KeyPadProfile) -> Unit,
    modifier: Modifier = Modifier,
    templates: Map<String, KeyPadProfile> = emptyMap(),
    onSaveTemplate: ((String, KeyPadProfile) -> Unit)? = null,
    onDeleteTemplate: ((String) -> Unit)? = null,
    /** 初次展示时选中哪个按钮（游戏内属性面板会传入浮层里已选的那个）。 */
    initialSelectedId: String? = null,
) {
    var selectedId by remember {
        mutableStateOf(
            initialSelectedId?.takeIf { id -> profile.buttons.any { it.id == id } }
                ?: profile.buttons.firstOrNull()?.id,
        )
    }
    var templateDialog by remember { mutableStateOf(false) }

    // 选中的按钮被删掉（或列表被替换）后，选中态回退到第一个，不写回状态。
    val fallbackId = profile.buttons.firstOrNull()?.id
    val effectiveId = selectedId?.takeIf { id -> profile.buttons.any { it.id == id } } ?: fallbackId

    Column(modifier = modifier.fillMaxWidth()) {
        SwitchRow(
            title = "显示自定义按键浮层",
            subtitle = "在游戏画面上叠一组按钮，点它等于按键盘上的对应键。" +
                "按钮以外的触摸照常传给游戏。",
            checked = profile.enabled,
            onCheckedChange = { onProfileChange(profile.copy(enabled = it)) },
        )

        if (profile.buttons.isEmpty()) {
            Text(
                text = "还没有按钮。点下面「恢复默认布局」拿一组方向键 + 确认/返回，或在游戏内编辑态里添加。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        } else {
            Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                Text("按钮", style = MaterialTheme.typography.bodyLarge)
                ButtonChips(
                    buttons = profile.buttons,
                    selectedId = effectiveId,
                    onSelect = { selectedId = it },
                    onAdd = {
                        val added = KeyButton(
                            id = UUID.randomUUID().toString(),
                            vk = VkCodes.RETURN,
                            label = "键",
                            x = 0.42f,
                            y = 0.42f,
                            w = 0.12f,
                            h = 0.14f,
                        )
                        onProfileChange(profile.withButton(added))
                        selectedId = added.id
                    },
                    onDelete = {
                        val id = effectiveId ?: return@ButtonChips
                        onProfileChange(profile.withoutButton(id))
                        selectedId = null
                    },
                    canDelete = effectiveId != null,
                )
            }
        }

        val current = profile.buttons.firstOrNull { it.id == effectiveId }
        if (current != null) {
            ButtonPropertiesEditor(
                button = current,
                onButtonChange = { onProfileChange(profile.withButton(it)) },
            )
        }

        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            TextButton(onClick = { onProfileChange(KeyPadProfile.starter()) }) {
                Text("恢复默认布局")
            }
            if (onSaveTemplate != null) {
                TextButton(onClick = { templateDialog = true }) { Text("存为模板") }
            }
            TextButton(onClick = { onProfileChange(KeyPadProfile.default()) }) { Text("清空") }
        }

        if (templates.isNotEmpty()) {
            TemplateList(
                templates = templates,
                onApply = { name, template ->
                    // 应用模板时重新分配 id：同一个模板套进不同游戏（或同一游戏两次）
                    // 时按钮 id 不能冲突。
                    onProfileChange(
                        profile.copy(
                            enabled = true,
                            buttons = template.buttons.map { it.copy(id = UUID.randomUUID().toString()) },
                        ),
                    )
                },
                onDelete = onDeleteTemplate,
            )
        }
    }

    if (templateDialog && onSaveTemplate != null) {
        SaveTemplateDialog(
            onDismiss = { templateDialog = false },
            onConfirm = { name ->
                onSaveTemplate(name, profile)
                templateDialog = false
            },
        )
    }
}

/** 按钮列表：选中的高亮，末尾是添加/删除。横向流式排列，窄屏也能换行。 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun ButtonChips(
    buttons: List<KeyButton>,
    selectedId: String?,
    onSelect: (String) -> Unit,
    onAdd: () -> Unit,
    onDelete: () -> Unit,
    canDelete: Boolean,
) {
    FlowRow(
        modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        buttons.forEach { button ->
            FilterChip(
                selected = button.id == selectedId,
                onClick = { onSelect(button.id) },
                label = {
                    Text(
                        text = button.label.ifBlank { vkLabel(button.vk) },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                },
            )
        }
        FilterChip(selected = false, onClick = onAdd, label = { Text("＋ 添加") })
        if (canDelete) {
            FilterChip(selected = false, onClick = onDelete, label = { Text("删除") })
        }
    }
}

/** 单个按钮的全部可配属性。 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun ButtonPropertiesEditor(
    button: KeyButton,
    onButtonChange: (KeyButton) -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text("按键", style = MaterialTheme.typography.titleSmall)
        Text(
            text = "对应键盘上的哪个键（Windows VK 码，游戏脚本判断的就是它）。",
            style = MaterialTheme.typography.bodySmall,
        )
        FlowRow(
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            VK_CHOICES.forEach { (vk, label) ->
                FilterChip(
                    selected = button.vk == vk,
                    onClick = { onButtonChange(button.copy(vk = vk)) },
                    label = { Text(label) },
                )
            }
        }

        OutlinedTextField(
            value = button.label,
            onValueChange = { onButtonChange(button.copy(label = it)) },
            label = { Text("按钮文字（可留空）") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
        )

        Text("图标", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
        FlowRow(
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            KeyPadIcons.selectableKeys.forEach { keyName ->
                val icon = KeyPadIcons.resolve(keyName)
                val selected = button.iconKey == keyName
                Box(
                    modifier = Modifier
                        .size(40.dp)
                        .background(
                            if (selected) MaterialTheme.colorScheme.primaryContainer
                            else MaterialTheme.colorScheme.surfaceVariant,
                            CircleShape,
                        )
                        .border(
                            width = 2.dp,
                            color = if (selected) MaterialTheme.colorScheme.primary else Color.Transparent,
                            shape = CircleShape,
                        )
                        .pointerInputSelect(keyName) { onButtonChange(button.copy(iconKey = keyName)) },
                    contentAlignment = Alignment.Center,
                ) {
                    if (icon == null) {
                        Text("无", style = MaterialTheme.typography.labelSmall)
                    } else {
                        Icon(icon, contentDescription = null, modifier = Modifier.size(20.dp))
                    }
                }
            }
        }

        ColorRow(
            title = "文字颜色",
            selected = button.textColor,
            onSelect = { onButtonChange(button.copy(textColor = it)) },
        )
        ColorRow(
            title = "背景颜色",
            selected = button.bgColor,
            onSelect = { onButtonChange(button.copy(bgColor = it)) },
        )
        ColorRow(
            title = "描边颜色",
            selected = button.strokeColor,
            onSelect = { onButtonChange(button.copy(strokeColor = it)) },
        )

        LabeledSlider(
            title = "文字大小",
            valueText = String.format(Locale.US, "%.0f sp", button.textSizeSp),
            value = button.textSizeSp,
            range = KeyButton.MIN_TEXT_SP..KeyButton.MAX_TEXT_SP,
            onChange = { onButtonChange(button.copy(textSizeSp = it)) },
        )
        LabeledSlider(
            title = "不透明度",
            valueText = String.format(Locale.US, "%.2f", button.alpha),
            value = button.alpha,
            range = KeyButton.MIN_ALPHA..1f,
            onChange = { onButtonChange(button.copy(alpha = it)) },
        )
        LabeledSlider(
            title = "描边粗细",
            valueText = String.format(Locale.US, "%.1f dp", button.strokeWidthDp),
            value = button.strokeWidthDp,
            range = 0f..KeyButton.MAX_STROKE_DP,
            onChange = { onButtonChange(button.copy(strokeWidthDp = it)) },
        )

        Text(
            text = "位置与大小（也可在游戏内编辑态直接拖拽/缩放）",
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(top = 8.dp),
        )
        LabeledSlider(
            title = "横向位置",
            valueText = pct(button.x),
            value = button.x,
            range = 0f..1f,
            onChange = { onButtonChange(button.copy(x = it).sanitized()) },
        )
        LabeledSlider(
            title = "纵向位置",
            valueText = pct(button.y),
            value = button.y,
            range = 0f..1f,
            onChange = { onButtonChange(button.copy(y = it).sanitized()) },
        )
        LabeledSlider(
            title = "宽度",
            valueText = pct(button.w),
            value = button.w,
            range = KeyButton.MIN_SIZE..1f,
            onChange = { onButtonChange(button.copy(w = it).sanitized()) },
        )
        LabeledSlider(
            title = "高度",
            valueText = pct(button.h),
            value = button.h,
            range = KeyButton.MIN_SIZE..1f,
            onChange = { onButtonChange(button.copy(h = it).sanitized()) },
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ColorRow(
    title: String,
    selected: Long,
    onSelect: (Long) -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
        Text(title, style = MaterialTheme.typography.titleSmall)
        FlowRow(
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            KEYPAD_COLORS.forEach { value ->
                val isSelected = selected == value
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .background(Color(value), CircleShape)
                        .border(
                            width = 2.dp,
                            color = if (isSelected) MaterialTheme.colorScheme.primary else Color(0x33000000),
                            shape = CircleShape,
                        )
                        .pointerInputSelect(value) { onSelect(value) },
                    contentAlignment = Alignment.Center,
                ) {
                    if (isSelected) {
                        Text(
                            text = "✓",
                            style = MaterialTheme.typography.labelSmall,
                            color = if (isLightColor(value)) Color(0xFF000000) else Color(0xFFFFFFFF),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun LabeledSlider(
    title: String,
    valueText: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text("$title：$valueText", style = MaterialTheme.typography.bodyMedium)
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}

@Composable
private fun TemplateList(
    templates: Map<String, KeyPadProfile>,
    onApply: (String, KeyPadProfile) -> Unit,
    onDelete: ((String) -> Unit)?,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
        Text("模板", style = MaterialTheme.typography.bodyLarge)
        Text(
            text = "把当前布局存成模板，别的游戏可以一键套用（按钮 id 会重新分配）。",
            style = MaterialTheme.typography.bodySmall,
        )
        templates.forEach { (name, template) ->
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "$name（${template.buttons.size} 个按钮）",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.weight(1f),
                )
                TextButton(onClick = { onApply(name, template) }) { Text("套用") }
                if (onDelete != null) {
                    IconButton(onClick = { onDelete(name) }) {
                        Icon(Icons.Filled.Delete, contentDescription = "删除模板 $name")
                    }
                }
            }
        }
    }
}

@Composable
private fun SaveTemplateDialog(
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("存为模板") },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("模板名称") },
                singleLine = true,
            )
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(name.trim()) },
                enabled = name.isNotBlank(),
            ) { Text("保存") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("取消") } },
    )
}

/** 无涟漪的可点击装饰器：颜色块/图标格用它，避免引入 `clickable` 的语义与默认最小尺寸。 */
private fun Modifier.pointerInputSelect(key: Any?, onClick: () -> Unit): Modifier =
    this.pointerInput(key) { detectTapGestures { onClick() } }

/** 颜色亮度判定：亮色底上用深色对勾，否则看不清。 */
private fun isLightColor(argb: Long): Boolean {
    val r = ((argb shr 16) and 0xFF).toInt()
    val g = ((argb shr 8) and 0xFF).toInt()
    val b = (argb and 0xFF).toInt()
    return (r * 299 + g * 587 + b * 114) / 1000 >= 160
}

private fun pct(value: Float): String = String.format(Locale.US, "%.0f%%", value * 100)

internal fun vkLabel(vk: Int): String =
    VK_CHOICES.firstOrNull { it.first == vk }?.second ?: "0x${vk.toString(16)}"

/** 常用键位表。含方向键、编辑键、功能键、字母与数字。 */
internal val VK_CHOICES: List<Pair<Int, String>> = listOf(
    VkCodes.LEFT to "←",
    VkCodes.UP to "↑",
    VkCodes.RIGHT to "→",
    VkCodes.DOWN to "↓",
    VkCodes.RETURN to "Enter",
    VkCodes.SPACE to "Space",
    VkCodes.ESCAPE to "Esc",
    VkCodes.TAB to "Tab",
    VkCodes.BACK to "Backspace",
    VkCodes.DELETE to "Delete",
    VkCodes.INSERT to "Insert",
    VkCodes.PRIOR to "PageUp",
    VkCodes.NEXT to "PageDown",
    VkCodes.HOME to "Home",
    VkCodes.END to "End",
    VkCodes.SHIFT to "Shift",
    VkCodes.CONTROL to "Ctrl",
    VkCodes.MENU to "Alt",
    VkCodes.CAPITAL to "CapsLock",
    VkCodes.PAUSE to "Pause",
) + ('A'..'Z').map { letter -> (VkCodes.letter(letter) ?: 0) to letter.toString() }
    .filter { it.first != 0 } +
    ('0'..'9').map { digit -> (VkCodes.digit(digit) ?: 0) to digit.toString() }
        .filter { it.first != 0 } +
    (0..23).map { (VkCodes.F1 + it) to "F${it + 1}" }

/**
 * 颜色选择表。都是常见的"深底浅字/浅底深字"组合，够覆盖浮层需求；
 * 不做任意取色器，避免为了配色再引一个依赖（AGENTS §2）。
 */
internal val KEYPAD_COLORS: List<Long> = listOf(
    0xFFFFFFFFL,
    0xFF000000L,
    0xFFFFC107L,
    0xFF4CAF50L,
    0xFF2196F3L,
    0xFFF44336L,
    0xFF9C27B0L,
    0x66000000L,
    0x33000000L,
    0x88FFFFFFL,
)
