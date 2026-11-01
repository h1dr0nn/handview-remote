package io.github.h1dr0nn.handview

import android.view.SurfaceView
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import io.github.h1dr0nn.handview.client.LatencyTracker
import io.github.h1dr0nn.handview.protocol.*
import java.util.Locale

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
)

@Composable
fun HandviewScreen(ui: CastState, command: (Cmd) -> Unit, disconnect: () -> Unit, surface: () -> SurfaceView) {
    Surface(Modifier.fillMaxSize()) {
        if (ui.welcome == null) {
            Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                Text(stringResource(if (ui.connecting) R.string.connecting else R.string.usb_waiting_title), style = MaterialTheme.typography.headlineSmall)
                if (!ui.connecting) {
                    Spacer(Modifier.height(24.dp))
                    Text(stringResource(R.string.usb_steps))
                    ui.message?.let { Spacer(Modifier.height(16.dp)); Text(stringResource(it)) }
                } else CircularProgressIndicator(Modifier.padding(24.dp))
            }
        } else {
            Column(Modifier.fillMaxSize().safeDrawingPadding()) {
                val encoder = when (ui.welcome.encoder) { Encoder.Nvenc -> R.string.nvenc; Encoder.MfHardware -> R.string.mf_hardware; Encoder.MfSoftware -> R.string.software }
                Text(stringResource(R.string.status, stringResource(encoder), ui.fps), Modifier.padding(8.dp))
                val p50 = ui.latency?.let { String.format(Locale.getDefault(), "%.1f", it.p50) } ?: stringResource(R.string.no_sample)
                val p95 = ui.latency?.let { String.format(Locale.getDefault(), "%.1f", it.p95) } ?: stringResource(R.string.no_sample)
                Text(stringResource(R.string.latency, p50, p95), Modifier.padding(horizontal = 8.dp))
                ui.message?.let { Text(stringResource(it), Modifier.padding(8.dp)) }
                if (ui.orientation == Orientation.Landscape) {
                    Row(Modifier.weight(1f)) {
                        Video(ui, surface, command, Modifier.weight(1f).fillMaxHeight())
                        Controls(ui, command, disconnect, true)
                    }
                } else {
                    Video(ui, surface, command, Modifier.weight(1f).fillMaxWidth())
                    Controls(ui, command, disconnect, false)
                }
            }
        }
    }
}

@Composable
private fun Video(ui: CastState, surface: () -> SurfaceView, command: (Cmd) -> Unit, modifier: Modifier) {
    BoxWithConstraints(modifier, contentAlignment = Alignment.Center) {
        val screen = LocalConfiguration.current
        val ratio = if (ui.width > 0 && ui.height > 0) ui.width.toFloat() / ui.height else screen.screenWidthDp.coerceAtLeast(1).toFloat() / screen.screenHeightDp.coerceAtLeast(1)
        val videoModifier = if (maxWidth / maxHeight > ratio) Modifier.fillMaxHeight().aspectRatio(ratio) else Modifier.fillMaxWidth().aspectRatio(ratio)
        AndroidView(factory = { surface() }, modifier = videoModifier)
        if (ui.editor == EditorStateValue.Stopped) {
            Surface {
                Column(Modifier.padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(stringResource(R.string.not_playing))
                    Button(onClick = { command(Cmd.Play) }, enabled = !ui.busy) { Text(stringResource(R.string.play)) }
                }
            }
        } else if (ui.editor == EditorStateValue.Paused) Surface { Text(stringResource(R.string.paused), Modifier.padding(8.dp)) }
    }
}

@Composable
private fun Controls(ui: CastState, command: (Cmd) -> Unit, disconnect: () -> Unit, vertical: Boolean) {
    val content: @Composable () -> Unit = {
        listOf(Cmd.Play to R.string.play, Cmd.Pause to R.string.pause, Cmd.Step to R.string.step, Cmd.Stop to R.string.stop).forEach { (cmd, label) ->
            if (ui.welcome?.host != Host.Godot || cmd != Cmd.Pause && cmd != Cmd.Step) {
                val enabled = !ui.busy && when (cmd) {
                    Cmd.Play -> ui.editor == EditorStateValue.Stopped
                    Cmd.Pause, Cmd.Stop -> ui.editor == EditorStateValue.Playing || ui.editor == EditorStateValue.Paused
                    Cmd.Step -> ui.editor == EditorStateValue.Paused
                }
                TextButton(onClick = { command(cmd) }, enabled = enabled) { Text(stringResource(label)) }
            }
        }
        TextButton(onClick = disconnect) { Text(stringResource(R.string.disconnect)) }
    }
    if (vertical) Column(Modifier.padding(4.dp), horizontalAlignment = Alignment.CenterHorizontally) { content() }
    else Row(Modifier.fillMaxWidth().padding(4.dp), horizontalArrangement = Arrangement.SpaceEvenly) { content() }
}
