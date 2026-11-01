package io.github.h1dr0nn.handview

import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.*
import androidx.compose.runtime.*
import io.github.h1dr0nn.handview.client.*
import io.github.h1dr0nn.handview.protocol.*
import kotlinx.coroutines.flow.MutableStateFlow

class MainActivity : ComponentActivity() {
    private val state = MutableStateFlow(CastState())
    private val main = Handler(Looper.getMainLooper())
    private val latency = LatencyTracker()
    private var client: SessionClient? = null
    @Volatile private var decoder: VideoDecoder? = null
    private var token: String? = null
    @Volatile private var generation = 0
    private var touchSeq = 0L
    private var frames = 0
    private var fpsSince = System.nanoTime()
    private val refresh = object : Runnable {
        override fun run() {
            val now = System.nanoTime()
            val elapsed = (now - fpsSince) / 1_000_000_000.0
            state.value = state.value.copy(latency = latency.summary(), fps = (frames / elapsed).toInt())
            frames = 0; fpsSince = now
            main.postDelayed(this, 1000)
        }
    }
    @Suppress("DEPRECATION")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        main.post(refresh)
        setContent {
            val ui by state.collectAsState()
            MaterialTheme(colorScheme = darkColorScheme()) {
                HandviewScreen(ui, ::command, ::disconnect, ::surfaceView)
            }
        }
        launch(intent)
    }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); launch(intent) }
    private fun launch(intent: Intent) {
        val next = intent.getStringExtra("token") ?: return
        if (next == token) return
        disconnect()
        val port = intent.getIntExtra("port", 0)
        if (port !in 1..65535 || runCatching { SessionClient.decodeToken(next) }.isFailure) {
            state.value = CastState(message = R.string.invalid_session); return
        }
        token = next; touchSeq = 0; frames = 0; fpsSince = System.nanoTime()
        state.value = CastState(connecting = true)
        val current = generation
        Thread({
            val greeting = runCatching { deviceHello(this) }
            main.post {
                if (current != generation) return@post
                val hello = greeting.getOrElse { end(R.string.device_error); return@post }
                val session = SessionClient(port, next, hello, { received ->
                    val frame = (received as? SessionClient.Event.Received)?.message as? VideoFrame
                    if (frame != null) {
                        if (current == generation) decoder?.frame(frame)
                    } else {
                        if (current == generation) {
                            when (received) {
                                is SessionClient.Event.Connected -> decoder?.configure(received.welcome.width, received.welcome.height)
                                is SessionClient.Event.Received -> (received.message as? StreamConfigChanged)?.let { decoder?.configure(it.width, it.height) }
                                else -> Unit
                            }
                        }
                        main.post { if (current == generation) event(received) }
                    }
                })
                client = session
                decoder = VideoDecoder(session, latency) { main.post { if (current == generation) frames++ } }
                session.start()
            }
        }, "handview-device-info").start()
    }

    private fun event(event: SessionClient.Event) {
        when (event) {
            is SessionClient.Event.Connected -> {
                val w = event.welcome
                state.value = CastState(welcome = w, editor = w.editorState, width = w.width, height = w.height, orientation = w.orientation)
                orient(w.orientation)
            }
            is SessionClient.Event.Rejected -> end(when (event.reason) {
                RejectReason.BadToken -> R.string.reject_token
                RejectReason.BadVersion -> R.string.reject_version
                RejectReason.Busy -> R.string.reject_busy
            })
            is SessionClient.Event.Disconnected -> end(when (val reason = event.reason) {
                SessionClient.DisconnectReason.Timeout -> R.string.ended_timeout
                SessionClient.DisconnectReason.ProtocolError -> R.string.ended_protocol
                SessionClient.DisconnectReason.IoError -> R.string.ended_connection
                is SessionClient.DisconnectReason.Remote -> when (reason.reason) {
                    ByeReason.Compiling -> R.string.ended_compiling
                    ByeReason.Reloading -> R.string.ended_reloading
                    ByeReason.Replaced -> R.string.ended_replaced
                    ByeReason.ProtocolError -> R.string.ended_protocol
                    ByeReason.Normal -> R.string.ended_connection
                }
            })
            is SessionClient.Event.Received -> when (val m = event.message) {
                is EditorState -> state.value = state.value.copy(editor = m.state, busy = false)
                is CommandResult -> state.value = state.value.copy(editor = m.editorState, busy = m.result == io.github.h1dr0nn.handview.protocol.Result.Busy)
                is StreamConfigChanged -> {
                    state.value = state.value.copy(width = m.width, height = m.height, orientation = m.orientation)
                    orient(m.orientation)
                }
                is StreamPaused -> state.value = state.value.copy(editor = if (m.reason == PauseReason.Paused) EditorStateValue.Paused else EditorStateValue.Stopped)
                is io.github.h1dr0nn.handview.protocol.Error -> state.value = state.value.copy(message = R.string.stream_error)
                else -> Unit
            }
        }
    }
    private fun orient(value: Orientation) {
        requestedOrientation = if (value == Orientation.Landscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
    }
    private fun command(cmd: Cmd) { client?.sendCommand(cmd) }
    private fun end(message: Int) { disconnect(); state.value = CastState(message = message) }
    private fun disconnect() {
        generation++; decoder?.close(); decoder = null
        client?.close(ByeReason.Normal); client = null; token = null
        latency.clear(); state.value = CastState()
    }
    private fun surfaceView(): SurfaceView = SurfaceView(this).apply {
        holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) { decoder?.setSurface(holder.surface) }
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) { decoder?.setSurface(holder.surface) }
            override fun surfaceDestroyed(holder: SurfaceHolder) { decoder?.setSurface(null) }
        })
        setOnTouchListener { view, motion ->
            if (state.value.editor == EditorStateValue.Playing && motion.pointerCount in 1..10) {
                val ui = state.value
                val pointers = (0 until motion.pointerCount).map { i ->
                    val phase = when (motion.actionMasked) {
                        MotionEvent.ACTION_CANCEL -> Phase.Canceled
                        MotionEvent.ACTION_MOVE -> Phase.Moved
                        MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> if (i == motion.actionIndex) Phase.Began else Phase.Stationary
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> if (i == motion.actionIndex) Phase.Ended else Phase.Stationary
                        else -> Phase.Stationary
                    }
                    val point = TouchMapper.map(motion.getX(i), motion.getY(i), view.width, view.height, ui.width, ui.height)
                    Pointer(motion.getPointerId(i) and 255, phase, point.x, point.y, motion.getPressure(i).coerceIn(0f, 1f))
                }
                val nanos = if (Build.VERSION.SDK_INT >= 34) motion.eventTimeNanos else motion.eventTime * 1_000_000
                touchSeq = (touchSeq + 1) and 0xffffffffL
                latency.record(touchSeq, nanos)
                client?.sendTouch(touchSeq, nanos / 1000, pointers)
                if (motion.actionMasked == MotionEvent.ACTION_UP) view.performClick()
            }
            true
        }
    }
    override fun onDestroy() { main.removeCallbacksAndMessages(null); disconnect(); super.onDestroy() }
}
