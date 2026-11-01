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
import androidx.compose.runtime.*
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch
import io.github.h1dr0nn.handview.client.*
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import io.github.h1dr0nn.handview.protocol.*
import kotlinx.coroutines.flow.MutableStateFlow

class MainActivity : ComponentActivity(), Actions {
    private val state = MutableStateFlow(CastState())
    private val app = MutableStateFlow(AppState())
    private lateinit var store: SettingsStore
    private lateinit var sensors: PhoneSensors
    private lateinit var discovery: LanDiscovery
    private var host = "127.0.0.1"
    private var wifi = false
    private val main = Handler(Looper.getMainLooper())
    private val latency = LatencyTracker()
    private var client: SessionClient? = null
    @Volatile private var decoder: VideoDecoder? = null
    private var token: String? = null
    private var port = 0
    // The app opens on Home and joins only when the user taps a project. Offers
    // connect on their own only to rejoin a session that dropped unexpectedly
    // (cable, editor restart), and only with Auto Reconnect on.
    private var autoConnect = false
    private var resumed = false
    private var lastTried: String? = null
    private var view = ViewSize(0, 0)
    private var activeHolder: SurfaceHolder? = null
    @Volatile private var generation = 0
    private var touchSeq = 0L
    private var touchReady = false
    private var renderedStream = -1
    @Volatile private var stream = 0
    private var screenWidth = 1
    private var screenHeight = 1
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
        store = SettingsStore(this)
        sensors = PhoneSensors(this) { client?.sendSensor(it) }
        discovery = LanDiscovery(this)
        app.value = AppState(settings = store.load())
        applyTheme()
        orient(Orientation.Portrait)
        lifecycleScope.launch { discovery.editors.collect { app.value = app.value.copy(lan = it) } }
        lifecycleScope.launch {
            SessionOffers.flow(this@MainActivity).collect { offers ->
                app.value = app.value.copy(offers = offers)
                maybeAutoConnect()
            }
        }
        setContent {
            val ui by state.collectAsState()
            val screens by app.collectAsState()
            HandviewTheme {
                HandviewScreen(ui, screens, this)
            }
        }
        if (savedInstanceState == null || intent.hasExtra("token")) launch(intent)
    }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); launch(intent) }
    override fun onResume() { super.onResume(); resumed = true; discovery.start(); startSensors(); maybeAutoConnect() }
    override fun onPause() { resumed = false; sensors.stop(); discovery.stop(); super.onPause() }
    override fun dismissMessage() {
        app.value = app.value.copy(notice = null)
        state.value = state.value.copy(message = null)
    }
    override fun askPin(editor: LanEditor?) { app.value = app.value.copy(pinFor = editor, notice = null) }
    override fun joinWithPin(editor: LanEditor, pin: String) {
        app.value = app.value.copy(pinFor = null)
        connect(WifiJoin.pinToken(pin), editor.port, editor.host)
    }
    override fun scanQr() {
        val options = GmsBarcodeScannerOptions.Builder().setBarcodeFormats(Barcode.FORMAT_QR_CODE).build()
        GmsBarcodeScanning.getClient(this, options).startScan()
            .addOnSuccessListener { code ->
                val link = code.rawValue?.let(WifiJoin::parseLink)
                if (link == null) app.value = app.value.copy(notice = R.string.scan_not_handview)
                else connect(link.token, link.port, link.host)
            }
            .addOnFailureListener { app.value = app.value.copy(notice = R.string.scan_unavailable) }
    }
    private fun startSensors() {
        val s = app.value.settings
        if (client != null && state.value.welcome != null && (s.accelerometer || s.gyroscope)) sensors.start(s.accelerometer, s.gyroscope) else sensors.stop()
    }
    override fun toggleKeyboard() { state.value = state.value.copy(keyboardOpen = !state.value.keyboardOpen) }
    override fun typeKey(keycode: Long, unicode: Long) {
        client?.sendKey(Key(keycode, unicode, true))
        client?.sendKey(Key(keycode, unicode, false))
    }
    // A hardware keyboard goes to the game too, while one is playing. Back,
    // volume and other keys without a Godot equivalent keep their usual job.
    override fun onKeyDown(keyCode: Int, event: android.view.KeyEvent): Boolean = hardwareKey(event, true) || super.onKeyDown(keyCode, event)
    override fun onKeyUp(keyCode: Int, event: android.view.KeyEvent): Boolean = hardwareKey(event, false) || super.onKeyUp(keyCode, event)
    private fun hardwareKey(event: android.view.KeyEvent, pressed: Boolean): Boolean {
        val playing = client != null && app.value.settings.keyboard && state.value.editor == EditorStateValue.Playing
        val keycode = io.github.h1dr0nn.handview.client.GodotKeys.fromAndroid(event.keyCode)
        if (!playing || keycode == null || state.value.keyboardOpen) return false
        client?.sendKey(Key(keycode, event.unicodeChar.toLong(), pressed))
        return true
    }
    private fun launch(intent: Intent) {
        val next = intent.getStringExtra("token")
        intent.removeExtra("token")
        if (next == null) return
        connect(next, intent.getIntExtra("port", 0))
    }
    private fun maybeAutoConnect() {
        if (!app.value.settings.autoReconnect || !autoConnect || !resumed || client != null) return
        val offer = app.value.offers.firstOrNull() ?: return
        if (offer.token == token || offer.token == lastTried) return
        lastTried = offer.token
        connect(offer.token, offer.port)
    }
    override fun fullScreen(on: Boolean) { state.value = state.value.copy(fullScreen = on) }
    override fun viewSize(view: ViewSize) {
        if (view == this.view) return
        this.view = view
        client?.sendViewSize(view)
    }
    override fun connectTo(offer: SessionOffer) { lastTried = offer.token; connect(offer.token, offer.port) }
    private fun connect(next: String, port: Int, host: String = "127.0.0.1") {
        if (next == token) return
        closeSession()
        if (port !in 1..65535 || runCatching { SessionClient.decodeToken(next) }.isFailure) {
            state.value = CastState(message = R.string.invalid_session); return
        }
        token = next; this.port = port; this.host = host; wifi = host != "127.0.0.1"; touchSeq = 0; frames = 0; fpsSince = System.nanoTime()
        state.value = CastState(connecting = true)
        val current = generation
        val display = runCatching { deviceDisplay(this) }.getOrElse { end(R.string.device_error); return }
        screenWidth = display.width; screenHeight = display.height
        Thread({
            val greeting = runCatching { deviceHello(display) }
            main.post {
                if (current != generation) return@post
                val hello = greeting.getOrElse { end(R.string.device_error); return@post }
                val session = SessionClient(port, next, hello, host = host, listener = { received ->
                    val frame = (received as? SessionClient.Event.Received)?.message as? VideoFrame
                    if (frame != null) {
                        if (current == generation) decoder?.frame(frame)
                    } else {
                        if (current == generation) {
                            when (received) {
                                is SessionClient.Event.Connected -> decoder?.configure(received.welcome.width.takeIf { it > 0 && received.welcome.height > 0 } ?: screenWidth, received.welcome.height.takeIf { it > 0 && received.welcome.width > 0 } ?: screenHeight, stream)
                                is SessionClient.Event.Received -> (received.message as? StreamConfigChanged)?.let { stream++; decoder?.configure(if (it.width > 0 && it.height > 0) it.width else state.value.width, if (it.width > 0 && it.height > 0) it.height else state.value.height, stream) }
                                else -> Unit
                            }
                        }
                        main.post { if (current == generation) event(received) }
                    }
                })
                client = session
                decoder = VideoDecoder(session) { renderedStream, touch, nanos -> main.post {
                    if (current == generation && renderedStream == stream) {
                        frames++
                        if (state.value.editor == EditorStateValue.Playing) {
                            touchReady = true
                            this.renderedStream = renderedStream
                            latency.onFrameRendered(touch, nanos)
                        }
                    }
                } }
                session.start()
            }
        }, "handview-device-info").start()
    }

    private fun event(event: SessionClient.Event) {
        when (event) {
            is SessionClient.Event.Connected -> {
                val w = event.welcome
                // The stage may have been measured before the session existed.
                client?.sendStreamPrefs(app.value.settings.streamPrefs())
                if (view.width > 0 && view.height > 0) client?.sendViewSize(view)
                state.value = CastState(wifi = wifi, keyboard = app.value.settings.keyboard, welcome = w, editor = w.editorState, width = if (w.width > 0 && w.height > 0) w.width else screenWidth, height = if (w.width > 0 && w.height > 0) w.height else screenHeight, orientation = w.orientation)
                orient(w.orientation)
                startSensors()
            }
            is SessionClient.Event.Rejected -> end(when (event.reason) {
                // A stale offer (the editor restarted): forget it, a new one follows.
                RejectReason.BadToken -> { SessionOffers.remove(this, port); R.string.reject_token }
                RejectReason.BadVersion -> R.string.reject_version
                RejectReason.Busy -> R.string.reject_busy
            })
            // Never got a Welcome: the editor behind this offer is gone.
            is SessionClient.Event.Disconnected -> if (state.value.welcome == null && event.reason == SessionClient.DisconnectReason.IoError) {
                SessionOffers.remove(this, port); end(R.string.editor_unreachable)
            } else end(rejoin = (event.reason as? SessionClient.DisconnectReason.Remote)?.reason != ByeReason.Replaced, message = when (val reason = event.reason) {
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
                is EditorState -> { if (m.state != EditorStateValue.Playing) touchReady = false; state.value = state.value.copy(editor = m.state, busy = false) }
                is CommandResult -> { if (m.editorState != EditorStateValue.Playing) touchReady = false; state.value = state.value.copy(editor = m.editorState, busy = m.result == io.github.h1dr0nn.handview.protocol.Result.Busy) }
                is StreamConfigChanged -> {
                    latency.clear(); touchReady = false
                    state.value = state.value.copy(width = if (m.width > 0 && m.height > 0) m.width else state.value.width, height = if (m.width > 0 && m.height > 0) m.height else state.value.height, orientation = m.orientation, latency = null)
                    orient(m.orientation)
                }
                // While reloading, the old run's end is expected: keep showing Reloading.
                is StreamPaused -> {
                    touchReady = false
                    if (state.value.editor != EditorStateValue.Reloading) state.value = state.value.copy(editor = if (m.reason == PauseReason.Paused) EditorStateValue.Paused else EditorStateValue.Stopped)
                    Unit
                }
                is io.github.h1dr0nn.handview.protocol.Error -> state.value = state.value.copy(message = R.string.stream_error)
                else -> Unit
            }.also {
                // Full Screen only shows a running game.
                if (state.value.fullScreen && state.value.editor == EditorStateValue.Stopped) state.value = state.value.copy(fullScreen = false)
            }
        }
    }
    private fun orient(value: Orientation) {
        requestedOrientation = if (value == Orientation.Landscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
    }
    override fun command(cmd: Cmd) { client?.sendCommand(cmd) }
    override fun open(screen: Screen) { app.value = app.value.copy(screen = screen, notice = null); state.value = state.value.copy(message = null) }
    override fun wifiNotReady() { app.value = app.value.copy(screen = Screen.Home, notice = R.string.wifi_hint) }
    override fun change(settings: AppSettings) {
        val video = settings.streamPrefs() != app.value.settings.streamPrefs()
        store.save(settings); app.value = app.value.copy(settings = settings); applyTheme()
        if (video) client?.sendStreamPrefs(settings.streamPrefs())
        state.value = state.value.copy(keyboard = settings.keyboard, keyboardOpen = state.value.keyboardOpen && settings.keyboard)
        startSensors()
    }
    override fun toggleTheme() { change(app.value.settings.copy(dark = !Palette.dark)) }
    private fun applyTheme() {
        val system = resources.configuration.uiMode and android.content.res.Configuration.UI_MODE_NIGHT_MASK == android.content.res.Configuration.UI_MODE_NIGHT_YES
        Palette.dark = app.value.settings.dark ?: system
    }
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) { super.onConfigurationChanged(newConfig); applyTheme() }
    override val version: String get() = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull().orEmpty()
    override fun surface(): SurfaceView = surfaceView()
    // A session that ends lands on Home, which shows why it ended.
    private fun end(message: Int, rejoin: Boolean = false) {
        val wasConnected = state.value.welcome != null
        closeSession()
        autoConnect = rejoin && wasConnected
        lastTried = null
        app.value = app.value.copy(screen = Screen.Home, notice = message)
    }
    override fun disconnect() { autoConnect = false; closeSession(); app.value = app.value.copy(screen = Screen.Home, notice = null) }
    private fun closeSession() {
        if (::sensors.isInitialized) sensors.stop()
        generation++; decoder?.close(); decoder = null
        client?.close(ByeReason.Normal); client = null; token = null
        latency.clear(); touchReady = false; state.value = CastState()
        orient(Orientation.Portrait)
    }
    private fun surfaceView(): SurfaceView = SurfaceView(this).apply {
        holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) { activeHolder = holder; decoder?.setSurface(holder.surface) }
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) { activeHolder = holder; decoder?.setSurface(holder.surface) }
            // A layout change (Full Screen) creates the new view before the old one
            // goes away; only the view in use may take the surface from the decoder.
            override fun surfaceDestroyed(holder: SurfaceHolder) { if (holder === activeHolder) { activeHolder = null; decoder?.setSurface(null) } }
        })
        setOnTouchListener { view, motion ->
            if (app.value.settings.touch && touchReady && renderedStream == stream && state.value.editor == EditorStateValue.Playing && motion.pointerCount in 1..10) {
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
    override fun onDestroy() { main.removeCallbacksAndMessages(null); closeSession(); super.onDestroy() }
}
