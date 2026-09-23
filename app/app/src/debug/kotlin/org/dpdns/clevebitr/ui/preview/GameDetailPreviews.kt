package org.dpdns.clevebitr.ui.preview

import android.content.res.Configuration
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import java.io.File
import org.dpdns.clevebitr.ui.GameDetailScreen
import org.dpdns.clevebitr.ui.KrKr2NextTheme

/**
 * 游戏详情页预览（`ui/GameDetailScreen.kt`）。
 *
 * 三份预览对应三种真实排版压力：
 * - **窄屏 411dp**：封面占 132dp，主按钮全宽横排能否放得下"图标 + 文字"；
 * - **深色**：`surfaceContainerLow` / `onSurfaceVariant` 这类不显眼的对比度问题只在深色露头；
 * - **平板 800dp**：`contentWidth` 限制（正文最宽 560dp）与封面 240dp 的分支。
 *
 * 换一条假数据看别的分支时，改 [previewGame] 就行，别在这里就地造数据。
 */
@Preview(name = "详情页 · 浅色 411dp", showBackground = true, widthDp = 411, heightDp = 900)
@Preview(
    name = "详情页 · 深色 411dp",
    showBackground = true,
    widthDp = 411,
    heightDp = 900,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Preview(name = "详情页 · 平板 800dp", showBackground = true, widthDp = 800, heightDp = 900)
@Composable
private fun GameDetailPreviews() {
    KrKr2NextTheme {
        GameDetailScreen(
            game = previewGame,
            coversDir = File(PREVIEW_COVERS_DIR),
            config = previewConfig,
            configInGameDir = true,
            globalDefaults = previewGlobalDefaults,
            onSave = { _, _ -> },
            onLaunch = {},
            onOpenSettings = {},
            onScrape = {},
            onToggleFavorite = {},
            onEditGroup = {},
            onRemove = {},
            onBack = {},
        )
    }
}
