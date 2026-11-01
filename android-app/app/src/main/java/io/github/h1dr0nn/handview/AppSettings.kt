package io.github.h1dr0nn.handview

import android.content.Context
import io.github.h1dr0nn.handview.protocol.Encoder
import io.github.h1dr0nn.handview.protocol.Host

enum class Resolution(val label: Int, val longSide: Int, val shortSide: Int) {
    Native(R.string.res_native, 0, 0),
    P1080(R.string.res_1080, 1920, 1080),
    P720(R.string.res_720, 1280, 720),
    P540(R.string.res_540, 960, 540),
}

val FpsOptions = listOf(30, 60, 90)
// 0 means the host picks the bitrate for the stream size.
val BitrateOptions = listOf(0, 4000, 8000, 12000, 20000)

data class AppSettings(
    val resolution: Resolution = Resolution.Native,
    val fps: Int = 60,
    val bitrateKbps: Int = 0,
    val touch: Boolean = true,
    val gyroscope: Boolean = false,
    val accelerometer: Boolean = false,
    val keyboard: Boolean = false,
    val autoReconnect: Boolean = false,
)

// An editor this phone streamed from, newest first on the Home screen.
data class RecentEditor(val host: Host, val encoder: Encoder, val width: Int, val height: Int, val atMillis: Long)

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("handview", Context.MODE_PRIVATE)

    fun load() = AppSettings(
        resolution = Resolution.entries.getOrElse(prefs.getInt("resolution", 0)) { Resolution.Native },
        fps = prefs.getInt("fps", 60).takeIf { it in FpsOptions } ?: 60,
        bitrateKbps = prefs.getInt("bitrate", 0).takeIf { it in BitrateOptions } ?: 0,
        touch = prefs.getBoolean("touch", true),
        gyroscope = prefs.getBoolean("gyroscope", false),
        accelerometer = prefs.getBoolean("accelerometer", false),
        keyboard = prefs.getBoolean("keyboard", false),
        autoReconnect = prefs.getBoolean("autoReconnect", false),
    )

    fun save(s: AppSettings) = prefs.edit()
        .putInt("resolution", s.resolution.ordinal).putInt("fps", s.fps).putInt("bitrate", s.bitrateKbps)
        .putBoolean("touch", s.touch).putBoolean("gyroscope", s.gyroscope).putBoolean("accelerometer", s.accelerometer)
        .putBoolean("keyboard", s.keyboard).putBoolean("autoReconnect", s.autoReconnect)
        .apply()

    fun recents(): List<RecentEditor> = prefs.getString("recents", "").orEmpty().split(';').mapNotNull { line ->
        val f = line.split(',')
        if (f.size != 5) return@mapNotNull null
        runCatching {
            RecentEditor(Host.entries.first { it.wire == f[0].toInt() }, Encoder.entries.first { it.wire == f[1].toInt() }, f[2].toInt(), f[3].toInt(), f[4].toLong())
        }.getOrNull()
    }

    fun remember(editor: RecentEditor): List<RecentEditor> {
        val list = (listOf(editor) + recents().filter { it.host != editor.host }).take(3)
        prefs.edit().putString("recents", list.joinToString(";") { "${it.host.wire},${it.encoder.wire},${it.width},${it.height},${it.atMillis}" }).apply()
        return list
    }
}
