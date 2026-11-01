package io.github.h1dr0nn.handview

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// Warm paper palette: one accent, everything else ink on paper. The dark
// variant keeps the same warmth. Every color reads `dark`, so switching it
// recomposes whatever used the palette.
object Palette {
    var dark by mutableStateOf(false)
    val Paper get() = if (dark) Color(0xFF161412) else Color(0xFFF4EFE7)
    val Card get() = if (dark) Color(0xFF23201C) else Color(0xFFEBE4DA)
    val CardActive get() = if (dark) Color(0xFF3A2A20) else Color(0xFFF0DCCB)
    val Ink get() = if (dark) Color(0xFFF1ECE4) else Color(0xFF1F1C19)
    val Muted get() = if (dark) Color(0xFFA39B90) else Color(0xFF7D766D)
    val Faint get() = if (dark) Color(0xFF6E675E) else Color(0xFFB9B0A4)
    val Line get() = if (dark) Color(0xFF35312C) else Color(0xFFDCD3C7)
    val Accent get() = if (dark) Color(0xFFD9774E) else Color(0xFFC5623A)
    val OnAccent get() = if (dark) Color(0xFF1A1310) else Color(0xFFFFF8F1)
    val Live get() = if (dark) Color(0xFF79A784) else Color(0xFF4F7A5A)
    // Same as the video surface before the first frame, so no edge shows around it.
    val Stage = Color(0xFF000000)
}

object Type {
    private val family = FontFamily.Default
    // Cap height of the system sans (Roboto) as a share of the font size.
    const val CAP_HEIGHT = 0.711f
    // Small caps-style labels above titles and in status rows.
    val Eyebrow = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 11.sp, letterSpacing = 1.4.sp, fontFeatureSettings = "tnum")
    val Wordmark = TextStyle(fontFamily = family, fontWeight = FontWeight.ExtraBold, letterSpacing = (-1).sp)
    val Title = TextStyle(fontFamily = family, fontWeight = FontWeight.ExtraBold, fontSize = 34.sp, lineHeight = 38.sp, letterSpacing = (-0.6).sp)
    val Subtitle = TextStyle(fontFamily = family, fontWeight = FontWeight.Normal, fontSize = 14.sp, lineHeight = 20.sp)
    val CardTitle = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 16.sp, lineHeight = 21.sp)
    val Caption = TextStyle(fontFamily = family, fontWeight = FontWeight.Normal, fontSize = 13.sp, lineHeight = 18.sp)
    val Button = TextStyle(fontFamily = family, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 1.sp)
    val Heading = TextStyle(fontFamily = family, fontWeight = FontWeight.Bold, fontSize = 22.sp, lineHeight = 26.sp, letterSpacing = (-0.3).sp)
}

@Composable
fun HandviewTheme(content: @Composable () -> Unit) {
    val colors = if (Palette.dark) {
        darkColorScheme(
            primary = Palette.Accent, onPrimary = Palette.OnAccent, background = Palette.Paper, onBackground = Palette.Ink,
            surface = Palette.Paper, onSurface = Palette.Ink, surfaceVariant = Palette.Card, onSurfaceVariant = Palette.Muted, outline = Palette.Line,
        )
    } else {
        lightColorScheme(
            primary = Palette.Accent, onPrimary = Palette.OnAccent, background = Palette.Paper, onBackground = Palette.Ink,
            surface = Palette.Paper, onSurface = Palette.Ink, surfaceVariant = Palette.Card, onSurfaceVariant = Palette.Muted, outline = Palette.Line,
        )
    }
    MaterialTheme(colorScheme = colors, content = content)
}
