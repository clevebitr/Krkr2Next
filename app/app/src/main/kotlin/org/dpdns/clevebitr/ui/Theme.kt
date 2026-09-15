package org.dpdns.clevebitr.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/**
 * 标准 Material 3 主题。
 *
 * ## 为什么不是 Material 3 Expressive
 *
 * MD3E 的 `MaterialExpressiveTheme` / `MotionScheme` 只在 material3 1.5.0-alpha 里是
 * public（稳定版 1.4.0 里它是 `internal fun`，调不到），而 **alpha19 起把
 * `minAndroidGradlePluginVersion` 抬到 9.1.0、`minCompileSdk` 抬到 37**——compose
 * 自己在 1.12.0-alpha02 也是同一条线。换言之"要 MD3E"等于把 AGP 从 8.13.2 连跳大版本、
 * 连带 Gradle 9 与 JDK 一起动，而本应用实际只用到 `MaterialExpressiveTheme`、
 * `MotionScheme.expressive()`、`ToggleButton` 三个 API。为一个观感换整套工具链不划算，
 * 所以退回稳定版：`MaterialTheme` + 下面这套配色，观感差异只是弹性动效与更大圆角。
 *
 * 将来若要重新上 MD3E：把 material3 显式指到 1.5.0-alpha18（最后一个 minAGP 8.6.0 的
 * 版本），并把此处换回 `MaterialExpressiveTheme`；再往上就得先升 AGP。
 *
 * ## 配色为什么显式写
 *
 * 不依赖 `ColorScheme.fromSeed`（它是 experimental，且不同版本的默认色板会漂），而是显式
 * 给出明暗两套角色：色相沿用应用原有的靛蓝强调色，并补齐 Material 3 组件会取用的容器色阶
 * （`*Container` / `surfaceContainer*` / `surfaceDim|Bright` / `outlineVariant`），否则
 * 默认紫色调会漏进一部分组件，两批组件不像同一套设计语言。
 *
 * 默认深色：VN 多数时间在看画面，深色更合适，也与 PocketKrKr 的默认一致。
 */
private val Accent = Color(0xFF6C7BFF)

private val DarkScheme: ColorScheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color(0xFF0A0C24),
    primaryContainer = Color(0xFF2C3272),
    onPrimaryContainer = Color(0xFFDDE1FF),
    inversePrimary = Color(0xFF4452B8),
    secondary = Color(0xFFB9C3FF),
    onSecondary = Color(0xFF1E2450),
    secondaryContainer = Color(0xFF333A6B),
    onSecondaryContainer = Color(0xFFDDE1FF),
    tertiary = Color(0xFFFFB0C8),
    onTertiary = Color(0xFF5B1133),
    tertiaryContainer = Color(0xFF7A2949),
    onTertiaryContainer = Color(0xFFFFD9E2),
    background = Color(0xFF121318),
    onBackground = Color(0xFFE3E1E9),
    surface = Color(0xFF121318),
    onSurface = Color(0xFFE3E1E9),
    surfaceVariant = Color(0xFF45464F),
    onSurfaceVariant = Color(0xFFC6C5D0),
    surfaceDim = Color(0xFF121318),
    surfaceBright = Color(0xFF38393F),
    surfaceContainerLowest = Color(0xFF0D0E13),
    surfaceContainerLow = Color(0xFF1A1B21),
    surfaceContainer = Color(0xFF1E1F25),
    surfaceContainerHigh = Color(0xFF292A30),
    surfaceContainerHighest = Color(0xFF34353B),
    outline = Color(0xFF90909A),
    outlineVariant = Color(0xFF45464F),
)

private val LightScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFF4452B8),
    onPrimary = Color.White,
    primaryContainer = Color(0xFFDDE1FF),
    onPrimaryContainer = Color(0xFF00105C),
    secondary = Color(0xFF5A5D8E),
    onSecondary = Color.White,
    secondaryContainer = Color(0xFFDFE0FF),
    onSecondaryContainer = Color(0xFF161A49),
    tertiary = Color(0xFF964061),
    onTertiary = Color.White,
    tertiaryContainer = Color(0xFFFFD9E2),
    onTertiaryContainer = Color(0xFF3E0021),
    background = Color(0xFFFBF8FF),
    onBackground = Color(0xFF1B1B21),
    surface = Color(0xFFFBF8FF),
    onSurface = Color(0xFF1B1B21),
    surfaceVariant = Color(0xFFE2E1EC),
    onSurfaceVariant = Color(0xFF45464F),
    surfaceDim = Color(0xFFDBD9E0),
    surfaceBright = Color(0xFFFBF8FF),
    surfaceContainerLowest = Color.White,
    surfaceContainerLow = Color(0xFFF5F2FA),
    surfaceContainer = Color(0xFFEFEDF4),
    surfaceContainerHigh = Color(0xFFE9E7EF),
    surfaceContainerHighest = Color(0xFFE3E1E9),
    outline = Color(0xFF767680),
    outlineVariant = Color(0xFFC6C5D0),
)

@Composable
fun KrKr2NextTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) DarkScheme else LightScheme,
        content = content,
    )
}

/**
 * 把设置里的主题档位（`AppPrefs.THEME_MODES`）解析成"用不用深色"。
 *
 * 单独抽出来是因为调用点（`MainActivity`）需要**同一个值**做两件事：包主题，以及
 * 决定窗口装饰（状态栏/导航栏图标明暗）。两处各算一次必然漂。
 */
@Composable
fun resolveDarkTheme(mode: String): Boolean = when (mode) {
    "light" -> false
    "dark" -> true
    // 认不出来的值（含旧的/手改的）都按跟随系统处理，与 AppPrefs 的默认一致。
    else -> isSystemInDarkTheme()
}
