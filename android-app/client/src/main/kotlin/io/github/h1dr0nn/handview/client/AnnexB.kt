package io.github.h1dr0nn.handview.client

object AnnexB {
    fun split(data: ByteArray): List<ByteArray> {
        val starts = mutableListOf<Pair<Int, Int>>()
        var i = 0
        while (i + 2 < data.size) {
            val length = when {
                data[i] != 0.toByte() || data[i + 1] != 0.toByte() -> 0
                data[i + 2] == 1.toByte() -> 3
                i + 3 < data.size && data[i + 2] == 0.toByte() && data[i + 3] == 1.toByte() -> 4
                else -> 0
            }
            if (length > 0) { starts.add(i to length); i += length } else i++
        }
        return starts.mapIndexedNotNull { index, (offset, length) ->
            val end = starts.getOrNull(index + 1)?.first ?: data.size
            if (end > offset + length) data.copyOfRange(offset + length, end) else null
        }
    }
    fun sps(data: ByteArray) = split(data).firstOrNull { it[0].toInt() and 31 == 7 }
    fun pps(data: ByteArray) = split(data).firstOrNull { it[0].toInt() and 31 == 8 }
    fun isIdr(data: ByteArray) = split(data).any { it[0].toInt() and 31 == 5 }
    fun csd(nal: ByteArray) = byteArrayOf(0, 0, 0, 1) + nal
}
