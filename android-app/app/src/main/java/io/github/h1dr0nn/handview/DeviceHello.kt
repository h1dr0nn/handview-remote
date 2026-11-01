package io.github.h1dr0nn.handview

import android.app.Activity
import android.os.Build
import android.media.MediaCodecInfo.CodecProfileLevel
import android.media.MediaCodecList
import android.util.DisplayMetrics
import io.github.h1dr0nn.handview.protocol.*

@Suppress("DEPRECATION")
fun deviceHello(activity: Activity): Hello {
    val metrics = DisplayMetrics()
    activity.windowManager.defaultDisplay.getRealMetrics(metrics)
    val bounds = if (Build.VERSION.SDK_INT >= 30) activity.windowManager.maximumWindowMetrics.bounds else null
    val decoders = MediaCodecList(MediaCodecList.ALL_CODECS).codecInfos.asSequence()
        .filter { !it.isEncoder && it.supportedTypes.any { type -> type.equals("video/avc", true) } }
        .filter { if (Build.VERSION.SDK_INT >= 29) it.isHardwareAccelerated else !it.name.startsWith("OMX.google.", true) && !it.name.startsWith("c2.android.", true) }
        .mapNotNull { info -> runCatching {
            val caps = info.getCapabilitiesForType("video/avc")
            val profiles = mapOf(CodecProfileLevel.AVCProfileBaseline to Profile.Baseline, CodecProfileLevel.AVCProfileMain to Profile.Main, CodecProfileLevel.AVCProfileHigh to Profile.High, CodecProfileLevel.AVCProfileConstrainedBaseline to Profile.Baseline, CodecProfileLevel.AVCProfileConstrainedHigh to Profile.High)
            val profile = caps.profileLevels.mapNotNull { profiles[it.profile] }.maxByOrNull { it.wire } ?: return@runCatching null
            val level = caps.profileLevels.filter { profiles[it.profile] == profile }.maxOf { avcLevel(it.level) }
            Decoder(Codec.H264, profile, level, caps.videoCapabilities.supportedWidths.upper.coerceAtMost(65535), caps.videoCapabilities.supportedHeights.upper.coerceAtMost(65535))
        }.getOrNull() }.take(4).toList()
    return Hello(PROTOCOL_VERSION, Bytes(ByteArray(32)), (bounds?.width() ?: metrics.widthPixels).coerceIn(1,65535), (bounds?.height() ?: metrics.heightPixels).coerceIn(1,65535), metrics.densityDpi.coerceIn(1,65535), activity.windowManager.defaultDisplay.refreshRate.toInt().coerceIn(1,255), decoders, Build.MODEL)
}

private fun avcLevel(level: Int): Int = when (level) {
    CodecProfileLevel.AVCLevel1 -> 10
    CodecProfileLevel.AVCLevel1b -> 9
    CodecProfileLevel.AVCLevel11 -> 11
    CodecProfileLevel.AVCLevel12 -> 12
    CodecProfileLevel.AVCLevel13 -> 13
    CodecProfileLevel.AVCLevel2 -> 20
    CodecProfileLevel.AVCLevel21 -> 21
    CodecProfileLevel.AVCLevel22 -> 22
    CodecProfileLevel.AVCLevel3 -> 30
    CodecProfileLevel.AVCLevel31 -> 31
    CodecProfileLevel.AVCLevel32 -> 32
    CodecProfileLevel.AVCLevel4 -> 40
    CodecProfileLevel.AVCLevel41 -> 41
    CodecProfileLevel.AVCLevel42 -> 42
    CodecProfileLevel.AVCLevel5 -> 50
    CodecProfileLevel.AVCLevel51 -> 51
    CodecProfileLevel.AVCLevel52 -> 52
    CodecProfileLevel.AVCLevel6 -> 60
    CodecProfileLevel.AVCLevel61 -> 61
    CodecProfileLevel.AVCLevel62 -> 62
    else -> 10
}
