package io.github.h1dr0nn.handview.protocol

// Wire Format v1 framing primitives, mirror of native/core wire.hpp.
// Full message codecs land in M1; the layout rules come from protocol/README.md.

const val PROTOCOL_VERSION: Int = 1

/** Every message: [type: u16][length: u32], little-endian, length = payload bytes. */
const val HEADER_SIZE: Int = 6

enum class Channel(val maxPayload: Long) {
    CONTROL(64L * 1024),
    VIDEO(32L * 1024 * 1024),
}

/** [type] holds a u16, [length] holds a u32. */
data class Header(val type: Int, val length: Long)

sealed interface HeaderResult {
    data class Ok(val header: Header) : HeaderResult

    /** Fewer than [HEADER_SIZE] bytes available. */
    data object NeedMore : HeaderResult

    /** Length over the channel limit: close with Bye{ProtocolError}. */
    data object TooLarge : HeaderResult
}

fun encodeHeader(header: Header, out: ByteArray, offset: Int = 0) {
    writeU16(out, offset, header.type)
    writeU32(out, offset + 2, header.length)
}

fun decodeHeader(data: ByteArray, offset: Int, size: Int, channel: Channel): HeaderResult {
    if (size < HEADER_SIZE) return HeaderResult.NeedMore
    val header = Header(readU16(data, offset), readU32(data, offset + 2))
    if (header.length > channel.maxPayload) return HeaderResult.TooLarge
    return HeaderResult.Ok(header)
}

fun writeU16(out: ByteArray, offset: Int, value: Int) {
    out[offset] = value.toByte()
    out[offset + 1] = (value ushr 8).toByte()
}

fun writeU32(out: ByteArray, offset: Int, value: Long) {
    for (i in 0 until 4) out[offset + i] = (value ushr (8 * i)).toByte()
}

fun writeU64(out: ByteArray, offset: Int, value: Long) {
    for (i in 0 until 8) out[offset + i] = (value ushr (8 * i)).toByte()
}

fun readU16(data: ByteArray, offset: Int): Int =
    (data[offset].toInt() and 0xFF) or ((data[offset + 1].toInt() and 0xFF) shl 8)

fun readU32(data: ByteArray, offset: Int): Long {
    var value = 0L
    for (i in 0 until 4) value = value or ((data[offset + i].toLong() and 0xFF) shl (8 * i))
    return value
}

fun readU64(data: ByteArray, offset: Int): Long {
    var value = 0L
    for (i in 0 until 8) value = value or ((data[offset + i].toLong() and 0xFF) shl (8 * i))
    return value
}

/**
 * Serial number comparison for u32 seq (raw bits held in an Int): [a] is newer
 * than or equal to [b] when (int32)(a - b) >= 0. Int subtraction wraps, so this
 * stays correct across wraparound.
 */
fun seqNewerOrEqual(a: Int, b: Int): Boolean = (a - b) >= 0
