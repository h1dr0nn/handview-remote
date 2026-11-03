package io.github.h1dr0nn.handview

import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import io.github.h1dr0nn.handview.client.GodotKeys
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import io.github.h1dr0nn.handview.client.LatencyTracker
import io.github.h1dr0nn.handview.protocol.*
import kotlin.math.roundToInt

data class CastState(
    val connecting: Boolean = false,
    val welcome: Welcome? = null,
    val editor: EditorStateValue = EditorStateValue.Stopped,
    val width: Int = 1,
    val height: Int = 1,
    val orientation: Orientation = Orientation.Portrait,
    val busy: Boolean = false,
    val fps: Int = 0,
    val latency: LatencyTracker.Summary? = null,
    val message: Int? = null,
    // Video only, no chrome; Back leaves it.
    val fullScreen: Boolean = false,
    // The Keyboard setting is on, and the soft keyboard is up.
    val keyboard: Boolean = false,
    val keyboardOpen: Boolean = false,
    val wifi: Boolean = false,
)

// Everything outside a live session: which screen is open and what it shows.
data class AppState(
    val screen: Screen = Screen.Home,
    val settings: AppSettings = AppSettings(),
    val offers: List<SessionOffer> = emptyList(),
    val lan: List<LanEditor> = emptyList(),
    // The editor waiting for its PIN, if the PIN sheet is open.
    val pinFor: LanEditor? = null,
    val notice: Int? = null,
)

interface Actions {
    fun command(cmd: Cmd)
    fun disconnect()
    fun dismissMessage()
    fun fullScreen(on: Boolean)
    fun toggleKeyboard()
    // A key typed on the phone: press and release.
    fun typeKey(keycode: Long, unicode: Long)
    // The area the video is shown in, with its unsafe edges (ViewSize).
    fun viewSize(view: ViewSize)
    fun connectTo(offer: SessionOffer)
    fun askPin(editor: LanEditor?)
    fun joinWithPin(editor: LanEditor, pin: String)
    fun scanQr()
    fun open(screen: Screen)
    fun wifiNotReady()
    fun change(settings: AppSettings)
    fun toggleTheme()
    fun surface(): SurfaceView
    val version: String
}

@Composable
fun HandviewScreen(ui: CastState, app: AppState, actions: Actions) {
    Box(Modifier.fillMaxSize().background(Palette.Paper)) {
        when {
            ui.welcome != null && ui.fullScreen -> {
                BackHandler { actions.fullScreen(false) }
                Stage(ui, actions, Modifier.fillMaxSize())
            }
            ui.welcome != null -> {
                BackHandler { actions.disconnect() }
                if (ui.orientation == Orientation.Landscape) CastLandscape(ui, actions) else CastPortrait(ui, actions)
            }
            ui.connecting -> UsbScreen(connecting = true) { actions.disconnect() }
            else -> {
                if (app.screen != Screen.Home) BackHandler { actions.open(Screen.Home) }
                when (app.screen) {
                    Screen.Home -> HomeScreen(app, actions)
                    Screen.Usb -> UsbScreen(connecting = false) { actions.open(Screen.Home) }
                    Screen.Settings -> SettingsScreen(app.settings, actions.version, actions::change) { actions.open(Screen.Home) }
                }
            }
        }
        // Messages float as a toast; in a session they sit above the controls.
        (ui.message ?: app.notice)?.let { message ->
            Toast(
                stringResource(message),
                Modifier.align(Alignment.BottomCenter).safeDrawingPadding().padding(start = 16.dp, end = 16.dp, bottom = if (ui.welcome != null) 96.dp else 24.dp),
                actions::dismissMessage,
            )
        }
    }
}

// ---- Cast ----

private enum class Control(val cmd: Cmd, val label: Int, val icon: ImageVector) {
    Play(Cmd.Play, R.string.play, Icons.Play),
    Pause(Cmd.Pause, R.string.pause, Icons.Pause),
    // Step stays in the protocol; the cast screen offers Reload in its place.
    Reload(Cmd.Restart, R.string.reload, Icons.Reload),
    Stop(Cmd.Stop, R.string.stop, Icons.Stop),
}

private fun enabled(ui: CastState, control: Control) = !ui.busy && when (control) {
    Control.Play -> ui.editor == EditorStateValue.Stopped
    Control.Pause, Control.Stop -> ui.editor == EditorStateValue.Playing || ui.editor == EditorStateValue.Paused
    Control.Reload -> ui.editor == EditorStateValue.Playing || ui.editor == EditorStateValue.Paused
}

@Composable
private fun encoderName(ui: CastState) = stringResource(
    when (ui.welcome?.encoder) {
        Encoder.Nvenc -> R.string.nvenc
        Encoder.MfSoftware -> R.string.mf_software
        Encoder.VideoToolbox -> R.string.videotoolbox
        else -> R.string.mf_hardware
    },
)

