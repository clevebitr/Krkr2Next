package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import org.dpdns.clevebitr.core.GraphicsConfig
import org.dpdns.clevebitr.core.MemoryUsage
import org.dpdns.clevebitr.core.TextureCompression

/**
 * 图形设置编辑器。
 *
 * 说明文案统一走行内问号 → snackbar：图形项的说明都是整段话，摊在行里会把行距撑散、
 * 一屏放不下几项。全局设置页与游戏详情页共用本编辑器，各自把自己的 [snackbar] 传进来。
 */
@Composable
fun GraphicsConfigEditor(
    config: GraphicsConfig,
    onConfigChange: (GraphicsConfig) -> Unit,
    snackbar: SnackbarController,
    modifier: Modifier = Modifier,
) {
    Column(modifier.fillMaxWidth()) {
        ChoiceRow(
            title = "纹理压缩",
            choices = TextureCompression.entries.map { it.key to "${it.label}：${it.detail}" },
            selected = config.textureCompression.key,
            onSelected = { key ->
                onConfigChange(config.copy(textureCompression = TextureCompression.fromKey(key)))
            },
            onHelpClick = {
                snackbar.showHelp(
                    "压缩后显存占用与带宽都下降，画质略有损失。设备不支持所选格式时" +
                        "引擎会自动回退到不压缩，不会报错。",
                )
            },
        )
        SwitchRow(
            title = "精确渲染",
            checked = config.accurateRender,
            onCheckedChange = { onConfigChange(config.copy(accurateRender = it)) },
            onHelpClick = {
                snackbar.showHelp(
                    "关掉「快速 GPU 路径」，改用更精确（也更慢）的合成路径。" +
                        "画面出现色块、边缘不对、半透明叠色异常时可以试试打开。",
                )
            },
        )
        ChoiceRow(
            title = "最大纹理尺寸",
            choices = GraphicsConfig.MAX_TEXTURE_SIZE_CHOICES.map { size ->
                size.toString() to if (size == 0) "不覆盖（设备上限）" else "${size}px"
            },
            selected = config.maxTextureSize.toString(),
            onSelected = { key ->
                onConfigChange(config.copy(maxTextureSize = key.toIntOrNull() ?: 0))
            },
            onHelpClick = {
                snackbar.showHelp(
                    "超过这个尺寸的纹理会被切成多块。调小省显存、但绘制次数变多；" +
                        "出现黑块或纹理缺失时调大。默认不覆盖（用设备上限）。",
                )
            },
        )
        ChoiceRow(
            title = "内存占用档",
            choices = MemoryUsage.entries.map { it.key to it.label },
            selected = config.memoryUsage.key,
            onSelected = { key ->
                onConfigChange(config.copy(memoryUsage = MemoryUsage.fromKey(key)))
            },
            onHelpClick = {
                snackbar.showHelp(
                    "告诉引擎它能用多少物理内存，影响图形缓存与归档段缓存的预算。" +
                        "设备内存紧张、或想给后台留余量时调低。",
                )
            },
        )
    }
}
