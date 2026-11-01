package io.github.h1dr0nn.handview

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathBuilder
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.unit.dp

// Icons drawn on a 24 x 24 grid; tinted by the Icon composable.
object Icons {
    private fun filled(name: String, block: PathBuilder.() -> Unit) =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f).apply {
            path(fill = SolidColor(Color.Black), strokeLineJoin = StrokeJoin.Round, pathBuilder = block)
        }.build()

    private fun stroked(name: String, width: Float = 1.8f, block: PathBuilder.() -> Unit) =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f).apply {
            path(stroke = SolidColor(Color.Black), strokeLineWidth = width, strokeLineCap = StrokeCap.Round, strokeLineJoin = StrokeJoin.Round, pathBuilder = block)
        }.build()

    val Play = filled("Play") { moveTo(8f, 5f); lineTo(19f, 12f); lineTo(8f, 19f); close() }
    val Pause = filled("Pause") {
        moveTo(7f, 5f); horizontalLineToRelative(3.5f); verticalLineToRelative(14f); horizontalLineToRelative(-3.5f); close()
        moveTo(13.5f, 5f); horizontalLineToRelative(3.5f); verticalLineToRelative(14f); horizontalLineToRelative(-3.5f); close()
    }
    val Step = filled("Step") {
        moveTo(6f, 5f); lineTo(15f, 12f); lineTo(6f, 19f); close()
        moveTo(16.5f, 5f); horizontalLineToRelative(2.5f); verticalLineToRelative(14f); horizontalLineToRelative(-2.5f); close()
    }
    val Stop = filled("Stop") { moveTo(6.5f, 6.5f); horizontalLineToRelative(11f); verticalLineToRelative(11f); horizontalLineToRelative(-11f); close() }
    val Close = stroked("Close", 2.2f) { moveTo(6.5f, 6.5f); lineTo(17.5f, 17.5f); moveTo(17.5f, 6.5f); lineTo(6.5f, 17.5f) }
    val Back = stroked("Back", 2.2f) { moveTo(14.5f, 5.5f); lineTo(8f, 12f); lineTo(14.5f, 18.5f) }
    val Usb = stroked("Usb") {
        moveTo(12f, 3.5f); lineTo(12f, 17f)
        moveTo(9.5f, 6f); lineTo(12f, 3.5f); lineTo(14.5f, 6f)
        moveTo(12f, 13.5f); lineTo(7.5f, 11f); lineTo(7.5f, 8.5f)
        moveTo(12f, 15.5f); lineTo(16.5f, 13f); lineTo(16.5f, 10f)
        moveTo(12f, 17f); arcToRelative(1.8f, 1.8f, 0f, true, true, 0f, 3.6f); arcToRelative(1.8f, 1.8f, 0f, true, true, 0f, -3.6f)
    }
    val Wifi = stroked("Wifi") {
        moveTo(3.5f, 9.5f); quadTo(12f, 2f, 20.5f, 9.5f)
        moveTo(6.5f, 12.8f); quadTo(12f, 8f, 17.5f, 12.8f)
        moveTo(9.5f, 16f); quadTo(12f, 14f, 14.5f, 16f)
        moveTo(12f, 19.2f); lineTo(12f, 19.3f)
    }
    val Qr = stroked("Qr") {
        moveTo(4f, 8.5f); lineTo(4f, 4f); lineTo(8.5f, 4f)
        moveTo(15.5f, 4f); lineTo(20f, 4f); lineTo(20f, 8.5f)
        moveTo(20f, 15.5f); lineTo(20f, 20f); lineTo(15.5f, 20f)
        moveTo(8.5f, 20f); lineTo(4f, 20f); lineTo(4f, 15.5f)
        moveTo(8.5f, 8.5f); lineTo(10.5f, 8.5f); lineTo(10.5f, 10.5f); lineTo(8.5f, 10.5f); close()
        moveTo(13.5f, 13.5f); lineTo(15.5f, 13.5f); lineTo(15.5f, 15.5f); lineTo(13.5f, 15.5f); close()
        moveTo(13.5f, 8.5f); lineTo(15.5f, 8.5f)
        moveTo(8.5f, 15.5f); lineTo(10.5f, 13.5f)
    }
    val Settings = stroked("Settings") {
        moveTo(4f, 7f); lineTo(6.8f, 7f); moveTo(11.2f, 7f); lineTo(20f, 7f)
        moveTo(4f, 17f); lineTo(12.8f, 17f); moveTo(17.2f, 17f); lineTo(20f, 17f)
        moveTo(9f, 4.8f); arcToRelative(2.2f, 2.2f, 0f, true, true, 0f, 4.4f); arcToRelative(2.2f, 2.2f, 0f, true, true, 0f, -4.4f)
        moveTo(15f, 14.8f); arcToRelative(2.2f, 2.2f, 0f, true, true, 0f, 4.4f); arcToRelative(2.2f, 2.2f, 0f, true, true, 0f, -4.4f)
    }
}