@Composable
private fun latencyText(ui: CastState): String {
    val latency = ui.latency ?: return stringResource(R.string.latency_none)
    return stringResource(R.string.latency_value, latency.p50.roundToInt(), latency.p95.roundToInt())
}

@Composable
private fun StatusRow(ui: CastState, modifier: Modifier, long: Boolean) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(Palette.Live))
        Spacer(Modifier.width(8.dp))
        Eyebrow(stringResource(if (ui.wifi) R.string.wifi_encoder_fps else R.string.usb_encoder_fps, encoderName(ui), ui.fps), Modifier.weight(1f), Palette.Ink)
        Eyebrow(if (long) stringResource(R.string.touch_to_display) + " · " + latencyText(ui) else latencyText(ui))
    }
}

@Composable
private fun CastPortrait(ui: CastState, actions: Actions) {
    // Chrome stays thin so a 9:16 game fills the full width of a tall phone.
    Column(Modifier.fillMaxSize().safeDrawingPadding()) {
        StatusRow(ui, Modifier.fillMaxWidth().height(36.dp).padding(horizontal = 16.dp), long = false)
        Stage(ui, actions, Modifier.weight(1f).fillMaxWidth())
        Row(Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 10.dp).height(56.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Control.entries.forEach { ControlTile(it, ui, actions, Modifier.weight(1f).fillMaxHeight()) }
            if (ui.keyboard) KeyboardTile(ui, actions, Modifier.width(48.dp).fillMaxHeight())
            FullScreenTile(ui, actions, Modifier.width(48.dp).fillMaxHeight())
            DisconnectTile(actions, Modifier.width(48.dp).fillMaxHeight())
        }
    }
}

@Composable
private fun CastLandscape(ui: CastState, actions: Actions) {
    Row(Modifier.fillMaxSize().safeDrawingPadding()) {
        Column(Modifier.weight(1f).fillMaxHeight()) {
            StatusRow(ui, Modifier.fillMaxWidth().height(36.dp).padding(horizontal = 16.dp), long = true)
            Stage(ui, actions, Modifier.weight(1f).fillMaxWidth())
        }
        Column(Modifier.width(80.dp).fillMaxHeight().padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Control.entries.forEach { ControlTile(it, ui, actions, Modifier.fillMaxWidth().weight(1f)) }
            if (ui.keyboard) KeyboardTile(ui, actions, Modifier.fillMaxWidth().height(44.dp))
            FullScreenTile(ui, actions, Modifier.fillMaxWidth().height(44.dp))
            DisconnectTile(actions, Modifier.fillMaxWidth().height(44.dp))
        }
    }
}

@Composable
private fun ControlTile(control: Control, ui: CastState, actions: Actions, modifier: Modifier) {
    val on = enabled(ui, control)
    // Pause toggles (SPEC 6.4): while paused it reads Resume.
    val resume = control == Control.Pause && ui.editor == EditorStateValue.Paused
    val primary = on && (control == Control.Play || resume)
    val content = if (primary) Palette.OnAccent else Palette.Ink
    Column(
        modifier.alpha(if (on) 1f else 0.4f).clip(CardShape).background(if (primary) Palette.Accent else Palette.Card)
            .clickable(enabled = on, role = Role.Button) { actions.command(control.cmd) },
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(if (resume) Icons.Play else control.icon, null, Modifier.size(18.dp), tint = content)
        Spacer(Modifier.height(4.dp))
        Text(stringResource(if (resume) R.string.resume else control.label).uppercase(), style = Type.Button, color = content, maxLines = 1)
    }
}

@Composable
private fun KeyboardTile(ui: CastState, actions: Actions, modifier: Modifier) {
    val label = stringResource(R.string.keyboard)
    val on = ui.editor != EditorStateValue.Stopped
    Box(
        modifier.alpha(if (on) 1f else 0.4f).clip(CardShape).background(if (ui.keyboardOpen) Palette.Accent else Palette.Card)
            .clickable(enabled = on, role = Role.Button, onClickLabel = label) { actions.toggleKeyboard() },
        contentAlignment = Alignment.Center,
    ) {
        Icon(Icons.Keyboard, label, Modifier.size(20.dp), tint = if (ui.keyboardOpen) Palette.OnAccent else Palette.Ink)
    }
    if (ui.keyboardOpen) TypingField(actions)
}

