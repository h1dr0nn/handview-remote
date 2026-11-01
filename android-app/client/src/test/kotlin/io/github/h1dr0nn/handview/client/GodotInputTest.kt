package io.github.h1dr0nn.handview.client

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GodotInputTest {
    @Test fun lettersDigitsAndSpaceMapToAscii() {
        assertEquals('A'.code.toLong(), GodotKeys.fromAndroid(29))
        assertEquals('Z'.code.toLong(), GodotKeys.fromAndroid(54))
        assertEquals('0'.code.toLong(), GodotKeys.fromAndroid(7))
        assertEquals(' '.code.toLong(), GodotKeys.fromAndroid(62))
    }

    @Test fun specialKeysUseGodotCodes() {
        assertEquals(4194309L, GodotKeys.fromAndroid(66))
        assertEquals(4194308L, GodotKeys.fromAndroid(67))
        assertEquals(4194305L, GodotKeys.fromAndroid(111))
        assertEquals(4194319L, GodotKeys.fromAndroid(21))
        assertNull(GodotKeys.fromAndroid(24))
    }

    @Test fun charactersMapToKeys() {
        assertEquals('A'.code.toLong(), GodotKeys.fromChar('a'))
        assertEquals('!'.code.toLong(), GodotKeys.fromChar('!'))
        assertEquals(GodotKeys.ENTER, GodotKeys.fromChar('\n'))
        assertEquals(0L, GodotKeys.fromChar('é'))
    }

    @Test fun sensorsFollowGodotAndroidConvention() {
        val reading = floatArrayOf(1f, 2f, 3f)
        assertArrayEquals(floatArrayOf(-1f, -2f, -3f), GodotSensors.fromAndroid(reading, 0, negate = true), 0f)
        assertArrayEquals(floatArrayOf(-2f, 1f, 3f), GodotSensors.fromAndroid(reading, 1, negate = false), 0f)
        assertArrayEquals(floatArrayOf(-1f, -2f, 3f), GodotSensors.fromAndroid(reading, 2, negate = false), 0f)
        assertArrayEquals(floatArrayOf(2f, -1f, 3f), GodotSensors.fromAndroid(reading, 3, negate = false), 0f)
    }
}
