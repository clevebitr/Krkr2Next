@file:OptIn(ExperimentalMaterial3ExpressiveApi::class)

package org.dpdns.clevebitr.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/**
 * Material 3 Expressive（MD3E）主题。
 *
 * ## 为什么是 MaterialExpressiveTheme 而不是 MaterialTheme
 *
 * MD3E 的弹性动效、更大的圆角与新组件（ButtonGroup、FloatingActionButtonMenu、
 * LoadingIndicator、浮动工具栏）都靠 [MaterialExpressiveTheme] 打开
 * `LocalUsingExpressiveTheme` 后才生效——它内部默认就给
 * `MotionScheme.expressive()`。用普通 `MaterialTheme` 时这些组件会退回标准动画与
 * 旧圆角，看起来就不像 MD3E。
 *
 * ## 配色为什么显式写
 *
 * 这个 material3 版本（1.5.0-alpha28）里**没有** `ColorScheme.fromSeed`（对 sources
 * 全量搜索零命中），官方对暗色的注释也是直接让用 `darkColorScheme()`（该版本只有
 * `expressiveLightColorScheme()` 这一个 expressive 基准亮色）。所以这里显式给出明暗
 * 两套角色：色相沿用应用原有的靛蓝强调色，但补齐 MD3E 组件会取用的容器色阶
 * （`*Container` / `surfaceContainer*` / `surfaceDim|Bright` / `outlineVariant`），
 * 否则新组件会落回默认紫色调，两批组件不像同一套设计语言。
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
    MaterialExpressiveTheme(
        colorScheme = if (darkTheme) DarkScheme else LightScheme,
        motionScheme = MotionScheme.expressive(),
        content = content,
    )
}
