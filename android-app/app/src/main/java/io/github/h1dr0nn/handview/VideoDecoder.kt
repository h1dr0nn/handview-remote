package io.github.h1dr0nn.handview

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import io.github.h1dr0nn.handview.client.*
import io.github.h1dr0nn.handview.protocol.*
import java.nio.ByteBuffer
import kotlin.math.abs

private const val TAG = "Handview"

class VideoDecoder(
    private val client: SessionClient,
    private val rendered: (Int, Long, Long) -> Unit,
) {
    private val thread = HandlerThread("handview-codec").apply { start() }
    private val handler = Handler(thread.looper)
    private var codec: MediaCodec? = null
    private var surface: Surface? = null
    private var width = 1
    private var height = 1
    private var configured = false
    private var stream = 0
    private var waiting = true
    private var closed = false
    private var lagSince = 0L
    private var lastKeyframeRequest = 0L
    private var dropped = 0
    private val inputs = ArrayDeque<Int>()
    private val frames = ArrayDeque<VideoFrame>()
    private data class Stamp(val seq: Long, val touch: Long, val pts: Long)
    private data class Output(val index: Int, val pts: Long, val stamp: Stamp?)
    private val stamps = ArrayDeque<Stamp>()
    private var lastOutputSeq = 0L
    private val displaying = mutableMapOf<Long, Stamp>()
    private val outputs = ArrayDeque<Output>()
    private var drainPosted = false
    private val stats = object : Runnable {
        override fun run() {
            if (closed) return
            client.sendDecoderStats(outputs.size, dropped)
            dropped = 0
            val now = System.nanoTime()
            if (waiting && configured && surface != null && now - lastKeyframeRequest >= 1_000_000_000L) {
                lastKeyframeRequest = now
                Log.d(TAG, "waiting for a keyframe, ${frames.size} frames queued")
                client.requestKeyframe()
            }
            if (frames.size > 6 || outputs.size > 3) {
                if (lagSince == 0L) lagSince = now
                if (now - lagSince > 1_000_000_000L) recover(null)
            } else lagSince = 0
            handler.postDelayed(this, 250)
        }
    }
    init { handler.post(stats) }
    fun setSurface(value: Surface?) { handler.post {
        if (closed || surface === value) return@post
        Log.d(TAG, "surface ${if (value == null) "cleared" else "set"}")
        if (codec != null || value == null) reset()
        surface = value
        if (value != null) {
            try { feed() } catch (e: Exception) { Log.w(TAG, "decoder failed", e); recover(ErrorCode.ProcessFailed) }
            if (waiting && frames.isEmpty()) client.requestKeyframe()
        }
    } }
    fun configure(w: Int, h: Int, streamId: Int) { handler.post {
        if (closed || w <= 0 || h <= 0) return@post
        if (configured) reset()
        Log.d(TAG, "configure ${w}x$h stream $streamId")
        width = w; height = h; stream = streamId; configured = true
        try { feed() } catch (e: Exception) { Log.w(TAG, "decoder failed", e); recover(ErrorCode.InitFailed) }
    } }
    fun frame(frame: VideoFrame) { handler.post {
        if (closed) return@post
        frames.addLast(frame)
        try { feed() } catch (e: Exception) { Log.w(TAG, "decoder failed", e); recover(ErrorCode.ProcessFailed) }
    } }
    private fun initialize(frame: VideoFrame) {
        val data = frame.data.toByteArray()
        val sps = AnnexB.sps(data) ?: error("Missing SPS")
        val pps = AnnexB.pps(data) ?: error("Missing PPS")
        fun format(optional: Boolean, c: MediaCodec): MediaFormat {
            return MediaFormat.createVideoFormat("video/avc", width, height).apply {
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, (width.toLong() * height * 3 / 2).coerceIn(1_048_576L, Int.MAX_VALUE.toLong()).toInt())
                setByteBuffer("csd-0", ByteBuffer.wrap(AnnexB.csd(sps)))
                setByteBuffer("csd-1", ByteBuffer.wrap(AnnexB.csd(pps)))
                if (optional) {
                    setInteger(MediaFormat.KEY_PRIORITY, 0)
                    setFloat(MediaFormat.KEY_OPERATING_RATE, Short.MAX_VALUE.toFloat())
                    if (Build.VERSION.SDK_INT >= 30 && c.codecInfo.getCapabilitiesForType("video/avc").isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_LowLatency)) setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
                    if (c.name.startsWith("c2.qti") || c.name.startsWith("OMX.qcom")) setInteger("vendor.qti-ext-dec-low-latency.enable", 1)
                }
            }
        }
        fun createCandidate(optional: Boolean): MediaCodec {
            val candidate = MediaCodec.createDecoderByType("video/avc")
            try {
                installCallbacks(candidate)
                candidate.configure(format(optional, candidate), surface, null, 0)
                return candidate
            } catch (e: Exception) { candidate.release(); throw e }
        }
        val candidate = try { createCandidate(true) } catch (_: Exception) { createCandidate(false) }
        codec = candidate
        Log.d(TAG, "decoder ${candidate.name} started for ${width}x$height")
        candidate.start()
    }
    private fun installCallbacks(candidate: MediaCodec) {
        candidate.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(c: MediaCodec, index: Int) {
                if (c !== codec) return
                inputs.addLast(index)
                try { feed() } catch (e: Exception) { Log.w(TAG, "decoder failed", e); recover(ErrorCode.ProcessFailed) }
            }
            override fun onOutputBufferAvailable(c: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                if (c !== codec) return
                val match = stamps.indexOfFirst { it.pts == info.presentationTimeUs }
                var stamp: Stamp? = null
                repeat(if (match >= 0) match + 1 else if (stamps.isNotEmpty()) 1 else 0) { stamp = stamps.removeFirst() }
                stamp?.let { lastOutputSeq = it.seq }
                client.sendFrameAck(lastOutputSeq)
                outputs.addLast(Output(index, info.presentationTimeUs, stamp))
                if (!drainPosted) { drainPosted = true; handler.post { drain() } }
            }
            override fun onOutputFormatChanged(c: MediaCodec, format: MediaFormat) = Unit
            override fun onError(c: MediaCodec, error: MediaCodec.CodecException) {
                if (c === codec) recover(ErrorCode.ProcessFailed)
            }
        }, handler)
        candidate.setOnFrameRenderedListener({ c, pts, nanoTime ->
            if (c === codec) displaying.remove(pts)?.let {
                rendered(stream, it.touch, monotonic(nanoTime))
            }
        }, handler)
    }
    // The render time should use System.nanoTime(), but some decoders (Exynos on
    // Pixel) report CLOCK_BOOTTIME, which runs ahead by the time spent asleep.
    // Touch times use the monotonic clock, so map the render time onto it.
    private fun monotonic(renderNanos: Long): Long {
        val now = System.nanoTime()
        if (abs(renderNanos - now) < 1_000_000_000L) return renderNanos
        val boot = SystemClock.elapsedRealtimeNanos()
        return if (abs(renderNanos - boot) < 1_000_000_000L) renderNanos - (boot - now) else now
    }
    private fun feed() {
        if (!configured || surface == null) return
        if (waiting) {
            // Frames before the next IDR cannot be decoded. Ack them anyway, or the
            // host's in-flight limit stops sending and the IDR never comes.
            while (frames.isNotEmpty() && frames.first().flags.wire and 3 != 3) client.sendFrameAck(frames.removeFirst().frameSeq)
            val first = frames.firstOrNull() ?: return
            initialize(first)
            waiting = false
        }
        val c = codec ?: return
        while (inputs.isNotEmpty() && frames.isNotEmpty()) {
            val index = inputs.removeFirst()
            val frame = frames.removeFirst()
            val bytes = frame.data.toByteArray()
            val buffer = c.getInputBuffer(index) ?: error("No input buffer")
            check(buffer.capacity() >= bytes.size) { "Input buffer too small" }
            buffer.clear(); buffer.put(bytes)
            stamps.addLast(Stamp(frame.frameSeq, frame.lastTouchSeq, frame.ptsUs))
            c.queueInputBuffer(index, 0, bytes.size, frame.ptsUs, 0)
        }
    }
    private fun drain() {
        drainPosted = false
        val c = codec ?: return
        try {
            while (outputs.isNotEmpty()) {
                val output = outputs.removeFirst()
                val show = outputs.isEmpty()
                if (show) output.stamp?.let { displaying[output.pts] = it }
                else dropped++
                c.releaseOutputBuffer(output.index, show)
            }
        } catch (e: Exception) { Log.w(TAG, "decoder failed", e); recover(ErrorCode.ProcessFailed) }
    }
    private fun recover(code: ErrorCode?) {
        Log.w(TAG, "decoder recovery ${code ?: "lag"}")
        if (code == null) runCatching { codec?.flush() }
        reset()
        code?.let { client.sendError(ErrorSource.Decoder, it) }
        client.requestKeyframe()
    }
    private fun reset() {
        // Frames queued or inside the old decoder are gone: release them on the host.
        (listOfNotNull(frames.lastOrNull()?.frameSeq, stamps.lastOrNull()?.seq).maxOrNull())?.let { client.sendFrameAck(it) }
        val old = codec; codec = null
        runCatching { old?.stop() }; runCatching { old?.release() }
        frames.clear(); inputs.clear(); outputs.clear(); stamps.clear(); displaying.clear()
        waiting = true; lagSince = 0; drainPosted = false
    }
    fun close() { handler.post { closed = true; handler.removeCallbacksAndMessages(null); reset(); thread.quitSafely() } }
}
