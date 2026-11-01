package io.github.h1dr0nn.handview.protocol

import java.io.File
import org.junit.Assert.*
import org.junit.Test

class MessagesTest {
    @Test fun sharedVectors() {
        val file = File(System.getProperty("handview.vectors"), "messages.tsv")
        assertTrue(file.isFile)
        var count = 0
        file.forEachLine { line ->
            if (line.isBlank() || line.startsWith("#")) return@forEachLine
            val (name, channel, expected, hex) = line.split('\t')
            val bytes = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
            val ch = Channel.valueOf(channel.uppercase())
            val result = decode(bytes, 0, bytes.size, ch)
            assertEquals(name, expected.uppercase(), result.status.name)
            val consumed = if (expected == "need_more" || expected == "too_large") 0 else HEADER_SIZE + readU32(bytes, 2).toInt()
            assertEquals(name, consumed, result.consumed)
            if (expected == "ok") {
                assertArrayEquals(name, bytes, encode(result.message!!))
                val padded = byteArrayOf(99, 98) + bytes + byteArrayOf(97)
                assertEquals(name, result, decode(padded, 2, bytes.size, ch))
            }
            when (name) {
                "hello" -> {
                    val m = result.message as Hello
                    assertEquals(1, m.protoVersion)
                    assertEquals(31, m.token.toByteArray()[31].toInt())
                    assertEquals(listOf(1080, 2400, 420, 90), listOf(m.screenW, m.screenH, m.dpi, m.refreshHz))
                    assertEquals("Pixel 9", m.model)
                    assertEquals(2, m.decoders.size)
                    assertEquals(Decoder(Codec.H264, Profile.Baseline, 31, 1920, 1080), m.decoders[0])
                    assertEquals(Decoder(Codec.H264, Profile.High, 51, 3840, 2160), m.decoders[1])
                }
                "welcome" -> {
                    val m = result.message as Welcome
                    assertEquals(30L, readU32(bytes, 2))
                    assertEquals(Bytes(ByteArray(16) { (it + 16).toByte() }), m.sessionId)
                    assertEquals(Host.Godot, m.host)
                    assertEquals(Codec.H264, m.codec)
                    assertEquals(Encoder.MfSoftware, m.encoder)
                    assertEquals(listOf(1920, 1080, 60), listOf(m.width, m.height, m.fps))
                    assertEquals(Orientation.Landscape, m.orientation)
                    assertEquals(12500L, m.bitrateKbps)
                    assertEquals(EditorStateValue.Playing, m.editorState)
                }
                "touch" -> {
                    val m = result.message as Touch
                    assertEquals(0xFEDCBA98L, m.touchSeq)
                    assertEquals(0x0102030405060708L, m.eventTimeUs)
                    assertEquals(listOf(Pointer(3, Phase.Began, .125f, .75f, .5f), Pointer(7, Phase.Moved, .875f, .25f, .625f)), m.pointers)
                }
                "video_frame" -> {
                    val m = result.message as VideoFrame
                    assertEquals(0xFFFFFFFFL, m.frameSeq)
                    assertEquals(0xFEDCBA98L, m.lastTouchSeq)
                    assertEquals(123456789L, m.ptsUs)
                    assertEquals(VideoFlags.Keyframe or VideoFlags.CodecConfig, m.flags)
                    assertEquals(Bytes(byteArrayOf(0, 0, 0, 1, 0x67, 0x64, 0, 0x1F)), m.data)
                }
                "link_start" -> assertEquals(LinkStart(1600, 900, 50, 16000L), result.message)
                "unknown" -> {
                    assertEquals(Unknown(0x1234, 4), result.message)
                    val combined = bytes + encode(RequestKeyframe)
                    val first = decode(combined, 0, combined.size, ch)
                    val second = decode(combined, first.consumed, combined.size - first.consumed, ch)
                    assertEquals(DecodeStatus.OK, second.status)
                    assertEquals(RequestKeyframe, second.message)
                }
            }
            count++
        }
        assertTrue(count >= 21)
    }

    @Test fun bytesHaveValueSemantics() {
        val source = byteArrayOf(1, 2, 3)
        val a = Bytes(source)
        source[0] = 9
        a.toByteArray()[0] = 8
        val b = Bytes(byteArrayOf(1, 2, 3))
        assertEquals(a, b)
        assertEquals(a.hashCode(), b.hashCode())
    }
}
