package io.github.h1dr0nn.handview.client

import java.security.MessageDigest

// Joining an editor over Wi-Fi (DECISIONS D22): the QR code in the Handview
// dock carries the address and the one-time token; a PIN typed by hand stands
// in for the token as SHA-256("handview-pin:<pin>").
object WifiJoin {
    const val DEFAULT_PORT = 38500
    const val BEACON_PORT = 38510

    data class Link(val host: String, val port: Int, val token: String, val name: String)
    data class Beacon(val name: String, val port: Int)

    fun pinToken(pin: String): String =
        MessageDigest.getInstance("SHA-256").digest("handview-pin:$pin".toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }

    // handview://wifi?h=<ip>&p=<port>&t=<64 hex>&n=<name as UTF-8 hex>
    fun parseLink(text: String): Link? {
        if (!text.startsWith("handview://wifi?")) return null
        val query = text.substringAfter('?').split('&').mapNotNull { part ->
            val i = part.indexOf('=')
            if (i <= 0) null else part.substring(0, i) to part.substring(i + 1)
        }.toMap()
        val host = query["h"]?.takeIf(::isIpv4) ?: return null
        val port = query["p"]?.toIntOrNull()?.takeIf { it in 1..65535 } ?: return null
        val token = query["t"]?.takeIf { it.length == 64 && it.all(::isHex) } ?: return null
        val name = query["n"]?.let(::fromHex).orEmpty()
        return Link(host, port, token.lowercase(), name)
    }

    // "192.168.1.10" or "192.168.1.10:38501".
    fun parseAddress(text: String): Pair<String, Int>? {
        val parts = text.trim().split(':')
        if (parts.size > 2 || !isIpv4(parts[0])) return null
        val port = if (parts.size == 2) parts[1].toIntOrNull()?.takeIf { it in 1..65535 } ?: return null else DEFAULT_PORT
        return parts[0] to port
    }

    // {"handview":1,"name":"...","port":38500,...}; only the fields the app uses.
    fun parseBeacon(json: String): Beacon? {
        if (!Regex("\"handview\"\\s*:\\s*1\\b").containsMatchIn(json)) return null
        val port = Regex("\"port\"\\s*:\\s*(\\d+)").find(json)?.groupValues?.get(1)?.toIntOrNull()?.takeIf { it in 1..65535 } ?: return null
        val name = Regex("\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").find(json)?.groupValues?.get(1)?.replace("\\\"", "\"")?.replace("\\\\", "\\").orEmpty()
        return Beacon(name.take(48), port)
    }

    private fun isIpv4(text: String): Boolean {
        val parts = text.split('.')
        return parts.size == 4 && parts.all { p -> p.isNotEmpty() && p.length <= 3 && p.all(Char::isDigit) && p.toInt() in 0..255 }
    }

    private fun isHex(c: Char) = c in '0'..'9' || c in 'a'..'f' || c in 'A'..'F'

    private fun fromHex(hex: String): String? = runCatching {
        String(hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray(), Charsets.UTF_8)
    }.getOrNull()
}
