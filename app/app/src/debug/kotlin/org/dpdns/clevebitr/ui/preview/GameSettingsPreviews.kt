package org.dpdns.clevebitr.ui.preview

import android.content.res.Configuration
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import org.dpdns.clevebitr.ui.GameSettingsScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme

/**
 * 单游戏设置页预览（`ui/GameSettingsScreen.kt`）。
 *
 * [previewConfig] 里运行模式/帧率/叠加层都是"独立配置"，所以预览默认落在**覆盖态**；
 * 想看"继承全局"的文案，把 [previewConfig] 里的 `engine` 换成 `EngineOverride()`
 * （空覆盖 = 全继承）即可，别在这个文件里造第二份配置。
 *
 * 页面较长（引擎 / 叠加层 / 按键 / 触控板 / 图形 / 日志），`heightDp` 给大一点，
 * 一次能看到更多分组，省得每次预览都滚动。
 */
@Preview(name = "游戏设置 · 浅色", showBackground = true, widthDp = 411, heightDp = 1600)
@Preview(
    name = "游戏设置 · 深色",
    showBackground = true,
    widthDp = 411,
    heightDp = 1600,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Composable
private fun GameSettingsPreviews() {
    KrKr2NextTheme {
        GameSettingsScreen(
            game = previewGame,
            config = previewConfig,
            globalDefaults = previewGlobalDefaults,
            onSave = {},
            onBack = {},
        )
    }
}
