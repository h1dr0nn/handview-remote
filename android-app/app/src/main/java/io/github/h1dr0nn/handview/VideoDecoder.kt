package io.github.h1dr0nn.handview

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import io.github.h1dr0nn.handview.client.*
import io.github.h1dr0nn.handview.protocol.*
import java.nio.ByteBuffer

class VideoDecoder(
    private val client: SessionClient,
    private val latency: LatencyTracker,
    private val rendered: () -> Unit,
) {
    private val thread = HandlerThread("handview-codec").apply { start() }
    private val handler = Handler(thread.looper)
    private var codec: MediaCodec? = null
    private var surface: Surface? = null
    private var width = 1
    private var height = 1
    private var configured = false
    private var waiting = true
    private var closed = false
    private var lagSince = 0L
    private var lastKeyframeRequest = 0L
    private var dropped = 0
    private val inputs = ArrayDeque<Int>()
    private val frames = ArrayDeque<VideoFrame>()
    private data class Stamp(val seq: Long, val touch: Long)
    private data class Output(val index: Int, val pts: Long, val stamp: Stamp?)
    private val stamps = mutableMapOf<Long, Stamp>()
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
        if (codec != null || value == null) reset()
        surface = value
        if (value != null) {
            try { feed() } catch (_: Exception) { recover(ErrorCode.ProcessFailed) }
            if (waiting && frames.isEmpty()) client.requestKeyframe()
        }
    } }
    fun configure(w: Int, h: Int) { handler.post {
        if (closed) return@post
        if (configured) reset()
        width = w; height = h; configured = true
        try { feed() } catch (_: Exception) { recover(ErrorCode.InitFailed) }
    } }
    fun frame(frame: VideoFrame) { handler.post {
        if (closed) return@post
        frames.addLast(frame)
        try { feed() } catch (_: Exception) { recover(ErrorCode.ProcessFailed) }
    } }
    private fun initialize(frame: VideoFrame) {
        val data = frame.data.toByteArray()
        val sps = AnnexB.sps(data) ?: error("Missing SPS")
        val pps = AnnexB.pps(data) ?: error("Missing PPS")
        fun format(optional: Boolean, c: MediaCodec): MediaFormat {
            return MediaFormat.createVideoFormat("video/avc", width, height).apply {
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
        candidate.start()
    }
    private fun installCallbacks(candidate: MediaCodec) {
        candidate.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(c: MediaCodec, index: Int) {
                if (c !== codec) return
                inputs.addLast(index)
                try { feed() } catch (_: Exception) { recover(ErrorCode.ProcessFailed) }
            }
            override fun onOutputBufferAvailable(c: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                if (c !== codec) return
                val stamp = stamps.remove(info.presentationTimeUs)
                stamp?.let { client.sendFrameAck(it.seq) }
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
                latency.onFrameRendered(it.touch, nanoTime)
                rendered()
            }
        }, handler)
    }
    private fun feed() {
        if (!configured || surface == null) return
        if (waiting) {
            while (frames.isNotEmpty() && frames.first().flags.wire and 3 != 3) frames.removeFirst()
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
            buffer.clear(); buffer.put(bytes)
            stamps[frame.ptsUs] = Stamp(frame.frameSeq, frame.lastTouchSeq)
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
        } catch (_: Exception) { recover(ErrorCode.ProcessFailed) }
    }
    private fun recover(code: ErrorCode?) {
        if (code == null) runCatching { codec?.flush() }
        reset()
        code?.let { client.sendError(ErrorSource.Decoder, it) }
        client.requestKeyframe()
    }
    private fun reset() {
        val old = codec; codec = null
        runCatching { old?.stop() }; runCatching { old?.release() }
        frames.clear(); inputs.clear(); outputs.clear(); stamps.clear(); displaying.clear()
        waiting = true; lagSince = 0; drainPosted = false
    }
    fun close() { handler.post { closed = true; handler.removeCallbacksAndMessages(null); reset(); thread.quitSafely() } }
}
