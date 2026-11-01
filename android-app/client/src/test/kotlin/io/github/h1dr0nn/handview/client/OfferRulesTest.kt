package io.github.h1dr0nn.handview.client

import org.junit.Assert.assertEquals
import org.junit.Test

class OfferRulesTest {
    private data class Offer(val port: Int, val name: String, val token: String)
    private fun add(list: List<Offer>, offer: Offer) = OfferRules.add(list, offer, Offer::port, Offer::name)

    @Test fun newerOfferOnSamePortReplacesOlder() {
        val list = add(listOf(Offer(38500, "Wild Paws", "a")), Offer(38500, "Wild Paws", "b"))
        assertEquals(listOf(Offer(38500, "Wild Paws", "b")), list)
    }

    @Test fun sameProjectOnAnotherPortReplacesOlder() {
        val list = add(listOf(Offer(38501, "Wild Paws", "a")), Offer(38500, "Wild Paws", "b"))
        assertEquals(listOf(Offer(38500, "Wild Paws", "b")), list)
    }

    @Test fun differentProjectsStay() {
        val list = add(listOf(Offer(38500, "Wild Paws", "a")), Offer(38501, "Demo", "b"))
        assertEquals(listOf(Offer(38501, "Demo", "b"), Offer(38500, "Wild Paws", "a")), list)
    }

    @Test fun unnamedOffersOnlyReplaceTheirPort() {
        val list = add(listOf(Offer(38500, "", "a"), Offer(38501, "", "b")), Offer(38502, "", "c"))
        assertEquals(3, list.size)
    }

    @Test fun distinctKeepsNewestPerProject() {
        val list = listOf(Offer(38500, "Wild Paws", "new"), Offer(38501, "Wild Paws", "old"), Offer(38502, "", "x"))
        assertEquals(listOf(Offer(38500, "Wild Paws", "new"), Offer(38502, "", "x")), OfferRules.distinct(list, Offer::port, Offer::name))
    }
}
