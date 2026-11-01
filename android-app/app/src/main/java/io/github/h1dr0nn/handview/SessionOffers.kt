package io.github.h1dr0nn.handview

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.util.Base64
import io.github.h1dr0nn.handview.client.SessionClient
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

// An editor that armed a one-time token for this phone over adb (DECISIONS D18).
// `icon` is the project icon saved by the receiver, or null.
data class SessionOffer(val port: Int, val token: String, val name: String, val atMillis: Long, val icon: File? = null)

object SessionOffers {
    private const val PREFS = "handview_offers"
    private val state = MutableStateFlow<List<SessionOffer>>(emptyList())
    private var loaded = false

    fun flow(context: Context): StateFlow<List<SessionOffer>> { load(context); return state.asStateFlow() }

    @Synchronized fun put(context: Context, offer: SessionOffer) {
        load(context)
        // One editor per port, and one per project: a newer offer replaces an
        // older one from the same port or the same project (an editor that moved
        // to another port after a restart).
        save(context, listOf(offer) + state.value.filter { it.port != offer.port && (offer.name.isEmpty() || it.name != offer.name) })
    }

    @Synchronized fun remove(context: Context, port: Int) {
        load(context)
        save(context, state.value.filter { it.port != port })
    }

    @Synchronized private fun load(context: Context) {
        if (loaded) return
        loaded = true
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        state.value = prefs.all.values.mapNotNull { value ->
            val f = (value as? String)?.split('\n') ?: return@mapNotNull null
            if (f.size !in 4..5) return@mapNotNull null
            runCatching { SessionOffer(f[0].toInt(), f[1], f[2], f[3].toLong(), f.getOrNull(4)?.let(::File)?.takeIf { it.isFile }) }.getOrNull()
        }.sortedByDescending { it.atMillis }.distinctBy { it.name.ifEmpty { "port ${it.port}" } }
    }

    private fun save(context: Context, offers: List<SessionOffer>) {
        state.value = offers.sortedByDescending { it.atMillis }
        val edit = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear()
        offers.forEach { edit.putString(it.port.toString(), "${it.port}\n${it.token}\n${it.name}\n${it.atMillis}\n${it.icon?.path.orEmpty()}") }
        edit.apply()
    }
}

// Receives offers sent by the editor addon with `adb shell am broadcast`. The
// manifest guards it with the DUMP permission, which only the shell and the
// system hold, so other apps on the phone cannot plant offers.
class SessionOfferReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val token = intent.getStringExtra("token") ?: return
        val port = intent.getIntExtra("port", 0)
        if (port !in 1..65535 || runCatching { SessionClient.decodeToken(token) }.isFailure) return
        val name = intent.getStringExtra("name_hex")?.let { hex ->
            runCatching { String(hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray(), Charsets.UTF_8) }.getOrNull()
        }.orEmpty().take(64)
        val icon = intent.getStringExtra("icon_b64")?.let { saveIcon(context, port, it) }
        SessionOffers.put(context, SessionOffer(port, token, name, System.currentTimeMillis(), icon))
    }

    // Keeps the icon only if it decodes as a small image.
    private fun saveIcon(context: Context, port: Int, base64: String): File? = runCatching {
        if (base64.length > 64 * 1024) return null
        val bytes = Base64.decode(base64, Base64.DEFAULT)
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        if (bounds.outWidth !in 1..256 || bounds.outHeight !in 1..256) return null
        File(context.filesDir, "offer_icons").apply { mkdirs() }.resolve("$port.png").apply { writeBytes(bytes) }
    }.getOrNull()
}