// An invisible field that holds the soft keyboard open and turns what it
// receives into keys: it keeps one space so a Backspace still has something
// to delete.
@Composable
private fun TypingField(actions: Actions) {
    val focus = remember { FocusRequester() }
    var value by remember { mutableStateOf(TextFieldValue(" ", TextRange(1))) }
    BasicTextField(
        value,
        onValueChange = { next ->
            when {
                next.text.isEmpty() -> actions.typeKey(GodotKeys.BACKSPACE, 0)
                next.text.length > 1 -> next.text.drop(1).forEach { c -> actions.typeKey(GodotKeys.fromChar(c), c.code.toLong()) }
            }
            value = TextFieldValue(" ", TextRange(1))
        },
        modifier = Modifier.size(1.dp).alpha(0f).focusRequester(focus),
        keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, imeAction = ImeAction.Send),
        keyboardActions = KeyboardActions(onSend = { actions.typeKey(GodotKeys.ENTER, 0) }),
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}

@Composable
private fun FullScreenTile(ui: CastState, actions: Actions, modifier: Modifier) {
    val label = stringResource(R.string.full_screen)
    val on = ui.editor != EditorStateValue.Stopped
    Box(
        modifier.alpha(if (on) 1f else 0.4f).clip(CardShape).background(Palette.Card)
            .clickable(enabled = on, role = Role.Button, onClickLabel = label) { actions.fullScreen(true) },
        contentAlignment = Alignment.Center,
    ) {
        Icon(Icons.Expand, label, Modifier.size(20.dp), tint = Palette.Ink)
    }
}

@Composable
private fun DisconnectTile(actions: Actions, modifier: Modifier) {
    val label = stringResource(R.string.disconnect)
    Box(
        modifier.clip(CardShape).border(1.dp, Palette.Line, CardShape).clickable(role = Role.Button, onClickLabel = label) { actions.disconnect() },
        contentAlignment = Alignment.Center,
    ) {
        Icon(Icons.Close, label, Modifier.size(20.dp), tint = Palette.Ink)
    }
}

@Composable
private fun Stage(ui: CastState, actions: Actions, modifier: Modifier) {
    // Unsafe edges of the screen (cutout, bars) that fall inside the stage.
    val safe = WindowInsets.safeDrawing
    val density = LocalDensity.current
    val window = LocalView.current.rootView
    BoxWithConstraints(
        modifier.background(Palette.Stage).onGloballyPositioned { c ->
            val pos = c.positionInRoot()
            val w = c.size.width; val h = c.size.height
            val left = (safe.getLeft(density, LayoutDirection.Ltr) - pos.x.toInt()).coerceIn(0, w)
            val top = (safe.getTop(density) - pos.y.toInt()).coerceIn(0, h)
            val right = (safe.getRight(density, LayoutDirection.Ltr) - (window.width - pos.x.toInt() - w)).coerceIn(0, w)
            val bottom = (safe.getBottom(density) - (window.height - pos.y.toInt() - h)).coerceIn(0, h)
            actions.viewSize(ViewSize(w, h, left, top, right, bottom))
        },
        contentAlignment = Alignment.Center,
    ) {
        val screen = LocalConfiguration.current
        val ratio = if (ui.width > 0 && ui.height > 0) ui.width.toFloat() / ui.height else screen.screenWidthDp.coerceAtLeast(1).toFloat() / screen.screenHeightDp.coerceAtLeast(1)
        // Before a stream there is no picture to letterbox: fill the stage.
        val videoModifier = when {
            ui.editor == EditorStateValue.Stopped || ui.editor == EditorStateValue.Reloading -> Modifier.fillMaxSize()
            maxWidth / maxHeight > ratio -> Modifier.fillMaxHeight().aspectRatio(ratio)
            else -> Modifier.fillMaxWidth().aspectRatio(ratio)
        }
        AndroidView(factory = { actions.surface() }, modifier = videoModifier)
        when (ui.editor) {
            EditorStateValue.Stopped -> StagePanel(R.string.not_playing, R.string.not_playing_hint)
            EditorStateValue.Reloading -> StagePanel(R.string.reloading, R.string.reloading_hint)
            EditorStateValue.Compiling -> StagePanel(R.string.compiling, R.string.compiling_hint)
            EditorStateValue.Paused -> Box(Modifier.align(Alignment.TopStart).padding(12.dp).clip(CircleShape).background(Palette.Paper).padding(horizontal = 12.dp, vertical = 6.dp)) {
                Eyebrow(stringResource(R.string.paused), color = Palette.Ink)
            }
            else -> Unit
        }
    }
}

// A message card in the middle of the stage.
@Composable
private fun StagePanel(title: Int, hint: Int) {
    Column(
        Modifier.padding(24.dp).widthIn(max = 320.dp).clip(RoundedCornerShape(20.dp)).background(Palette.Paper).padding(horizontal = 22.dp, vertical = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(stringResource(title), style = Type.Heading, color = Palette.Ink, textAlign = TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        Text(stringResource(hint), style = Type.Caption, color = Palette.Muted, textAlign = TextAlign.Center)
    }
}
