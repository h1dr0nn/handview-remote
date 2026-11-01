package io.github.h1dr0nn.handview.client

// How the app keeps editor offers (DECISIONS D18): newest first, one per port
// and one per project name, so an editor that moved to another port after a
// restart does not show twice.
object OfferRules {
    fun <T> add(existing: List<T>, offer: T, port: (T) -> Int, name: (T) -> String): List<T> =
        listOf(offer) + existing.filter { port(it) != port(offer) && (name(offer).isEmpty() || name(it) != name(offer)) }

    // For lists saved before this rule: keep the first (newest) per project.
    fun <T> distinct(offers: List<T>, port: (T) -> Int, name: (T) -> String): List<T> =
        offers.distinctBy { name(it).ifEmpty { "port ${port(it)}" } }
}
