package io.github.h1dr0nn.handview.client

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class WifiJoinTest {
    @Test fun pinTokenMatchesTheEditor() {
        // Same value as session_logic.gd pin_token("123456").
        assertEquals("d7a953a33414d2394445619d5e21d3b4185c683a07d9f2cf13f8c3eb07688e05", WifiJoin.pinToken("123456"))
    }

    @Test fun parsesTheDockLink() {
        val token = "ab".repeat(32)
        val ok = WifiJoin.parseLink("handview://wifi?h=192.168.1.10&p=38501&t=$token&n=57696c642050617773")
        assertEquals(WifiJoin.Link("192.168.1.10", 38501, token, "Wild Paws"), ok)
    }

    @Test fun rejectsOtherCodes() {
        assertNull(WifiJoin.parseLink("https://example.com"))
        assertNull(WifiJoin.parseLink("handview://wifi?h=host&p=1&t=${"ab".repeat(32)}"))
        assertNull(WifiJoin.parseLink("handview://wifi?h=10.0.0.1&p=1&t=abc"))
    }

    @Test fun parsesTypedAddresses() {
        assertEquals("10.0.0.5" to 38500, WifiJoin.parseAddress("10.0.0.5"))
        assertEquals("10.0.0.5" to 38502, WifiJoin.parseAddress(" 10.0.0.5:38502 "))
        assertNull(WifiJoin.parseAddress("10.0.0"))
        assertNull(WifiJoin.parseAddress("10.0.0.5:99999"))
    }

    @Test fun parsesBeacons() {
        assertEquals(WifiJoin.Beacon("Wild Paws", 38500), WifiJoin.parseBeacon("""{"handview":1,"hosts":["192.168.1.10"],"name":"Wild Paws","port":38500}"""))
        assertNull(WifiJoin.parseBeacon("""{"other":1,"port":38500}"""))
    }
}
