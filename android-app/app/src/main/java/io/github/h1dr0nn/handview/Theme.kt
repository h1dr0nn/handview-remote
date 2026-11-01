package io.github.h1dr0nn.handview

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// Warm paper palette: one accent, everything else ink on paper.
object Palette {
    val Paper = Color(0xFFF4EFE7)
    val Card = Color(0xFFEBE4DA)
    val CardActive = Color(0xFFF0DCCB)
    val Ink = Color(0xFF1F1C19)
    val Muted = Color(0xFF7D766D)
    val Faint = Color(0xFFB9B0A4)
    val Line = Color(0xFFDCD3C7)
    val Accent = Color(0xFFC5623A)
    val OnAccent = Color(0xFFFFF8F1)
    val Live = Color(0xFF4F7A5A)
    val Stage = Color(0xFF181614)
}

object Type {
    private val family = FontFamily.Default
    // Small caps-style labels above titles and in status rows.
    val Eyebrow = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 11.sp, letterSpacing = 1.4.sp, fontFeatureSettings = "tnum")
    val Title = TextStyle(fontFamily = family, fontWeight = FontWeight.ExtraBold, fontSize = 34.sp, lineHeight = 38.sp, letterSpacing = (-0.6).sp)
    val Subtitle = TextStyle(fontFamily = family, fontWeight = FontWeight.Normal, fontSize = 14.sp, lineHeight = 20.sp)
    val CardTitle = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 16.sp, lineHeight = 21.sp)
    val Caption = TextStyle(fontFamily = family, fontWeight = FontWeight.Normal, fontSize = 13.sp, lineHeight = 18.sp)
    val Button = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 1.sp)
    val Heading = TextStyle(fontFamily = family, fontWeight = FontWeight.Bold, fontSize = 22.sp, lineHeight = 26.sp, letterSpacing = (-0.3).sp)
}

@Composable
fun HandviewTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = lightColorScheme(
            primary = Palette.Accent,
            onPrimary = Palette.OnAccent,
            background = Palette.Paper,
            onBackground = Palette.Ink,
            surface = Palette.Paper,
            onSurface = Palette.Ink,
            surfaceVariant = Palette.Card,
            onSurfaceVariant = Palette.Muted,
            outline = Palette.Line,
        ),
        content = content,
    )
}
