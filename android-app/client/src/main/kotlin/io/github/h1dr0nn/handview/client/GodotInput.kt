package io.github.h1dr0nn.handview.client

// Phone input in Godot's terms, so the game sees what Godot's own Android
// port would give it.
object GodotKeys {
    // Godot's Key enum: printable keys use their (uppercase) ASCII code,
    // special keys sit above KEY_SPECIAL.
    private const val SPECIAL = 1L shl 22
    const val ESCAPE = SPECIAL or 0x01
    const val TAB = SPECIAL or 0x02
    const val BACKSPACE = SPECIAL or 0x04
    const val ENTER = SPECIAL or 0x05
    const val DELETE = SPECIAL or 0x08
    const val HOME = SPECIAL or 0x0D
    const val END = SPECIAL or 0x0E
    const val LEFT = SPECIAL or 0x0F
    const val UP = SPECIAL or 0x10
    const val RIGHT = SPECIAL or 0x11
    const val DOWN = SPECIAL or 0x12
    const val SHIFT = SPECIAL or 0x15
    const val CTRL = SPECIAL or 0x16
    const val ALT = SPECIAL or 0x18

    // Android KeyEvent key codes (android.view.KeyEvent) for keys without a
    // character; letters, digits and symbols go through fromChar.
    private val special = mapOf(
        111 to ESCAPE, 61 to TAB, 67 to BACKSPACE, 66 to ENTER, 160 to ENTER, 112 to DELETE,
        122 to HOME, 123 to END, 21 to LEFT, 19 to UP, 22 to RIGHT, 20 to DOWN,
        59 to SHIFT, 60 to SHIFT, 113 to CTRL, 114 to CTRL, 57 to ALT, 58 to ALT,
    )

    // Godot key for an Android key code, or null when it has none.
    fun fromAndroid(keyCode: Int): Long? = special[keyCode] ?: when (keyCode) {
        in 29..54 -> 'A'.code.toLong() + (keyCode - 29)
        in 7..16 -> '0'.code.toLong() + (keyCode - 7)
        62 -> ' '.code.toLong()
        else -> null
    }

    // Godot key for a typed character: ASCII keys by code (letters uppercase),
    // anything else 0 (KEY_NONE) and only the character carries meaning.
    fun fromChar(c: Char): Long = when {
        c == '\n' -> ENTER
        c == '\t' -> TAB
        c.code in 32..126 -> c.uppercaseChar().code.toLong()
        else -> 0L
    }
}

object GodotSensors {
    // Display rotation in quarter turns, as Surface.ROTATION_0..270.
    // Rotates a device-axes reading into screen axes like Godot's Android port,
    // then applies its sign convention: accelerometer, gravity and magnetometer
    // negated, gyroscope as is.
    fun fromAndroid(values: FloatArray, rotation: Int, negate: Boolean): FloatArray {
        val x = values[0]; val y = values[1]; val z = values[2]
        val (rx, ry) = when (rotation) {
            1 -> -y to x
            2 -> -x to -y
            3 -> y to -x
            else -> x to y
        }
        val sign = if (negate) -1f else 1f
        return floatArrayOf(sign * rx, sign * ry, sign * z)
    }
}
