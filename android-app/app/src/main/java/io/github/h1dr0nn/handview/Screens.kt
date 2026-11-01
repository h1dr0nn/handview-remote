package io.github.h1dr0nn.handview

import androidx.compose.ui.draw.shadow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.sp
import io.github.h1dr0nn.handview.client.WifiJoin
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

enum class Screen { Home, Usb, Settings }

@Composable
private fun Page(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 20.dp),
        content = content,
    )
}

// ---- Home (Connect) ----

@Composable
fun HomeScreen(app: AppState, actions: Actions) {
    var address by rememberSaveable { mutableStateOf("") }
    val typed = WifiJoin.parseAddress(address)
    Page {
        Row(Modifier.fillMaxWidth().height(HeaderButtonSize), verticalAlignment = Alignment.CenterVertically) {
            Wordmark(stringResource(R.string.app_name))
            Spacer(Modifier.weight(1f))
            IconButton(if (Palette.dark) Icons.Sun else Icons.Moon, stringResource(if (Palette.dark) R.string.light_mode else R.string.dark_mode), actions::toggleTheme)
            Spacer(Modifier.width(HeaderButtonGap.dp))
            IconButton(Icons.Settings, stringResource(R.string.settings_title)) { actions.open(Screen.Settings) }
        }
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.home_subtitle), style = Type.Subtitle, color = Palette.Muted)
        SectionLabel(stringResource(R.string.ip_port))
        Row(Modifier.fillMaxWidth().height(52.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Box(Modifier.weight(1f).fillMaxHeight().clip(CardShape).background(Palette.Card).padding(horizontal = 16.dp), contentAlignment = Alignment.CenterStart) {
                if (address.isEmpty()) Text(stringResource(R.string.ip_hint), style = Type.Subtitle, color = Palette.Faint)
                BasicTextField(
                    address, { address = it.filter { c -> c.isDigit() || c == '.' || c == ':' }.take(21) },
                    Modifier.fillMaxWidth(), singleLine = true, textStyle = Type.Subtitle.copy(color = Palette.Ink),
                    cursorBrush = SolidColor(Palette.Accent), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                )
            }
            AccentButton(stringResource(R.string.connect), enabled = typed != null, modifier = Modifier.fillMaxHeight()) {
                typed?.let { (host, port) -> actions.askPin(LanEditor(host, host, port, System.currentTimeMillis())) }
            }
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ModeTile(Icons.Usb, stringResource(R.string.usb), null, Modifier.weight(1f)) { actions.open(Screen.Usb) }
            ModeTile(Icons.Wifi, stringResource(R.string.wifi), null, Modifier.weight(1f), actions::wifiNotReady)
            ModeTile(Icons.Qr, stringResource(R.string.scan_qr), null, Modifier.weight(1f), actions::scanQr)
        }
        SectionLabel(stringResource(R.string.devices))
        if (app.offers.isEmpty() && app.lan.isEmpty()) {
            Column(Modifier.fillMaxWidth().clip(CardShape).border(1.dp, Palette.Line, CardShape).padding(18.dp)) {
                Text(stringResource(R.string.no_devices_title), style = Type.CardTitle, color = Palette.Ink)
                Spacer(Modifier.height(3.dp))
                Text(stringResource(R.string.no_devices_body), style = Type.Caption, color = Palette.Muted)
            }
        } else Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            app.offers.forEach { DeviceRow(it.name, stringResource(R.string.device_ready), stringResource(R.string.usb), it.icon) { actions.connectTo(it) } }
            app.lan.forEach { DeviceRow(it.name, stringResource(R.string.wifi_device, it.host), stringResource(R.string.wifi), null) { actions.askPin(it) } }
        }
    }
    app.pinFor?.let { PinSheet(it, actions) }
}

