package io.github.h1dr0nn.handview

import android.content.Context
import android.net.wifi.WifiManager
import io.github.h1dr0nn.handview.client.WifiJoin
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

// An editor with Wi-Fi allowed, heard on this network (DECISIONS D22).
data class LanEditor(val name: String, val host: String, val port: Int, val seenMillis: Long)

// Listens for the editors' UDP beacons while the app is in front. Beacons hold
// no token: joining needs the PIN or the QR code.
class LanDiscovery(context: Context) {
    private val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val state = MutableStateFlow<List<LanEditor>>(emptyList())
    val editors: StateFlow<List<LanEditor>> = state.asStateFlow()
    @Volatile private var thread: Thread? = null
    @Volatile private var socket: DatagramSocket? = null

    fun start() {
        if (thread != null) return
        val lock = wifi.createMulticastLock("handview-discovery").apply { setReferenceCounted(false); acquire() }
        thread = Thread({
            try {
                DatagramSocket(null).use { s ->
                    socket = s
                    s.reuseAddress = true
                    s.broadcast = true
                    s.soTimeout = 1000
                    s.bind(InetSocketAddress(WifiJoin.BEACON_PORT))
                    val buffer = ByteArray(1024)
                    while (thread === Thread.currentThread()) {
                        val packet = DatagramPacket(buffer, buffer.size)
                        try { s.receive(packet) } catch (_: SocketTimeoutException) { prune(); continue }
                        val beacon = WifiJoin.parseBeacon(String(packet.data, 0, packet.length, Charsets.UTF_8)) ?: continue
                        val host = packet.address.hostAddress ?: continue
                        val now = System.currentTimeMillis()
                        state.value = (listOf(LanEditor(beacon.name, host, beacon.port, now)) + state.value.filter { it.host != host || it.port != beacon.port })
                            .filter { now - it.seenMillis < EXPIRY_MS }
                    }
                }
            } catch (_: Exception) {
            } finally {
                runCatching { lock.release() }
            }
        }, "handview-discovery").apply { isDaemon = true; start() }
    }

    fun stop() {
        thread = null
        socket?.close()
        socket = null
        state.value = emptyList()
    }

    private fun prune() {
        val now = System.currentTimeMillis()
        if (state.value.any { now - it.seenMillis >= EXPIRY_MS }) state.value = state.value.filter { now - it.seenMillis < EXPIRY_MS }
    }

    private companion object { const val EXPIRY_MS = 6_000L }
}
