package io.github.h1dr0nn.handview

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.view.WindowManager
import io.github.h1dr0nn.handview.client.GodotSensors
import io.github.h1dr0nn.handview.protocol.SensorKind

// Streams the phone's motion sensors to the game while a session runs. The
// Accelerometer setting covers accelerometer, gravity and magnetometer; the
// Gyroscope setting covers the gyroscope.
class PhoneSensors(private val context: Context, private val send: (io.github.h1dr0nn.handview.protocol.Sensor) -> Unit) : SensorEventListener {
    private val manager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val kinds = mapOf(
        Sensor.TYPE_ACCELEROMETER to SensorKind.Accelerometer,
        Sensor.TYPE_GRAVITY to SensorKind.Gravity,
        Sensor.TYPE_MAGNETIC_FIELD to SensorKind.Magnetometer,
        Sensor.TYPE_GYROSCOPE to SensorKind.Gyroscope,
    )

    fun start(accelerometer: Boolean, gyroscope: Boolean) {
        stop()
        val types = buildList {
            if (accelerometer) addAll(listOf(Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GRAVITY, Sensor.TYPE_MAGNETIC_FIELD))
            if (gyroscope) add(Sensor.TYPE_GYROSCOPE)
        }
        types.forEach { type -> manager.getDefaultSensor(type)?.let { manager.registerListener(this, it, SensorManager.SENSOR_DELAY_GAME) } }
    }

    fun stop() = manager.unregisterListener(this)

    @Suppress("DEPRECATION")
    override fun onSensorChanged(event: SensorEvent) {
        val kind = kinds[event.sensor.type] ?: return
        val rotation = (context.getSystemService(Context.WINDOW_SERVICE) as WindowManager).defaultDisplay.rotation
        val v = GodotSensors.fromAndroid(event.values, rotation, negate = kind != SensorKind.Gyroscope)
        send(io.github.h1dr0nn.handview.protocol.Sensor(kind, v[0], v[1], v[2]))
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
