package org.dpdns.clevebitr.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import org.dpdns.clevebitr.core.GraphicsConfig
import org.dpdns.clevebitr.core.MemoryUsage
import org.dpdns.clevebitr.core.TextureCompression

/**
 * 图形设置编辑器（全局默认页与游戏设置页共用）。
 *
 * 每一项都直接对应一个**引擎已有的配置键**（见 `GraphicsConfig` 的说明），壳只负责
 * 把值下发，不重新实现渲染逻辑 —— 所以这里没有"预览"，改动通过"换游戏/重开本局"
 * 生效（渲染器的惰性缓存在 `engine_set_option` 里被显式失效）。
 */
@Composable
fun GraphicsConfigEditor(
    config: GraphicsConfig,
    onConfigChange: (GraphicsConfig) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.fillMaxWidth()) {
        ChoiceRow(
            title = "纹理压缩",
            subtitle = "压缩后显存占用与带宽都下降，画质略有损失。设备不支持所选格式时" +
                "引擎会自动回退到不压缩，不会报错。",
            choices = TextureCompression.entries.map { it.key to "${it.label}：${it.detail}" },
            selected = config.textureCompression.key,
            onSelected = { key ->
                onConfigChange(config.copy(textureCompression = TextureCompression.fromKey(key)))
            },
        )
        SwitchRow(
            title = "精确渲染",
            subtitle = "关掉「快速 GPU 路径」，改用更精确（也更慢）的合成路径。" +
                "画面出现色块、边缘不对、半透明叠色异常时可以试试打开。",
            checked = config.accurateRender,
            onCheckedChange = { onConfigChange(config.copy(accurateRender = it)) },
        )
        ChoiceRow(
            title = "最大纹理尺寸",
            subtitle = "超过这个尺寸的纹理会被切成多块。调小省显存、但绘制次数变多；" +
                "出现黑块或纹理缺失时调大。默认不覆盖（用设备上限）。",
            choices = GraphicsConfig.MAX_TEXTURE_SIZE_CHOICES.map { size ->
                size.toString() to if (size == 0) "不覆盖（设备上限）" else "${size}px"
            },
            selected = config.maxTextureSize.toString(),
            onSelected = { key ->
                onConfigChange(config.copy(maxTextureSize = key.toIntOrNull() ?: 0))
            },
        )
        ChoiceRow(
            title = "内存占用档",
            subtitle = "告诉引擎它能用多少物理内存，影响图形缓存与归档段缓存的预算。" +
                "设备内存紧张、或想给后台留余量时调低。",
            choices = MemoryUsage.entries.map { it.key to it.label },
            selected = config.memoryUsage.key,
            onSelected = { key ->
                onConfigChange(config.copy(memoryUsage = MemoryUsage.fromKey(key)))
            },
        )
    }
}
