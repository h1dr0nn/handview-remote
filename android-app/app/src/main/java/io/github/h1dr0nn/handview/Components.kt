package io.github.h1dr0nn.handview

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.getValue
import kotlinx.coroutines.delay
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

val CardShape = RoundedCornerShape(16.dp)

@Composable
fun Eyebrow(text: String, modifier: Modifier = Modifier, color: Color = Palette.Muted) =
    Text(text.uppercase(), modifier, style = Type.Eyebrow, color = color, maxLines = 1, overflow = TextOverflow.Ellipsis)

val HeaderButtonSize = 40.dp
const val HeaderButtonGap = 8

// The app name as a logo whose text box is exactly as tall as the header
// buttons: font size and line height both equal the button size.
@Composable
fun Wordmark(text: String, modifier: Modifier = Modifier) {
    // A touch under the box height so the letters sit inside the buttons' outline.
    val size = with(LocalDensity.current) { (HeaderButtonSize * 0.9f).toSp() }
    val line = with(LocalDensity.current) { HeaderButtonSize.toSp() }
    Text(
        text, modifier.height(HeaderButtonSize), maxLines = 1, softWrap = false, color = Palette.Ink,
        style = Type.Wordmark.copy(
            fontSize = size, lineHeight = line,
            lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both),
            platformStyle = PlatformTextStyle(includeFontPadding = false),
        ),
    )
}

// Round icon button used in top bars (Settings, Back, theme).
@Composable
fun IconButton(icon: ImageVector, label: String, onClick: () -> Unit) {
    Box(
        Modifier.size(HeaderButtonSize).clip(CircleShape).background(Palette.Card).clickable(role = Role.Button, onClickLabel = label, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, label, Modifier.size(20.dp), tint = Palette.Ink)
    }
}

// Screen header: back button with the title centered on the same row, then
// an optional subtitle.
@Composable
fun ScreenHeader(title: String, subtitle: String?, back: () -> Unit) {
    Box(Modifier.fillMaxWidth().height(HeaderButtonSize), contentAlignment = Alignment.Center) {
        Box(Modifier.align(Alignment.CenterStart)) { IconButton(Icons.Back, stringResource(R.string.back), back) }
        Text(title, Modifier.padding(horizontal = HeaderButtonSize + 8.dp), style = Type.Heading, color = Palette.Ink, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
    if (subtitle != null) {
        Spacer(Modifier.height(20.dp))
        Text(subtitle, style = Type.Subtitle, color = Palette.Muted)
    }
}

@Composable
fun SectionLabel(text: String) {
    Spacer(Modifier.height(28.dp))
    Eyebrow(text)
    Spacer(Modifier.height(12.dp))
}

// A short message floating over the screen; it fades out on its own and
// never moves the layout.
@Composable
fun Toast(text: String, modifier: Modifier = Modifier, dismiss: () -> Unit) {
    var shown by remember(text) { mutableStateOf(false) }
    val alpha by animateFloatAsState(if (shown) 1f else 0f, tween(220), label = "toast")
    LaunchedEffect(text) {
        shown = true
        delay(3500)
        shown = false
        delay(240)
        dismiss()
    }
    Text(
        text,
        modifier.widthIn(max = 420.dp).alpha(alpha).clip(RoundedCornerShape(20.dp)).background(Palette.ToastBackground)
            .clickable(role = Role.Button, onClick = dismiss).padding(horizontal = 18.dp, vertical = 12.dp),
        style = Type.Caption, color = Palette.ToastText,
    )
}

// Small outlined label, e.g. the transport of a device or "Soon".
@Composable
fun Tag(text: String, accent: Boolean = false) {
    Text(
        text.uppercase(),
        Modifier.clip(CircleShape).border(1.dp, if (accent) Palette.Accent else Palette.Faint, CircleShape).padding(horizontal = 8.dp, vertical = 3.dp),
        style = Type.Eyebrow,
        color = if (accent) Palette.Accent else Palette.Muted,
    )
}

@Composable
fun AccentButton(text: String, modifier: Modifier = Modifier, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        modifier.alpha(if (enabled) 1f else 0.4f).clip(CardShape).background(Palette.Accent)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick).padding(horizontal = 20.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text.uppercase(), style = Type.Button, color = Palette.OnAccent)
    }
}

@Composable
fun Toggle(checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    val track by animateColorAsState(if (checked) Palette.Accent else Palette.Line, label = "track")
    val offset: Dp by animateDpAsState(if (checked) 20.dp else 2.dp, label = "thumb")
    Box(
        Modifier.alpha(if (enabled) 1f else 0.4f).size(width = 46.dp, height = 28.dp).clip(CircleShape).background(track)
            .clickable(enabled = enabled, role = Role.Switch) { onChange(!checked) },
    ) {
        Box(Modifier.padding(start = offset, top = 2.dp).size(24.dp).clip(CircleShape).background(Palette.Paper))
    }
}

// A card holding rows separated by hairlines.
@Composable
fun Group(content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().clip(CardShape).background(Palette.Card), content = content)
}

@Composable
fun GroupDivider() = Box(Modifier.padding(horizontal = 18.dp).fillMaxWidth().height(1.dp).background(Palette.Line))
