package org.dpdns.clevebitr.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import java.util.Locale
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.OverlayCorner
import org.dpdns.clevebitr.core.OverlayField

/**
 * 叠加层自定义编辑器。
 *
 * 说明文案走行内问号 → snackbar：全局设置页与游戏详情页各自把自己的 [snackbar] 传进来。
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun OverlayConfigEditor(
    config: OverlayConfig,
    onConfigChange: (OverlayConfig) -> Unit,
    snackbar: SnackbarController,
    modifier: Modifier = Modifier,
    showPreview: Boolean = true,
) {
    Column(modifier = modifier.fillMaxWidth()) {
        SwitchRow(
            title = "显示性能叠加层",
            checked = config.enabled,
            onCheckedChange = { onConfigChange(config.copy(enabled = it)) },
            onHelpClick = {
                snackbar.showHelp("游戏画面内浮层，按 4Hz 采样；不勾选任何指标时等于关闭。")
            },
        )

        Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
            RowTitleWithHelp(
                title = "显示哪些指标",
                onHelpClick = {
                    snackbar.showHelp("顺序固定（环境 → 时间账 → 内存），勾选顺序不影响排版。")
                },
            )
            FlowRow(
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OverlayField.entries.forEach { field ->
                    FilterChip(
                        selected = field in config.fields,
                        onClick = {
                            val next = if (field in config.fields) {
                                config.fields - field
                            } else {
                                config.fields + field
                            }
                            onConfigChange(config.copy(fields = next))
                        },
                        label = { Text(field.label) },
                    )
                }
            }
        }

        SliderRow(
            title = "字号",
            valueText = String.format(Locale.US, "%.2fx", config.fontScale),
            value = config.fontScale,
            range = OverlayConfig.FONT_SCALE_RANGE,
            onValueChange = { onConfigChange(config.withFontScale(it)) },
        )

        SliderRow(
            title = "不透明度",
            valueText = String.format(Locale.US, "%.2f", config.alpha),
            value = config.alpha,
            range = OverlayConfig.ALPHA_RANGE,
            onValueChange = { onConfigChange(config.withAlpha(it)) },
        )

        Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
            Text("位置", style = MaterialTheme.typography.bodyLarge)
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
                OverlayCorner.entries.forEachIndexed { index, corner ->
                    SegmentedButton(
                        selected = config.corner == corner,
                        onClick = { onConfigChange(config.copy(corner = corner)) },
                        shape = SegmentedButtonDefaults.itemShape(
                            index = index,
                            count = OverlayCorner.entries.size,
                        ),
                    ) {
                        Text(corner.label)
                    }
                }
            }
        }

        if (showPreview) {
            Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
                RowTitleWithHelp(
                    title = "预览",
                    onHelpClick = {
                        snackbar.showHelp("用固定示例数据渲染，便于对照字号与透明度。")
                    },
                )
                // 预览底色固定为黑：游戏画面是黑的，浅色主题下预览若铺在浅色上，
                // 深色面板的观感会与真机完全不同
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(200.dp)
                        .padding(top = 8.dp)
                        .background(OverlayPreviewBackdrop, RoundedCornerShape(8.dp)),
                ) {
                    OverlayPreview(config = config, modifier = Modifier.padding(8.dp))
                }
            }
        }

        TextButton(
            onClick = { onConfigChange(OverlayConfig.default()) },
            modifier = Modifier.padding(horizontal = 8.dp),
        ) {
            Text("恢复默认")
        }
    }
}

/** 滑杆行：标题 + 当前值，值用固定 `Locale.US` 格式化。 */
@Composable
private fun SliderRow(
    title: String,
    valueText: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
        Text(
            text = "$title：$valueText",
            style = MaterialTheme.typography.bodyLarge,
        )
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
        )
    }
}

/** 面板背景色需要一个明确的深色常量：预览与游戏内都用它。 */
internal val OverlayPreviewBackdrop = Color(0xFF000000)
