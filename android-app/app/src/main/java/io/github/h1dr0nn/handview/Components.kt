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
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.FirstBaseline
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Constraints
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

// The app name as a logo: its capitals span exactly the header button height,
// with the baseline on the buttons' bottom edge.
@Composable
fun Wordmark(text: String, modifier: Modifier = Modifier) {
    val density = LocalDensity.current
    val size = with(density) { (HeaderButtonSize / Type.CAP_HEIGHT).toSp() }
    Text(
        text, maxLines = 1, softWrap = false, style = Type.Wordmark.copy(fontSize = size, lineHeight = size), color = Palette.Ink,
        modifier = modifier.layout { measurable, constraints ->
            val placeable = measurable.measure(constraints.copy(minHeight = 0, maxHeight = Constraints.Infinity))
            val height = HeaderButtonSize.roundToPx()
            layout(placeable.width, height) { placeable.place(0, height - placeable[FirstBaseline]) }
        },
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

// Screen header: back button or app eyebrow, an optional trailing button, then the big title.
@Composable
fun ScreenHeader(title: String, subtitle: String?, back: (() -> Unit)?, trailing: (@Composable () -> Unit)? = null) {
    Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
        if (back != null) IconButton(Icons.Back, stringResource(R.string.back), back)
        else Eyebrow(stringResource(R.string.app_name))
        Spacer(Modifier.weight(1f))
        trailing?.invoke()
    }
    Spacer(Modifier.height(18.dp))
    Text(title, style = Type.Title, color = Palette.Ink)
    if (subtitle != null) {
        Spacer(Modifier.height(8.dp))
        Text(subtitle, style = Type.Subtitle, color = Palette.Muted)
    }
}

@Composable
fun SectionLabel(text: String) {
    Spacer(Modifier.height(28.dp))
    Eyebrow(text)
    Spacer(Modifier.height(12.dp))
}

@Composable
fun Notice(text: String, modifier: Modifier = Modifier) {
    Row(
        modifier.fillMaxWidth().clip(CardShape).background(Palette.CardActive).padding(horizontal = 18.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(Palette.Accent))
        Spacer(Modifier.width(12.dp))
        Text(text, style = Type.Caption, color = Palette.Ink)
    }
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
