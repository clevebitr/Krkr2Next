package org.dpdns.clevebitr.ui.preview

import android.content.res.Configuration
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import java.io.File
import org.dpdns.clevebitr.core.AppPrefs
import org.dpdns.clevebitr.ui.KrKr2NextTheme
import org.dpdns.clevebitr.ui.LibraryScreen

/**
 * 游戏库页预览（`ui/LibraryScreen.kt`）。
 *
 * 用 [previewGames] 里的三条数据，故意覆盖"超长标题 / 无封面 / 未分组 / 收藏"四种边界：
 * 库页卡片是整页最容易出现截断与错位的地方。
 *
 * 排序键取自 [AppPrefs.LIBRARY_SORTS]，不写死字符串——排序项改名时预览不会悄悄失效。
 */
@Preview(name = "库页 · 浅色 411dp", showBackground = true, widthDp = 411, heightDp = 900)
@Preview(
    name = "库页 · 深色 411dp",
    showBackground = true,
    widthDp = 411,
    heightDp = 900,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
@Preview(name = "库页 · 平板 800dp", showBackground = true, widthDp = 800, heightDp = 900)
@Composable
private fun LibraryPreviews() {
    KrKr2NextTheme {
        LibraryScreen(
            games = previewGames,
            coversDir = File(PREVIEW_COVERS_DIR),
            sort = AppPrefs.LIBRARY_SORTS.first(),
            onSortChange = {},
            onLaunch = {},
            onOpenDetail = {},
            onScrape = {},
            onRemove = {},
            onToggleFavorite = {},
            onEditGroup = {},
            onAddGame = {},
        )
    }
}

/** 空库状态：`EmptyLibrary` 分支（引导"添加游戏"），只有这一条路径会露出来。 */
@Preview(name = "库页 · 空", showBackground = true, widthDp = 411, heightDp = 900)
@Composable
private fun LibraryEmptyPreview() {
    KrKr2NextTheme {
        LibraryScreen(
            games = emptyList(),
            coversDir = File(PREVIEW_COVERS_DIR),
            sort = AppPrefs.LIBRARY_SORTS.first(),
            onSortChange = {},
            onLaunch = {},
            onOpenDetail = {},
            onScrape = {},
            onRemove = {},
            onToggleFavorite = {},
            onEditGroup = {},
            onAddGame = {},
        )
    }
}