// The PIN from the Handview dock, for an editor heard on Wi-Fi or typed by hand.
@Composable
private fun PinSheet(editor: LanEditor, actions: Actions) {
    var pin by rememberSaveable { mutableStateOf("") }
    val focus = remember { FocusRequester() }
    Box(
        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.5f)).clickable(interactionSource = null, indication = null) { actions.askPin(null) }
            .safeDrawingPadding().imePadding().padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier.widthIn(max = 400.dp).fillMaxWidth().clip(RoundedCornerShape(24.dp)).background(Palette.Paper)
                .clickable(interactionSource = null, indication = null) {}.padding(20.dp),
        ) {
            Text(stringResource(R.string.pin_title), style = Type.Heading, color = Palette.Ink)
            Spacer(Modifier.height(6.dp))
            Text(stringResource(R.string.pin_body, editor.name.ifEmpty { editor.host }), style = Type.Caption, color = Palette.Muted)
            Spacer(Modifier.height(18.dp))
            Box(Modifier.fillMaxWidth().height(56.dp).clip(CardShape).background(Palette.Card).padding(horizontal = 16.dp), contentAlignment = Alignment.CenterStart) {
                if (pin.isEmpty()) Text("000000", style = Type.Heading.copy(letterSpacing = 6.sp), color = Palette.Faint)
                BasicTextField(
                    pin, { pin = it.filter(Char::isDigit).take(6) },
                    Modifier.fillMaxWidth().focusRequester(focus), singleLine = true,
                    textStyle = Type.Heading.copy(color = Palette.Ink, letterSpacing = 6.sp), cursorBrush = SolidColor(Palette.Accent),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword, imeAction = ImeAction.Go),
                    keyboardActions = KeyboardActions(onGo = { if (pin.length == 6) actions.joinWithPin(editor, pin) }),
                )
            }
            Spacer(Modifier.height(14.dp))
            Row(Modifier.fillMaxWidth().height(52.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(
                    Modifier.weight(1f).fillMaxHeight().clip(CardShape).border(1.dp, Palette.Line, CardShape).clickable(role = Role.Button) { actions.askPin(null) },
                    contentAlignment = Alignment.Center,
                ) { Text(stringResource(R.string.cancel).uppercase(), style = Type.Button, color = Palette.Ink) }
                AccentButton(stringResource(R.string.join), enabled = pin.length == 6, modifier = Modifier.weight(1f).fillMaxHeight()) { actions.joinWithPin(editor, pin) }
            }
        }
    }
    LaunchedEffect(Unit) { focus.requestFocus() }
}

@Composable
private fun ModeTile(icon: ImageVector, label: String, tag: String?, modifier: Modifier, onClick: () -> Unit) {
    Box(modifier.fillMaxHeight().clip(CardShape).background(Palette.Card).clickable(role = Role.Button, onClick = onClick)) {
        Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(icon, null, Modifier.size(24.dp), tint = Palette.Ink)
            Spacer(Modifier.height(10.dp))
            Text(label.uppercase(), style = Type.Button, color = Palette.Ink)
        }
        if (tag != null) Box(Modifier.align(Alignment.TopEnd).padding(8.dp).clip(CircleShape).background(Palette.Paper).padding(horizontal = 6.dp, vertical = 2.dp)) {
            Text(tag.uppercase(), style = Type.Eyebrow, color = Palette.Muted)
        }
    }
}

@Composable
private fun DeviceRow(name: String, detail: String, tag: String, icon: java.io.File?, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(CardShape).background(Palette.Card).clickable(role = Role.Button, onClick = onClick).padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(44.dp).clip(RoundedCornerShape(12.dp)).background(Palette.Paper), contentAlignment = Alignment.Center) {
            val image = remember(icon, icon?.lastModified()) { icon?.let { BitmapFactory.decodeFile(it.path) }?.asImageBitmap() }
            if (image != null) Image(image, null, Modifier.fillMaxSize())
            else Text(name.firstOrNull()?.uppercase() ?: "G", style = Type.Heading, color = Palette.Accent)
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(name.ifEmpty { stringResource(R.string.godot_editor) }, style = Type.CardTitle, color = Palette.Ink)
            Spacer(Modifier.height(2.dp))
            Text(detail, style = Type.Caption, color = Palette.Muted)
        }
        Spacer(Modifier.width(10.dp))
        Tag(tag)
    }
}

// ---- USB Waiting ----

@Composable
fun UsbScreen(connecting: Boolean, back: () -> Unit) {
    Page {
        ScreenHeader(stringResource(if (connecting) R.string.connecting else R.string.waiting_title), stringResource(R.string.waiting_subtitle), back)
        SectionLabel(stringResource(R.string.setup))
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            UsbStep(1, R.string.step_cable_title, R.string.step_cable_body, active = false)
            UsbStep(2, R.string.step_addon_title, R.string.step_addon_body, active = false)
            UsbStep(3, R.string.step_start_title, R.string.step_start_body, active = connecting)
        }
    }
}

@Composable
private fun UsbStep(number: Int, title: Int, body: Int, active: Boolean) {
    Row(
        Modifier.fillMaxWidth().clip(CardShape).background(if (active) Palette.CardActive else Palette.Card).padding(horizontal = 18.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(stringResource(title), style = Type.CardTitle, color = Palette.Ink)
            Spacer(Modifier.height(3.dp))
            Text(stringResource(body), style = Type.Caption, color = Palette.Muted)
        }
        Spacer(Modifier.width(16.dp))
        val circle = Modifier.size(28.dp).clip(CircleShape)
        Box(if (active) circle.background(Palette.Accent) else circle.border(1.5.dp, Palette.Faint, CircleShape), contentAlignment = Alignment.Center) {
            Text(number.toString(), style = Type.Button, color = if (active) Palette.OnAccent else Palette.Muted)
        }
    }
}

// ---- Settings ----

