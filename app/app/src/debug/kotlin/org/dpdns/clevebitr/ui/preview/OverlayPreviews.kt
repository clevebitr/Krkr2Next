package org.dpdns.clevebitr.ui.preview

import android.content.res.Configuration
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import org.dpdns.clevebitr.core.OverlayConfig
import org.dpdns.clevebitr.core.OverlayCorner
import org.dpdns.clevebitr.core.OverlayField
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.OverlayPreview

/**
 * 性能叠加层预览（`ui/PerformanceOverlay.kt`）。
 *
 * 直接复用设置页里的 [OverlayPreview]：它拿 `PREVIEW_SNAPSHOT` 这份假采样按配置画一遍，
 * 所以预览不需要开游戏、也不需要真实会话。
 *
 * 三份预览各对应一种真实排版压力，别合并成一个：
 * - 默认档（5 个字段）——日常样子，用来对"平时看着挤不挤"；
 * - **全字段 + 2.0x + 右下角**——字段最多、字号最大、贴角方向最容易溢出；
 * - **0.6x + 左上角 + 低透明度**——最小字号与最低不透明度，检查可读性下限。
 */
@Preview(name = "叠加层 · 默认", showBackground = true, widthDp = 411, heightDp = 360)
@Composable
private fun OverlayDefaultPreview() {
    OverlayStage { OverlayPreview(config = OverlayConfig()) }
}

@Preview(
    name = "叠加层 · 深色 / 全字段 / 2.0x / 右下",
    showBackground = true,
    widthDp = 411,
    heightDp = 360,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Composable
private fun OverlayMaxPreview() {
    OverlayStage {
        OverlayPreview(
            config = OverlayConfig(
                fields = OverlayField.entries.toSet(),
                fontScale = 2f,
                alpha = 1f,
                corner = OverlayCorner.BOTTOM_END,
            ),
        )
    }
}

@Preview(name = "叠加层 · 0.6x / 左上 / 半透明", showBackground = true, widthDp = 411, heightDp = 360)
@Composable
private fun OverlayMinPreview() {
    OverlayStage {
        OverlayPreview(
            config = OverlayConfig(
                fields = OverlayField.entries.toSet(),
                fontScale = 0.6f,
                alpha = 0.2f,
                corner = OverlayCorner.TOP_START,
            ),
        )
    }
}

/**
 * 预览舞台：真机里叠加层是压在**游戏画面**上的，直接贴在白色预览背景上看不出
 * "半透明 + 浅色文字"到底能不能读，所以垫一层深色底模拟画面。
 */
@Composable
private fun OverlayStage(content: @Composable () -> Unit) {
    KrKr2NextTheme {
        Box(
            modifier = Modifier.fillMaxSize().background(Color(0xFF2B2B33)).padding(8.dp),
            contentAlignment = Alignment.Center,
        ) {
            content()
        }
    }
}
