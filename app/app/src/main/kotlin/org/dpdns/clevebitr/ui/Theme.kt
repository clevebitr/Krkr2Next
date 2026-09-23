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
 */

private val DarkScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFC4C0FF), // P80（M3 深色的主色本来就该是浅色调，文字/图标才看得清）
    onPrimary = Color(0xFF00297B), // P20
    primaryContainer = Color(0xFF1E3DA2), // P30
    onPrimaryContainer = Color(0xFFE2DFFF), // P90
    inversePrimary = Color(0xFF4354BE), // P40
    secondary = Color(0xFFC6C3E8), // S80
    onSecondary = Color(0xFF2E2D4B), // S20
    secondaryContainer = Color(0xFF454463), // S30
    onSecondaryContainer = Color(0xFFE2DFFF), // S90
    tertiary = Color(0xFFF7B4CB), // T80
    onTertiary = Color(0xFF531D34), // T20
    tertiaryContainer = Color(0xFF6D344B), // T30
    onTertiaryContainer = Color(0xFFFFD8E5), // T90
    background = Color(0xFF131318), // N6
    onBackground = Color(0xFFE3E2E9), // N90
    surface = Color(0xFF131318), // N6：比 M3 默认（N10）更黑一点，深色下省电也压得住画面
    onSurface = Color(0xFFE3E2E9), // N90
    surfaceVariant = Color(0xFF464557), // NV30
    onSurfaceVariant = Color(0xFFC7C4DA), // NV80
    surfaceDim = Color(0xFF0E0D14), // N4
    surfaceBright = Color(0xFF39383E), // N24
    surfaceContainerLowest = Color(0xFF0E0D14), // N4
    surfaceContainerLow = Color(0xFF1B1B20), // N10
    surfaceContainer = Color(0xFF201F24), // N12
    surfaceContainerHigh = Color(0xFF2A292F), // N17
    surfaceContainerHighest = Color(0xFF35343A), // N22
    outline = Color(0xFF918EA4), // NV60
    outlineVariant = Color(0xFF464557), // NV30
    inverseSurface = Color(0xFFE3E2E9), // N90
    inverseOnSurface = Color(0xFF303036), // N20
)

private val LightScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFF4354BE), // P40
    onPrimary = Color(0xFFFFFFFF), // P100
    primaryContainer = Color(0xFFE2DFFF), // P90
    onPrimaryContainer = Color(0xFF00164D), // P10
    inversePrimary = Color(0xFFC4C0FF), // P80
    secondary = Color(0xFF5D5B7C), // S40
    onSecondary = Color(0xFFFFFFFF), // S100
    secondaryContainer = Color(0xFFE2DFFF), // S90
    onSecondaryContainer = Color(0xFF181934), // S10
    tertiary = Color(0xFF874C63), // T40
    onTertiary = Color(0xFFFFFFFF), // T100
    tertiaryContainer = Color(0xFFFFD8E5), // T90
    onTertiaryContainer = Color(0xFF3B051F), // T10
    background = Color(0xFFFCFCFF), // N99
    onBackground = Color(0xFF1B1B20), // N10
    surface = Color(0xFFF9F9FF), // N98
    onSurface = Color(0xFF1B1B20), // N10
    surfaceVariant = Color(0xFFE3E0F7), // NV90
    onSurfaceVariant = Color(0xFF464557), // NV30
    surfaceDim = Color(0xFFDAD9E1), // N87
    surfaceBright = Color(0xFFF9F9FF), // N98
    surfaceContainerLowest = Color(0xFFFFFFFF), // N100
    surfaceContainerLow = Color(0xFFF4F3FA), // N96
    surfaceContainer = Color(0xFFEEEDF5), // N94
    surfaceContainerHigh = Color(0xFFE8E7EF), // N92
    surfaceContainerHighest = Color(0xFFE3E2E9), // N90
    outline = Color(0xFF777589), // NV50
    outlineVariant = Color(0xFFC7C4DA), // NV80
    inverseSurface = Color(0xFF303036), // N20
    inverseOnSurface = Color(0xFFF1F0F8), // N95
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

@Composable
fun resolveDarkTheme(mode: String): Boolean = when (mode) {
    "light" -> false
    "dark" -> true
    else -> isSystemInDarkTheme()
}
