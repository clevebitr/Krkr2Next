package org.dpdns.clevebitr.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Accent = Color(0xFF6C7BFF)

private val DarkScheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    background = Color(0xFF121212),
    surface = Color(0xFF1B1B1F),
)

private val LightScheme = lightColorScheme(
    primary = Accent,
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
