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

@Composable
fun resolveDarkTheme(mode: String): Boolean = when (mode) {
    "light" -> false
    "dark" -> true
    else -> isSystemInDarkTheme()
}
