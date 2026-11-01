package io.github.h1dr0nn.handview.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

// Same cases and byte dumps as native/core/tests/test_wire.cpp.
class WireTest {

    @Test
    fun headerRoundTrip() {
        val buf = ByteArray(HEADER_SIZE)
        encodeHeader(Header(0x0100, 0x00012345L), buf)
        val expected = byteArrayOf(0x00, 0x01, 0x45, 0x23, 0x01, 0x00)
        assertArrayEquals(expected, buf)

        val result = decodeHeader(buf, 0, buf.size, Channel.VIDEO)
        assertEquals(HeaderResult.Ok(Header(0x0100, 0x00012345L)), result)
    }

    @Test
    fun headerNeedMore() {
        val buf = ByteArray(HEADER_SIZE)
        assertEquals(HeaderResult.NeedMore, decodeHeader(buf, 0, HEADER_SIZE - 1, Channel.CONTROL))
    }

    @Test
    fun headerLengthLimits() {
        val buf = ByteArray(HEADER_SIZE)
        fun decode(length: Long, channel: Channel): HeaderResult {
            encodeHeader(Header(0x0010, length), buf)
            return decodeHeader(buf, 0, buf.size, channel)
        }

        assertTrue(decode(Channel.CONTROL.maxPayload, Channel.CONTROL) is HeaderResult.Ok)
        assertEquals(HeaderResult.TooLarge, decode(Channel.CONTROL.maxPayload + 1, Channel.CONTROL))
        assertTrue(decode(Channel.VIDEO.maxPayload, Channel.VIDEO) is HeaderResult.Ok)
        assertEquals(HeaderResult.TooLarge, decode(Channel.VIDEO.maxPayload + 1, Channel.VIDEO))
    }

    @Test
    fun littleEndianFields() {
        val buf = ByteArray(8)
        writeU64(buf, 0, 0x0102030405060708L)
        assertEquals(0x08.toByte(), buf[0])
        assertEquals(0x01.toByte(), buf[7])
        assertEquals(0x0102030405060708L, readU64(buf, 0))

        // Unaligned access, and u32 values above Int.MAX_VALUE.
        val odd = byteArrayOf(0xFF.toByte(), 0x78, 0x56, 0x34, 0x12)
        assertEquals(0x12345678L, readU32(odd, 1))
        writeU32(buf, 0, 0xFFFFFFFFL)
        assertEquals(0xFFFFFFFFL, readU32(buf, 0))
    }

    @Test
    fun seqSerialCompare() {
        assertTrue(seqNewerOrEqual(5, 5))
        assertTrue(seqNewerOrEqual(6, 5))
        assertFalse(seqNewerOrEqual(5, 6))
        // Wraparound: 1 comes after 0xFFFFFFFF.
        assertTrue(seqNewerOrEqual(1, 0xFFFFFFFF.toInt()))
        assertFalse(seqNewerOrEqual(0xFFFFFFFF.toInt(), 1))
        assertTrue(seqNewerOrEqual(0x7FFFFFFF, 0))
        assertFalse(seqNewerOrEqual(0x80000000.toInt(), 0))
    }
}
