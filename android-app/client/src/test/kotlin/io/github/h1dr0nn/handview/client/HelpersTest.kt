package io.github.h1dr0nn.handview.client

import org.junit.Assert.*
import org.junit.Test

class HelpersTest {
    @Test fun annexMixedStarts() {
        val bytes = byteArrayOf(0,0,0,1,0x67,2,0,0,1,0x68,3,0,0,1,0x65,4)
        assertEquals(3, AnnexB.split(bytes).size)
        assertArrayEquals(byteArrayOf(0x67,2), AnnexB.sps(bytes))
        assertArrayEquals(byteArrayOf(0x68,3), AnnexB.pps(bytes))
        assertTrue(AnnexB.isIdr(bytes))
    }
    @Test fun annexEmptyAndMissing() {
        assertTrue(AnnexB.split(byteArrayOf(1,2)).isEmpty())
        assertTrue(AnnexB.split(byteArrayOf(0,0,1)).isEmpty())
        assertNull(AnnexB.sps(byteArrayOf(0,0,1,0x61)))
        assertFalse(AnnexB.isIdr(byteArrayOf(0,0,1,0x61)))
    }
    @Test fun touchLetterboxAndClamp() {
        assertEquals(TouchMapper.Point(.5f,.5f), TouchMapper.map(100f,100f,200,200,200,100))
        assertEquals(TouchMapper.Point(0f,0f), TouchMapper.map(-10f,10f,200,200,200,100))
        assertEquals(TouchMapper.Point(1f,1f), TouchMapper.map(300f,200f,200,200,200,100))
    }
    @Test fun touchPillarbox() {
        assertEquals(TouchMapper.Point(0f,.5f), TouchMapper.map(0f,50f,200,100,100,100))
        assertEquals(TouchMapper.Point(.5f,.5f), TouchMapper.map(100f,50f,200,100,100,100))
    }
    @Test fun latencyResolvesAllOlderTouches() {
        val t = LatencyTracker()
        t.record(1,0); t.record(2,1_000_000); t.record(3,2_000_000)
        t.onFrameRendered(2,10_000_000)
        val s = t.summary(10_000_000)!!
        assertEquals(9.0,s.p50,0.0); assertEquals(10.0,s.p95,0.0)
        t.onFrameRendered(3,20_000_000)
        assertEquals(18.0,t.summary(20_000_000)!!.max,0.0)
    }
    @Test fun latencyWrapAndNoDuplicateSamples() {
        val t = LatencyTracker()
        t.record(0xffffffffL,0); t.record(1,0)
        t.onFrameRendered(0,1_000_000)
        assertNull(t.summary(1_000_000))
        t.onFrameRendered(1,3_000_000)
        t.onFrameRendered(1,100_000_000)
        assertEquals(3.0,t.summary(100_000_000)!!.max,0.0)
    }
    @Test fun latencyExpiresAndClears() {
        val t = LatencyTracker(); t.record(1,0); t.onFrameRendered(1,1)
        assertNull(t.summary(10_000_000_002L))
        t.record(2,0); t.clear(); t.onFrameRendered(2,10)
        assertNull(t.summary(10))
    }
    @Test fun latencyCapsAt500() {
        val t = LatencyTracker()
        for (i in 1..501) { t.record(i.toLong(),0); t.onFrameRendered(i.toLong(),i * 1_000_000L) }
        assertEquals(251.0,t.summary(501_000_000)!!.p50,0.0)
    }
    @Test fun latencyPendingCapsAt256() {
        val t = LatencyTracker()
        for (i in 1..300) t.record(i.toLong(), i * 1_000_000L)
        t.onFrameRendered(300,301_000_000)
        assertEquals(256.0,t.summary(301_000_000)!!.max,0.0)
    }
    @Test fun latencyEvictsPendingBeforeRecordingAndRendering() {
        val t = LatencyTracker()
        t.record(1,0); t.record(2,5_000_000_001L)
        t.onFrameRendered(2,5_001_000_001L)
        assertEquals(1.0,t.summary(5_001_000_001L)!!.max,0.0)
        t.clear(); t.record(3,0); t.onFrameRendered(3,5_000_000_001L)
        assertNull(t.summary(5_000_000_001L))
    }
    @Test fun hexValidation() {
        assertArrayEquals(ByteArray(32) { 0xab.toByte() }, SessionClient.decodeToken("Ab".repeat(32)))
        assertThrows(IllegalArgumentException::class.java) { SessionClient.decodeToken("x".repeat(64)) }
        assertThrows(IllegalArgumentException::class.java) { SessionClient.decodeToken("00") }
    }
}
