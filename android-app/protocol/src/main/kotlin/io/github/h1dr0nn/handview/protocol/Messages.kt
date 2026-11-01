package io.github.h1dr0nn.handview.protocol

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

class Bytes(data: ByteArray) {
    private val value = data.copyOf()
    fun toByteArray(): ByteArray = value.copyOf()
    val size: Int get() = value.size
    override fun equals(other: Any?): Boolean = other is Bytes && value.contentEquals(other.value)
    override fun hashCode(): Int = value.contentHashCode()
}
sealed interface Message
enum class Host(val wire: Int) { Unity(0), Godot(1) }
enum class Codec(val wire: Int) { H264(1) }
enum class Profile(val wire: Int) { Baseline(66), Main(77), High(100) }
enum class Encoder(val wire: Int) { Nvenc(0), MfHardware(1), MfSoftware(2) }
enum class Orientation(val wire: Int) { Portrait(0), Landscape(1) }
enum class EditorStateValue(val wire: Int) { Stopped(0), Playing(1), Paused(2), Compiling(3), Reloading(4) }
enum class Cmd(val wire: Int) { Play(1), Pause(2), Step(3), Stop(4) }
enum class Result(val wire: Int) { Ok(0), Ignored(1), Busy(2) }
enum class Phase(val wire: Int) { Began(0), Moved(1), Stationary(2), Ended(3), Canceled(4) }
enum class RejectReason(val wire: Int) { BadVersion(0), BadToken(1), Busy(2) }
enum class PauseReason(val wire: Int) { NotPlaying(0), Paused(1) }
enum class ErrorSource(val wire: Int) { Encoder(0), Decoder(1) }
enum class ErrorCode(val wire: Int) { InitFailed(1), ProcessFailed(2), DeviceLost(3), Unsupported(4) }
enum class VideoFlags(val wire: Int) {
    None(0), Keyframe(1), CodecConfig(2), KeyframeCodecConfig(3);
    infix fun or(other: VideoFlags): VideoFlags = entries.first { it.wire == (wire or other.wire) }
}
enum class ByeReason(val wire: Int) { Normal(0), ProtocolError(1), Compiling(2), Replaced(3), Reloading(4) }
data class Decoder(val codec: Codec, val profile: Profile, val level: Int, val maxW: Int, val maxH: Int)
data class Pointer(val id: Int, val phase: Phase, val x: Float, val y: Float, val pressure: Float)
data class Hello(val protoVersion: Int, val token: Bytes, val screenW: Int, val screenH: Int, val dpi: Int, val refreshHz: Int, val decoders: List<Decoder>, val model: String) : Message
data class Welcome(val sessionId: Bytes, val host: Host, val codec: Codec, val encoder: Encoder, val width: Int, val height: Int, val orientation: Orientation, val fps: Int, val bitrateKbps: Long, val editorState: EditorStateValue) : Message
data class Reject(val reason: RejectReason) : Message
data class VideoAttach(val sessionId: Bytes) : Message
data class Command(val cmd: Cmd) : Message
data class CommandResult(val cmd: Cmd, val result: Result, val editorState: EditorStateValue) : Message
data class EditorState(val state: EditorStateValue) : Message
data class Touch(val touchSeq: Long, val eventTimeUs: Long, val pointers: List<Pointer>) : Message
data object RequestKeyframe : Message
data class DecoderStats(val pendingOutputs: Int, val droppedDisplays: Int) : Message
data class StreamConfigChanged(val width: Int, val height: Int, val orientation: Orientation, val fps: Int, val bitrateKbps: Long) : Message
data class StreamPaused(val reason: PauseReason) : Message
data class Error(val source: ErrorSource, val code: ErrorCode) : Message
data class FrameAck(val frameSeq: Long) : Message
data class Ping(val timeUs: Long) : Message
data class Pong(val timeUs: Long) : Message
data class Bye(val reason: ByeReason) : Message
data class VideoFrame(val frameSeq: Long, val lastTouchSeq: Long, val ptsUs: Long, val flags: VideoFlags, val data: Bytes) : Message
data class LinkHello(val linkToken: Bytes, val processId: Long) : Message
data class LinkStart(val maxW: Int, val maxH: Int, val maxFps: Int, val bitrateKbps: Long) : Message
data object LinkStop : Message
data class Unknown(val type: Int, val length: Long) : Message
enum class DecodeStatus { OK, NEED_MORE, TOO_LARGE, UNKNOWN_TYPE, BAD_SIZE, BAD_ENUM }
data class DecodeResult(val status: DecodeStatus, val consumed: Int = 0, val message: Message? = null)
private class Writer {
    val out = ByteArrayOutputStream()
    fun u8(v: Int) { out.write(v) }
    fun u16(v: Int) { u8(v); u8(v ushr 8) }
    fun u32(v: Long) { repeat(4) { u8((v ushr (8 * it)).toInt()) } }
    fun u64(v: Long) { repeat(8) { u8((v ushr (8 * it)).toInt()) } }
    fun f32(v: Float) { u32(v.toRawBits().toLong()) }
    fun bytes(v: Bytes) { out.write(v.toByteArray()) }
}
fun encode(message: Message): ByteArray {
    val w = Writer()
    val type = when (val m = message) {
        is Hello -> {
            require(m.token.size == 32 && m.decoders.size <= 255)
            val model = m.model.toByteArray(Charsets.UTF_8)
            require(model.size <= 65535)
            w.u16(m.protoVersion); w.bytes(m.token); w.u16(m.screenW); w.u16(m.screenH); w.u16(m.dpi); w.u8(m.refreshHz)
            w.u8(m.decoders.size); w.u16(model.size)
            m.decoders.forEach { d -> w.u8(d.codec.wire); w.u8(d.profile.wire); w.u8(d.level); w.u16(d.maxW); w.u16(d.maxH) }
            w.out.write(model)
            1
        }
        is Welcome -> {
            require(m.sessionId.size == 16)
            w.bytes(m.sessionId)
            w.u8(m.host.wire)
            w.u8(m.codec.wire)
            w.u8(m.encoder.wire)
            w.u16(m.width)
            w.u16(m.height)
            w.u8(m.orientation.wire)
            w.u8(m.fps)
            w.u32(m.bitrateKbps)
            w.u8(m.editorState.wire)
            2
        }
        is Reject -> {
            w.u8(m.reason.wire)
            3
        }
        is VideoAttach -> {
            require(m.sessionId.size == 16)
            w.bytes(m.sessionId)
            4
        }
        is Command -> {
            w.u8(m.cmd.wire)
            16
        }
        is CommandResult -> {
            w.u8(m.cmd.wire)
            w.u8(m.result.wire)
            w.u8(m.editorState.wire)
            17
        }
        is EditorState -> {
            w.u8(m.state.wire)
            18
        }
        is Touch -> {
            w.u32(m.touchSeq); w.u64(m.eventTimeUs); w.u8(m.pointers.size)
            m.pointers.forEach { d -> w.u8(d.id); w.u8(d.phase.wire); w.f32(d.x); w.f32(d.y); w.f32(d.pressure) }
            32
        }
        RequestKeyframe -> {
            48
        }
        is DecoderStats -> {
            w.u16(m.pendingOutputs)
            w.u16(m.droppedDisplays)
            49
        }
        is StreamConfigChanged -> {
            w.u16(m.width)
            w.u16(m.height)
            w.u8(m.orientation.wire)
            w.u8(m.fps)
            w.u32(m.bitrateKbps)
            50
        }
        is StreamPaused -> {
            w.u8(m.reason.wire)
            51
        }
        is Error -> {
            w.u8(m.source.wire)
            w.u16(m.code.wire)
            52
        }
        is FrameAck -> {
            w.u32(m.frameSeq)
            53
        }
        is Ping -> {
            w.u64(m.timeUs)
            64
        }
        is Pong -> {
            w.u64(m.timeUs)
            65
        }
        is Bye -> {
            w.u8(m.reason.wire)
            240
        }
        is VideoFrame -> {
            w.u32(m.frameSeq)
            w.u32(m.lastTouchSeq)
            w.u64(m.ptsUs)
            w.u8(m.flags.wire)
            w.bytes(m.data)
            256
        }
        is LinkHello -> {
            require(m.linkToken.size == 32)
            w.bytes(m.linkToken)
            w.u32(m.processId)
            512
        }
        is LinkStart -> {
            w.u16(m.maxW)
            w.u16(m.maxH)
            w.u8(m.maxFps)
            w.u32(m.bitrateKbps)
            513
        }
        LinkStop -> {
            514
        }
        is Unknown -> { w.out.write(ByteArray(m.length.toInt())); m.type }
    }
    val payload = w.out.toByteArray()
    return ByteArray(HEADER_SIZE + payload.size).also {
        encodeHeader(Header(type, payload.size.toLong()), it)
        payload.copyInto(it, HEADER_SIZE)
    }
}
private class Reader(val b: ByteBuffer) {
    var enums = true
    fun u8(): Int = b.get().toInt() and 255
    fun u16(): Int = b.short.toInt() and 65535
    fun u32(): Long = b.int.toLong() and 0xFFFFFFFFL
    fun u64(): Long = b.long
    fun f32(): Float = Float.fromBits(b.int)
    fun bytes(n: Int): Bytes = Bytes(ByteArray(n).also { b.get(it) })
    inline fun <reified T : Enum<T>> enumeration(wide: Boolean = false, wire: (T) -> Int): T {
        val v = if (wide) u16() else u8()
        return enumValues<T>().firstOrNull { wire(it) == v } ?: enumValues<T>()[0].also { enums = false }
    }
}
fun decode(data: ByteArray, offset: Int, size: Int, channel: Channel): DecodeResult {
    require(offset >= 0 && size >= 0 && offset <= data.size && size <= data.size - offset)
    val header = when (val h = decodeHeader(data, offset, size, channel)) {
        HeaderResult.NeedMore -> return DecodeResult(DecodeStatus.NEED_MORE)
        HeaderResult.TooLarge -> return DecodeResult(DecodeStatus.TOO_LARGE)
        is HeaderResult.Ok -> h.header
    }
    val total = HEADER_SIZE + header.length.toInt()
    if (size < total) return DecodeResult(DecodeStatus.NEED_MORE)
    val r = Reader(ByteBuffer.wrap(data, offset + HEADER_SIZE, header.length.toInt()).slice().order(ByteOrder.LITTLE_ENDIAN))
    try {
        val message: Message = when (header.type) {
            1 -> {
                val protoVersion = r.u16()
                val token = r.bytes(32)
                val screenW = r.u16()
                val screenH = r.u16()
                val dpi = r.u16()
                val refreshHz = r.u8()
                val count = r.u8(); val modelLen = r.u16()
                if (header.length != 44L + 7L * count + modelLen) return DecodeResult(DecodeStatus.BAD_SIZE, total)
                val decoders = List(count) {
                    val d = Decoder(r.enumeration<Codec> { it.wire }, r.enumeration<Profile> { it.wire }, r.u8(), r.u16(), r.u16())
                    r.enums = d.level in 9..62 && r.enums
                    d
                }
                val model = String(r.bytes(modelLen).toByteArray(), Charsets.UTF_8)
                Hello(protoVersion, token, screenW, screenH, dpi, refreshHz, decoders, model)
            }
            2 -> Welcome(r.bytes(16), r.enumeration<Host> { it.wire }, r.enumeration<Codec> { it.wire }, r.enumeration<Encoder> { it.wire }, r.u16(), r.u16(), r.enumeration<Orientation> { it.wire }, r.u8(), r.u32(), r.enumeration<EditorStateValue> { it.wire })
            3 -> Reject(r.enumeration<RejectReason> { it.wire })
            4 -> VideoAttach(r.bytes(16))
            16 -> Command(r.enumeration<Cmd> { it.wire })
            17 -> CommandResult(r.enumeration<Cmd> { it.wire }, r.enumeration<Result> { it.wire }, r.enumeration<EditorStateValue> { it.wire })
            18 -> EditorState(r.enumeration<EditorStateValue> { it.wire })
            32 -> {
                val touchSeq = r.u32(); val eventTimeUs = r.u64(); val count = r.u8()
                if (header.length != 13L + 14L * count) return DecodeResult(DecodeStatus.BAD_SIZE, total)
                r.enums = count in 1..10
                Touch(touchSeq, eventTimeUs, List(count) { Pointer(r.u8(), r.enumeration<Phase> { it.wire }, r.f32(), r.f32(), r.f32()) })
            }
            48 -> RequestKeyframe
            49 -> DecoderStats(r.u16(), r.u16())
            50 -> StreamConfigChanged(r.u16(), r.u16(), r.enumeration<Orientation> { it.wire }, r.u8(), r.u32())
            51 -> StreamPaused(r.enumeration<PauseReason> { it.wire })
            52 -> Error(r.enumeration<ErrorSource> { it.wire }, r.enumeration<ErrorCode>(wide = true) { it.wire })
            53 -> FrameAck(r.u32())
            64 -> Ping(r.u64())
            65 -> Pong(r.u64())
            240 -> Bye(r.enumeration<ByeReason> { it.wire })
            256 -> VideoFrame(r.u32(), r.u32(), r.u64(), r.enumeration<VideoFlags> { it.wire }, r.bytes(r.b.remaining()))
            512 -> LinkHello(r.bytes(32), r.u32())
            513 -> LinkStart(r.u16(), r.u16(), r.u8(), r.u32())
            514 -> LinkStop
            else -> return DecodeResult(DecodeStatus.UNKNOWN_TYPE, total, Unknown(header.type, header.length))
        }
        if (r.b.hasRemaining()) return DecodeResult(DecodeStatus.BAD_SIZE, total)
        if (!r.enums) return DecodeResult(DecodeStatus.BAD_ENUM, total)
        return DecodeResult(DecodeStatus.OK, total, message)
    } catch (_: java.nio.BufferUnderflowException) {
        return DecodeResult(DecodeStatus.BAD_SIZE, total)
    }
}
