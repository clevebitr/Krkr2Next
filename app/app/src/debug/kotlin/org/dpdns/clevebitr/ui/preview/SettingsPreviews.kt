package org.dpdns.clevebitr.ui.preview

import android.content.res.Configuration
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import org.dpdns.clevebitr.core.KeyPadProfile
import org.dpdns.clevebitr.ui.AboutScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.SettingsScreen

/**
 * 全局设置页预览（`ui/SettingsScreen.kt`）。
 *
 * 只传三个必填参数，其余全走函数默认值：默认值就是"全新安装、什么都没改"的样子，
 * 正好是排查布局时最该先看的状态。要模拟"用户改过一堆设置"，把对应参数显式传进来
 * （如 `overlayConfig = previewGlobalDefaults.overlay`），不要另起一份假数据。
 */
@Preview(name = "设置页 · 浅色", showBackground = true, widthDp = 411, heightDp = 1800)
@Preview(
    name = "设置页 · 深色",
    showBackground = true,
    widthDp = 411,
    heightDp = 1800,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Composable
private fun SettingsPreviews() {
    KrKr2NextTheme {
        SettingsScreen(
            logDirPath = PREVIEW_LOG_DIR,
            onBack = {},
            onShareLogs = {},
            keyPadProfile = KeyPadProfile.default(),
        )
    }
}

/** 关于页预览（`ui/AboutScreen.kt`）：整页只有一个返回回调，布局改动最省事的一页。 */
@Preview(name = "关于页 · 浅色", showBackground = true, widthDp = 411, heightDp = 900)
@Preview(
    name = "关于页 · 深色",
    showBackground = true,
    widthDp = 411,
    heightDp = 900,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Composable
private fun AboutPreviews() {
    KrKr2NextTheme {
        AboutScreen(onBack = {})
    }
}
