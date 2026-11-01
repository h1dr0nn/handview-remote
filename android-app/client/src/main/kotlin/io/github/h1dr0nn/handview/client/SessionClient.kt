package io.github.h1dr0nn.handview.client

import io.github.h1dr0nn.handview.protocol.*
import java.net.InetSocketAddress
import java.net.Socket
import java.io.EOFException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class SessionClient(
    private val port: Int,
    token: String,
    hello: Hello,
    private val listener: (Event) -> Unit,
    private val timeoutMs: Long = 5000,
    private val pingMs: Long = 1000,
) {
    sealed interface Event {
        data class Connected(val welcome: Welcome) : Event
        data class Rejected(val reason: RejectReason) : Event
        data class Received(val message: Message) : Event
        data class Disconnected(val reason: DisconnectReason) : Event
    }
    sealed interface DisconnectReason {
        data class Remote(val reason: ByeReason) : DisconnectReason
        data object Timeout : DisconnectReason
        data object IoError : DisconnectReason
        data object ProtocolError : DisconnectReason
    }
    private val greeting = hello.copy(token = Bytes(decodeToken(token)))
    private val stopped = AtomicBoolean(false)
    private val started = AtomicBoolean(false)
    private val writer = Executors.newSingleThreadExecutor { r -> Thread(r, "handview-control-writer") }
    private val timer = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "handview-heartbeat") }
    private val lock = Any()
    @Volatile private var control: Socket? = null
    @Volatile private var video: Socket? = null
    @Volatile private var lastReceived = System.nanoTime()
    @Volatile private var welcomed = false
    private val unknownTypes = mutableSetOf<Int>()

    fun start() {
        check(started.compareAndSet(false, true))
        Thread({
            try {
                val socket = connect(false) ?: return@Thread
                writer.submit { write(greeting) }.get()
                lastReceived = System.nanoTime()
                timer.scheduleAtFixedRate({
                    if (System.nanoTime() - lastReceived >= timeoutMs * 1_000_000) finish(DisconnectReason.Timeout)
                    else send(Ping(System.nanoTime() / 1000))
                }, pingMs, pingMs, TimeUnit.MILLISECONDS)
                read(socket, Channel.CONTROL)
            } catch (_: Exception) { finish(DisconnectReason.IoError) }
        }, "handview-control-reader").start()
    }
    private fun connect(isVideo: Boolean): Socket? {
        val socket = Socket()
        synchronized(lock) {
            if (stopped.get()) { socket.close(); return null }
            if (isVideo) video = socket else control = socket
        }
        socket.connect(InetSocketAddress("127.0.0.1", port), 5000)
        socket.tcpNoDelay = true
        return socket
    }
    private fun read(socket: Socket, channel: Channel) {
        val input = java.io.BufferedInputStream(socket.getInputStream(), 64 * 1024)
        while (!stopped.get()) {
            val header = ByteArray(HEADER_SIZE)
            input.readFully(header)
            val h = decodeHeader(header, 0, header.size, channel)
            if (h !is HeaderResult.Ok) { protocolError(); return }
            val packet = ByteArray(HEADER_SIZE + h.header.length.toInt())
            header.copyInto(packet)
            input.readFully(packet, HEADER_SIZE)
            val result = decode(packet, 0, packet.size, channel)
            lastReceived = System.nanoTime()
            when (result.status) {
                DecodeStatus.UNKNOWN_TYPE -> synchronized(unknownTypes) {
                    if (unknownTypes.add(h.header.type)) System.err.println("Handview: skipping unknown type ${h.header.type}")
                }
                DecodeStatus.OK -> {
                    val m = result.message!!
                    if (channel == Channel.VIDEO) {
                        if (m !is VideoFrame) { protocolError(); return }
                        listener(Event.Received(m))
                    } else handleControl(m)
                }
                else -> { protocolError(); return }
            }
        }
    }
    private fun handleControl(message: Message) {
        if (!welcomed && message !is Welcome && message !is Reject && message !is Ping && message !is Pong && message !is Bye) {
            protocolError(); return
        }
        when (message) {
            is Welcome -> {
                if (welcomed) { protocolError(); return }
                welcomed = true
                Thread({
                    try {
                        val socket = connect(true) ?: return@Thread
                        socket.getOutputStream().write(encode(VideoAttach(message.sessionId)))
                        listener(Event.Connected(message))
                        read(socket, Channel.VIDEO)
                    } catch (_: Exception) { finish(DisconnectReason.IoError) }
                }, "handview-video-reader").start()
            }
            is Reject -> finish(null, rejected = message.reason)
            is Ping -> send(Pong(message.timeUs))
            is Pong -> Unit
            is Bye -> finish(DisconnectReason.Remote(message.reason))
            is CommandResult, is EditorState, is StreamConfigChanged, is StreamPaused, is io.github.h1dr0nn.handview.protocol.Error -> listener(Event.Received(message))
            else -> protocolError()
        }
    }
    private fun java.io.InputStream.readFully(bytes: ByteArray, offset: Int = 0) {
        var position = offset
        while (position < bytes.size) {
            val count = read(bytes, position, bytes.size - position)
            if (count < 0) throw EOFException()
            position += count
        }
    }
    private fun write(message: Message) {
        try { control?.getOutputStream()?.write(encode(message)) }
        catch (_: Exception) { finish(DisconnectReason.IoError) }
    }
    private fun send(message: Message) {
        synchronized(lock) {
            if (!stopped.get() && (welcomed || message is Ping || message is Pong)) writer.execute { write(message) }
        }
    }
    fun sendCommand(cmd: Cmd) = send(Command(cmd))
    fun sendTouch(seq: Long, eventTimeUs: Long, pointers: List<Pointer>) = send(Touch(seq, eventTimeUs, pointers))
    fun sendFrameAck(seq: Long) = send(FrameAck(seq))
    fun sendStreamPrefs(prefs: StreamPrefs) = send(prefs)
    fun sendViewSize(view: ViewSize) = send(ViewSize(
        view.width.coerceIn(0, 65535), view.height.coerceIn(0, 65535),
        view.safeLeft.coerceIn(0, 65535), view.safeTop.coerceIn(0, 65535), view.safeRight.coerceIn(0, 65535), view.safeBottom.coerceIn(0, 65535),
    ))
    fun sendDecoderStats(pending: Int, dropped: Int) = send(DecoderStats(pending.coerceIn(0, 65535), dropped.coerceIn(0, 65535)))
    fun requestKeyframe() = send(RequestKeyframe)
    fun sendError(source: ErrorSource, code: ErrorCode) = send(io.github.h1dr0nn.handview.protocol.Error(source, code))
    fun close(byeReason: ByeReason = ByeReason.Normal) = finish(DisconnectReason.Remote(byeReason), byeReason)
    private fun protocolError() = finish(DisconnectReason.ProtocolError, ByeReason.ProtocolError)
    private fun finish(reason: DisconnectReason?, bye: ByeReason? = null, rejected: RejectReason? = null) {
        synchronized(lock) {
            if (!stopped.compareAndSet(false, true)) return
            timer.shutdownNow()
            if (bye != null) writer.execute { write(Bye(bye)); cleanup(reason) }
            else writer.execute { cleanup(reason); rejected?.let { listener(Event.Rejected(it)) } }
            writer.shutdown()
        }
    }
    private fun cleanup(reason: DisconnectReason?) {
        runCatching { control?.close() }; runCatching { video?.close() }
        reason?.let { listener(Event.Disconnected(it)) }
    }
    companion object {
        fun decodeToken(hex: String): ByteArray {
            require(hex.length == 64 && hex.all { it.digitToIntOrNull(16) != null }) { "Invalid session token" }
            return ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        }
    }
}
