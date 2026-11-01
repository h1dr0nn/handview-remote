package io.github.h1dr0nn.handview.client

object TouchMapper {
    data class Point(val x: Float, val y: Float)
    fun map(x: Float, y: Float, viewW: Int, viewH: Int, streamW: Int, streamH: Int): Point {
        require(viewW > 0 && viewH > 0 && streamW > 0 && streamH > 0)
        val scale = minOf(viewW.toFloat() / streamW, viewH.toFloat() / streamH)
        val w = streamW * scale; val h = streamH * scale
        return Point(((x - (viewW - w) / 2) / w).coerceIn(0f, 1f), ((y - (viewH - h) / 2) / h).coerceIn(0f, 1f))
    }
}
