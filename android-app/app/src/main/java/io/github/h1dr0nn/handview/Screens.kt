package io.github.h1dr0nn.handview

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

enum class Screen { Home, Usb, ScanQr, Settings }

@Composable
private fun Page(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 20.dp),
        content = content,
    )
}

// ---- Home (Connect) ----

@Composable
fun HomeScreen(offers: List<SessionOffer>, notice: Int?, open: (Screen) -> Unit, connect: (SessionOffer) -> Unit, wifiNotReady: () -> Unit, toggleTheme: () -> Unit) {
    var address by rememberSaveable { mutableStateOf("") }
    Page {
        Row(Modifier.fillMaxWidth().height(HeaderButtonSize), verticalAlignment = Alignment.CenterVertically) {
            Wordmark(stringResource(R.string.app_name))
            Spacer(Modifier.weight(1f))
            IconButton(if (Palette.dark) Icons.Sun else Icons.Moon, stringResource(if (Palette.dark) R.string.light_mode else R.string.dark_mode), toggleTheme)
            Spacer(Modifier.width(HeaderButtonGap.dp))
            IconButton(Icons.Settings, stringResource(R.string.settings_title)) { open(Screen.Settings) }
        }
        Spacer(Modifier.height(18.dp))
        Text(stringResource(R.string.home_title), style = Type.Title, color = Palette.Ink)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.home_subtitle), style = Type.Subtitle, color = Palette.Muted)
        notice?.let { Spacer(Modifier.height(20.dp)); Notice(stringResource(it)) }
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
            AccentButton(stringResource(R.string.connect), enabled = address.isNotEmpty(), modifier = Modifier.fillMaxHeight(), onClick = wifiNotReady)
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ModeTile(Icons.Usb, stringResource(R.string.usb), null, Modifier.weight(1f)) { open(Screen.Usb) }
            ModeTile(Icons.Wifi, stringResource(R.string.wifi), stringResource(R.string.soon), Modifier.weight(1f), wifiNotReady)
            ModeTile(Icons.Qr, stringResource(R.string.scan_qr), null, Modifier.weight(1f)) { open(Screen.ScanQr) }
        }
        SectionLabel(stringResource(R.string.devices))
        if (offers.isEmpty()) {
            Column(Modifier.fillMaxWidth().clip(CardShape).border(1.dp, Palette.Line, CardShape).padding(18.dp)) {
                Text(stringResource(R.string.no_devices_title), style = Type.CardTitle, color = Palette.Ink)
                Spacer(Modifier.height(3.dp))
                Text(stringResource(R.string.no_devices_body), style = Type.Caption, color = Palette.Muted)
            }
        } else Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            offers.forEach { DeviceRow(it) { connect(it) } }
        }
    }
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
private fun DeviceRow(offer: SessionOffer, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(CardShape).background(Palette.Card).clickable(role = Role.Button, onClick = onClick).padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(44.dp).clip(RoundedCornerShape(12.dp)).background(Palette.Paper), contentAlignment = Alignment.Center) {
            // Re-read when a newer offer rewrote the file.
            val icon = remember(offer.icon, offer.atMillis) { offer.icon?.let { BitmapFactory.decodeFile(it.path) }?.asImageBitmap() }
            if (icon != null) Image(icon, null, Modifier.fillMaxSize())
            else Text(offer.name.firstOrNull()?.uppercase() ?: "G", style = Type.Heading, color = Palette.Accent)
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(offer.name.ifEmpty { stringResource(R.string.godot_editor) }, style = Type.CardTitle, color = Palette.Ink)
            Spacer(Modifier.height(2.dp))
            Text(stringResource(R.string.device_ready), style = Type.Caption, color = Palette.Muted)
        }
        Spacer(Modifier.width(10.dp))
        Tag(stringResource(R.string.usb))
    }
}

// ---- USB Waiting ----

