package io.github.h1dr0nn.handview.client

import io.github.h1dr0nn.handview.protocol.*
import org.junit.Assert.*
import org.junit.Test
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class SessionClientTest {
    private val hello = Hello(1,Bytes(ByteArray(32)),1080,1920,420,60,emptyList(),"test")
    private val welcome = Welcome(Bytes(ByteArray(16) { it.toByte() }),Host.Godot,Codec.H264,Encoder.Nvenc,640,480,Orientation.Landscape,60,5000,EditorStateValue.Playing)
    private fun read(socket: Socket, channel: Channel = Channel.CONTROL): Message {
        socket.soTimeout = 3000
        val input = socket.getInputStream()
        val h = input.readNBytes(6)
        check(h.size == 6)
        val header = (decodeHeader(h,0,6,channel) as HeaderResult.Ok).header
        val packet = h + input.readNBytes(header.length.toInt())
        return decode(packet,0,packet.size,channel).message!!
    }
    private fun await(events: LinkedBlockingQueue<SessionClient.Event>, predicate: (SessionClient.Event) -> Boolean): SessionClient.Event {
        val deadline = System.nanoTime() + 4_000_000_000
        while (System.nanoTime() < deadline) {
            val event = events.poll(100,TimeUnit.MILLISECONDS) ?: continue
            if (predicate(event)) return event
        }
        throw AssertionError("Expected session event")
    }
    @Test fun handshakeAttachFragmentedAndCoalescedFrames() {
        ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
            val events = LinkedBlockingQueue<SessionClient.Event>()
            val client = SessionClient(host.localPort,"ab".repeat(32),hello,{ events.add(it) })
            try {
                client.start()
                host.accept().use { control ->
                    val greeting = read(control) as Hello
                    assertEquals(Bytes(ByteArray(32) { 0xab.toByte() }),greeting.token)
                    val bytes = encode(welcome)
                    control.getOutputStream().write(bytes,0,3)
                    control.getOutputStream().write(bytes,3,bytes.size-3)
                    host.accept().use { video ->
                        assertEquals(VideoAttach(welcome.sessionId),read(video,Channel.VIDEO))
                        await(events) { it is SessionClient.Event.Connected }
                        val f1 = VideoFrame(1,0,100,VideoFlags.KeyframeCodecConfig,Bytes(byteArrayOf(1)))
                        val f2 = f1.copy(frameSeq = 2, ptsUs = 200)
                        video.getOutputStream().write(encode(f1) + encode(f2))
                        assertEquals(f1,(await(events) { it is SessionClient.Event.Received } as SessionClient.Event.Received).message)
                        assertEquals(f2,(await(events) { it is SessionClient.Event.Received } as SessionClient.Event.Received).message)
                        client.close()
                        assertEquals(Bye(ByeReason.Normal),read(control))
                    }
                }
            } finally { client.close() }
        }
    }
    @Test fun rejectReasonsCloseWithoutDisconnected() {
        for (reason in RejectReason.entries) {
            ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
                val events = LinkedBlockingQueue<SessionClient.Event>()
                val client = SessionClient(host.localPort,"00".repeat(32),hello,{ events.add(it) })
                try {
                    client.start()
                    host.accept().use { socket ->
                        read(socket)
                        socket.getOutputStream().write(encode(Reject(reason)))
                        assertEquals(SessionClient.Event.Rejected(reason),await(events) { it is SessionClient.Event.Rejected })
                        assertEquals(-1,socket.getInputStream().read())
                        client.close()
                        assertNull(events.poll(200,TimeUnit.MILLISECONDS))
                    }
                } finally { client.close() }
            }
        }
    }
    @Test fun pingPongAndUnknownType() {
        ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
            val events = LinkedBlockingQueue<SessionClient.Event>()
            val client = SessionClient(host.localPort,"00".repeat(32),hello,{ events.add(it) },pingMs=50)
            try {
                client.start()
                host.accept().use { socket ->
                    read(socket)
                    socket.getOutputStream().write(encode(Unknown(0x7777,3)) + encode(Ping(42)))
                    var sawPong = false; var sawPing = false
                    repeat(5) {
                        when (val m = read(socket)) {
                            is Pong -> { assertEquals(42L,m.timeUs); sawPong = true }
                            is Ping -> { sawPing = true; socket.getOutputStream().write(encode(Pong(m.timeUs))) }
                            else -> fail("Unexpected message")
                        }
                        if (sawPong && sawPing) return@repeat
                    }
                    assertTrue(sawPong && sawPing)
                }
            } finally { client.close() }
        }
    }
    @Test fun timeoutWithoutMessages() {
        ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
            val events = LinkedBlockingQueue<SessionClient.Event>()
            val client = SessionClient(host.localPort,"00".repeat(32),hello,{ events.add(it) },timeoutMs=150,pingMs=25)
            try {
                client.start()
                host.accept().use { socket ->
                    read(socket)
                    assertEquals(SessionClient.Event.Disconnected(SessionClient.DisconnectReason.Timeout),await(events) { it is SessionClient.Event.Disconnected })
                }
            } finally { client.close() }
        }
    }
    @Test fun badEnumSendsProtocolBye() {
        ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
            val events = LinkedBlockingQueue<SessionClient.Event>()
            val client = SessionClient(host.localPort,"00".repeat(32),hello,{ events.add(it) })
            try {
                client.start()
                host.accept().use { socket ->
                    read(socket)
                    val bytes = encode(Reject(RejectReason.BadToken)); bytes[6] = 99
                    socket.getOutputStream().write(bytes)
                    assertEquals(Bye(ByeReason.ProtocolError),read(socket))
                    assertEquals(SessionClient.Event.Disconnected(SessionClient.DisconnectReason.ProtocolError),await(events) { it is SessionClient.Event.Disconnected })
                }
            } finally { client.close() }
        }
    }
    @Test fun oversizedHeaderSendsProtocolBye() {
        ServerSocket(0).apply { soTimeout = 4000 }.use { host ->
            val client = SessionClient(host.localPort,"00".repeat(32),hello,{ })
            try {
                client.start()
                host.accept().use { socket ->
                    read(socket)
                    val header = ByteArray(6); encodeHeader(Header(2,65537),header)
                    socket.getOutputStream().write(header)
                    assertEquals(Bye(ByeReason.ProtocolError),read(socket))
                }
            } finally { client.close() }
        }
    }
}
