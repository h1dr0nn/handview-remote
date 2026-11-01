package io.github.h1dr0nn.handview

import android.content.Context

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
    val autoReconnect: Boolean = true,
    // null follows the system setting.
    val dark: Boolean? = null,
)

// The video choices as sent to the editor (0 = automatic).
fun AppSettings.streamPrefs() = io.github.h1dr0nn.handview.protocol.StreamPrefs(resolution.longSide, resolution.shortSide, fps, bitrateKbps.toLong())

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
        autoReconnect = prefs.getBoolean("autoReconnect", true),
        dark = when (prefs.getInt("theme", 0)) { 1 -> false; 2 -> true; else -> null },
    )

    fun save(s: AppSettings) = prefs.edit()
        .putInt("resolution", s.resolution.ordinal).putInt("fps", s.fps).putInt("bitrate", s.bitrateKbps)
        .putBoolean("touch", s.touch).putBoolean("gyroscope", s.gyroscope).putBoolean("accelerometer", s.accelerometer)
        .putBoolean("keyboard", s.keyboard).putBoolean("autoReconnect", s.autoReconnect)
        .putInt("theme", when (s.dark) { null -> 0; false -> 1; true -> 2 })
        .apply()
}