@Composable
fun UsbScreen(connecting: Boolean, message: Int?, back: () -> Unit) {
    Page {
        ScreenHeader(stringResource(if (connecting) R.string.connecting else R.string.waiting_title), stringResource(R.string.waiting_subtitle), back)
        message?.let { Spacer(Modifier.height(20.dp)); Notice(stringResource(it)) }
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

// ---- Scan QR ----

@Composable
fun ScanQrScreen(back: () -> Unit, manual: () -> Unit) {
    Column(Modifier.fillMaxSize().safeDrawingPadding().padding(20.dp)) {
        ScreenHeader(stringResource(R.string.scan_qr), stringResource(R.string.scan_subtitle), back)
        Spacer(Modifier.height(24.dp))
        Box(Modifier.weight(1f).fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(Palette.Stage), contentAlignment = Alignment.Center) {
            Box(
                Modifier.size(220.dp).drawBehind {
                    val stroke = Stroke(width = 3.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(18f, 14f)))
                    drawRoundRect(Palette.Paper.copy(alpha = 0.8f), style = stroke, cornerRadius = androidx.compose.ui.geometry.CornerRadius(24.dp.toPx()))
                    drawCircle(Palette.Accent, radius = 4.dp.toPx(), center = Offset(size.width / 2, size.height / 2))
                },
            )
            Column(Modifier.align(Alignment.BottomCenter).padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Tag(stringResource(R.string.soon), accent = true)
                Spacer(Modifier.height(8.dp))
                Text(stringResource(R.string.scan_soon), style = Type.Caption, color = Palette.Faint, textAlign = TextAlign.Center)
            }
        }
        Spacer(Modifier.height(16.dp))
        Box(
            Modifier.fillMaxWidth().height(52.dp).clip(CardShape).border(1.dp, Palette.Line, CardShape).clickable(role = Role.Button, onClick = manual),
            contentAlignment = Alignment.Center,
        ) {
            Text(stringResource(R.string.enter_ip).uppercase(), style = Type.Button, color = Palette.Ink)
        }
    }
}

// ---- Settings ----

@Composable
fun SettingsScreen(settings: AppSettings, version: String, change: (AppSettings) -> Unit, back: () -> Unit) {
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    fun toggle(key: String) { open = if (open == key) null else key }
    Page {
        ScreenHeader(stringResource(R.string.settings_title), null, back)
        SectionLabel(stringResource(R.string.video))
        Group {
            ChoiceRow(stringResource(R.string.codec), stringResource(R.string.h264), open == "codec", listOf(stringResource(R.string.h264)), 0, soon = false, onToggle = { toggle("codec") }) {}
            GroupDivider()
            ChoiceRow(stringResource(R.string.resolution), stringResource(settings.resolution.label), open == "res",
                Resolution.entries.map { stringResource(it.label) }, settings.resolution.ordinal, soon = true, onToggle = { toggle("res") }) { change(settings.copy(resolution = Resolution.entries[it])); open = null }
            GroupDivider()
            ChoiceRow(stringResource(R.string.fps), settings.fps.toString(), open == "fps",
                FpsOptions.map { it.toString() }, FpsOptions.indexOf(settings.fps), soon = true, onToggle = { toggle("fps") }) { change(settings.copy(fps = FpsOptions[it])); open = null }
            GroupDivider()
            val bitrates = BitrateOptions.map { if (it == 0) stringResource(R.string.auto) else stringResource(R.string.mbps, it / 1000) }
            ChoiceRow(stringResource(R.string.bitrate), bitrates[BitrateOptions.indexOf(settings.bitrateKbps)], open == "bitrate",
                bitrates, BitrateOptions.indexOf(settings.bitrateKbps), soon = true, onToggle = { toggle("bitrate") }) { change(settings.copy(bitrateKbps = BitrateOptions[it])); open = null }
        }
        SectionLabel(stringResource(R.string.input))
        Group {
            SwitchRow(stringResource(R.string.touch), stringResource(R.string.touch_hint), settings.touch, soon = false) { change(settings.copy(touch = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.gyroscope), null, settings.gyroscope, soon = true) { change(settings.copy(gyroscope = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.accelerometer), null, settings.accelerometer, soon = true) { change(settings.copy(accelerometer = it)) }
            GroupDivider()
            SwitchRow(stringResource(R.string.keyboard), null, settings.keyboard, soon = true) { change(settings.copy(keyboard = it)) }
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
private fun ChoiceRow(label: String, value: String, expanded: Boolean, options: List<String>, selected: Int, soon: Boolean, onToggle: () -> Unit, pick: (Int) -> Unit) {
    val enabled = options.size > 1 && !soon
    Column(Modifier.fillMaxWidth()) {
        Row(
            Modifier.fillMaxWidth().heightIn(min = 56.dp).clickable(enabled = enabled, role = Role.Button, onClick = onToggle).padding(horizontal = 18.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(label, Modifier.weight(1f), style = Type.CardTitle, color = Palette.Ink)
            if (soon) { Tag(stringResource(R.string.soon)); Spacer(Modifier.width(8.dp)) }
            Text(value.uppercase(), Modifier.clip(CircleShape).background(Palette.Paper).padding(horizontal = 12.dp, vertical = 6.dp), style = Type.Button, color = if (enabled || options.size == 1) Palette.Ink else Palette.Muted)
        }
        if (expanded) Row(Modifier.fillMaxWidth().padding(start = 18.dp, end = 18.dp, bottom = 14.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEachIndexed { i, option ->
                val on = i == selected
                Box(
                    Modifier.weight(1f).height(38.dp).clip(CircleShape).background(if (on) Palette.Accent else Palette.Paper).clickable(role = Role.RadioButton) { pick(i) },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(option.uppercase(), style = Type.Button, color = if (on) Palette.OnAccent else Palette.Ink)
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