@Composable
fun SettingsScreen(settings: AppSettings, version: String, change: (AppSettings) -> Unit, back: () -> Unit) {
    Page {
        ScreenHeader(stringResource(R.string.settings_title), null, back)
        SectionLabel(stringResource(R.string.video))
        Group {
            ChoiceRow(stringResource(R.string.codec), stringResource(R.string.h264), listOf(stringResource(R.string.h264)), 0) {}
            GroupDivider()
            ChoiceRow(stringResource(R.string.resolution), stringResource(settings.resolution.label), Resolution.entries.map { stringResource(it.label) }, settings.resolution.ordinal) {
                change(settings.copy(resolution = Resolution.entries[it]))
            }
            GroupDivider()
            ChoiceRow(stringResource(R.string.fps), settings.fps.toString(), FpsOptions.map { it.toString() }, FpsOptions.indexOf(settings.fps)) {
                change(settings.copy(fps = FpsOptions[it]))
            }
            GroupDivider()
            val bitrates = BitrateOptions.map { if (it == 0) stringResource(R.string.auto) else stringResource(R.string.mbps, it / 1000) }
            ChoiceRow(stringResource(R.string.bitrate), bitrates[BitrateOptions.indexOf(settings.bitrateKbps)], bitrates, BitrateOptions.indexOf(settings.bitrateKbps)) {
                change(settings.copy(bitrateKbps = BitrateOptions[it]))
            }
        }
        SectionLabel(stringResource(R.string.input))
        Group {
            SwitchRow(stringResource(R.string.touch), stringResource(R.string.touch_hint), settings.touch, soon = false) { change(settings.copy(touch = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.gyroscope), null, settings.gyroscope, soon = false) { change(settings.copy(gyroscope = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.accelerometer), null, settings.accelerometer, soon = false) { change(settings.copy(accelerometer = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.keyboard), null, settings.keyboard, soon = false) { change(settings.copy(keyboard = it)) }
        }
        SectionLabel(stringResource(R.string.connection))
        Group {
            SwitchRow(stringResource(R.string.auto_reconnect), stringResource(R.string.auto_reconnect_hint), settings.autoReconnect, soon = false) { change(settings.copy(autoReconnect = it)) }
        }
        Spacer(Modifier.height(28.dp))
        Eyebrow(stringResource(R.string.version, version), Modifier.align(Alignment.CenterHorizontally), Palette.Faint)
    }
}

@Composable
private fun ChoiceRow(label: String, value: String, options: List<String>, selected: Int, pick: (Int) -> Unit) {
    val enabled = options.size > 1
    var open by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().heightIn(min = 56.dp).clickable(enabled = enabled, role = Role.DropdownList) { open = true }.padding(horizontal = 18.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, Modifier.weight(1f), style = Type.CardTitle, color = Palette.Ink)
        Box {
            Row(Modifier.clip(CircleShape).background(Palette.Paper).padding(start = 12.dp, end = if (enabled) 6.dp else 12.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(value.uppercase(), style = Type.Button, color = Palette.Ink)
                if (enabled) Icon(Icons.Chooser, null, Modifier.padding(start = 2.dp).size(18.dp), tint = Palette.Muted)
            }
            if (open) Dropdown(options, selected, { open = false }) { pick(it); open = false }
        }
    }
}

// A floating list under its anchor; it covers the page instead of pushing it.
@Composable
private fun Dropdown(options: List<String>, selected: Int, dismiss: () -> Unit, pick: (Int) -> Unit) {
    val gap = with(LocalDensity.current) { 40.dp.roundToPx() }
    Popup(alignment = Alignment.TopEnd, offset = IntOffset(0, gap), onDismissRequest = dismiss, properties = PopupProperties(focusable = true)) {
        Column(Modifier.width(180.dp).shadow(12.dp, RoundedCornerShape(16.dp)).clip(RoundedCornerShape(16.dp)).background(Palette.Card).border(1.dp, Palette.Line, RoundedCornerShape(16.dp)).padding(vertical = 6.dp)) {
            options.forEachIndexed { i, option ->
                val on = i == selected
                Row(
                    Modifier.fillMaxWidth().height(44.dp).clickable(role = Role.RadioButton) { pick(i) }.padding(horizontal = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(option, Modifier.weight(1f), style = Type.CardTitle, color = if (on) Palette.Accent else Palette.Ink)
                    if (on) Icon(Icons.Check, null, Modifier.size(18.dp), tint = Palette.Accent)
                }
            }
        }
    }
}

@Composable
private fun SwitchRow(label: String, hint: String?, checked: Boolean, soon: Boolean, change: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).padding(horizontal = 18.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = Type.CardTitle, color = Palette.Ink)
            if (hint != null) Text(hint, style = Type.Caption, color = Palette.Muted)
        }
        if (soon) { Tag(stringResource(R.string.soon)); Spacer(Modifier.width(10.dp)) }
        Toggle(checked && !soon, enabled = !soon, onChange = change)
    }
}
