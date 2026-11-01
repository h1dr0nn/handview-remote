package io.github.h1dr0nn.handview.client

import io.github.h1dr0nn.handview.protocol.seqNewerOrEqual
import kotlin.math.ceil

class LatencyTracker {
    data class Summary(val p50: Double, val p95: Double, val max: Double)
    private val pending = linkedMapOf<Long, Long>()
    private val samples = ArrayDeque<Pair<Long, Long>>()
    @Synchronized fun record(touchSeq: Long, t0Nanos: Long) {
        prunePending(t0Nanos)
        pending[touchSeq] = t0Nanos
        while (pending.size > 256) pending.remove(pending.keys.first())
    }
    private fun prunePending(now: Long) {
        pending.entries.removeAll { it.value < now - 5_000_000_000L }
    }
    @Synchronized fun onFrameRendered(lastTouchSeq: Long, renderNanos: Long) {
        prunePending(renderNanos)
        if (lastTouchSeq == 0L) return
        val iterator = pending.iterator()
        while (iterator.hasNext()) {
            val (seq, start) = iterator.next()
            if (seqNewerOrEqual(lastTouchSeq.toInt(), seq.toInt())) {
                if (renderNanos >= start) samples.addLast(renderNanos to (renderNanos - start))
                iterator.remove()
            }
        }
        prune(renderNanos)
    }
    private fun prune(now: Long) {
        while (samples.isNotEmpty() && (samples.first().first < now - 10_000_000_000L || samples.size > 500)) samples.removeFirst()
    }
    @Synchronized fun summary(nowNanos: Long = System.nanoTime()): Summary? {
        prunePending(nowNanos)
        prune(nowNanos)
        val sorted = samples.map { it.second / 1_000_000.0 }.sorted()
        if (sorted.isEmpty()) return null
        fun percentile(p: Double) = sorted[(ceil(sorted.size * p).toInt() - 1).coerceAtLeast(0)]
        return Summary(percentile(.5), percentile(.95), sorted.last())
    }
    @Synchronized fun clear() { pending.clear(); samples.clear() }
}
