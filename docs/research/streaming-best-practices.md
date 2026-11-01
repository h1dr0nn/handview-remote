# Streaming best practices (research)

### 1. scrcpy: ADB tunneling, socket architecture, and client pacing

* **ADB reverse vs forward:** [scrcpy tunnels](https://github.com/Genymobile/scrcpy/blob/master/doc/tunnels.md) defaults to `adb reverse localabstract:scrcpy_<scid> tcp:PORT`. The host starts a TCP listener before launching the device server, enabling instantaneous connection setup. If `adb reverse` fails (legacy Android versions or unsupported network transports), scrcpy falls back via [`adb_tunnel.h`](https://github.com/Genymobile/scrcpy/blob/master/app/src/adb/adb_tunnel.h) to `adb forward tcp:PORT localabstract:scrcpy_<scid>`, where the device listens on an abstract UNIX domain socket and the host connects.
* **Socket layout:** Scrcpy establishes independent sockets for video, audio, and control over ADB as documented in [scrcpy develop guide](https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md). Multiplexing video and touch onto one TCP stream causes severe head-of-line blocking: a multi-packet video I-frame delays subsequent touch input packets behind network buffer queues. Dedicated sockets decouple control packets from video throughput spikes.
* **MediaCodec low-latency encoding:** As outlined in [scrcpy video documentation](https://github.com/Genymobile/scrcpy/blob/master/doc/video.md), scrcpy configures Android `MediaCodec` with `KEY_LATENCY = 0` (Android 8.0+) and `KEY_PRIORITY = 0` (real-time). It sets `KEY_I_FRAME_INTERVAL` or issues manual keyframe requests on demand to eliminate recurring keyframe buffer bursts.
* **Client frame skipping:** By default, the scrcpy client prioritizes displaying the latest frame. If the decoder or rendering thread stalls, expired frames that miss their presentation window are skipped immediately as detailed in [scrcpy display documentation](https://github.com/Genymobile/scrcpy/blob/master/doc/display.md). The flag `--render-expired-frames` disables this dropping mechanism, which preserves every frame but causes progressive latency accumulation.
* **Lessons for reverse streaming (PC encodes, phone decodes):**
  * Android should initiate connection over `adb reverse tcp:PORT tcp:PORT` directly to the PC listener.
  * Separate video and touch into two distinct TCP sockets to prevent touch event queuing behind large video slices.
  * The Android receiver must aggressively drop late input packets or unrendered output frames rather than buffering them when the decoder falls behind.

#### Recommendation for Handview
* Use `adb reverse tcp:<port> tcp:<port>` so the Android app connects directly to the PC TCP listener, falling back to `adb forward` if reverse fails.
* Allocate two distinct TCP connections: one strictly for inbound H.264 video NALUs and one for outbound touch input and handshake data.
* Discard queued frames on the Android client if frame arrival timestamp falls behind presentation deadlines to avoid buffer bloat.

---

### 2. Sunshine / Moonlight / Parsec / OBS: Low-latency NVENC and Media Foundation

* **Low-latency NVENC H.264 settings:**
  * **Presets & Tuning:** Use preset P1 (fastest) or P2/P3 with tuning set to `NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY` (`nv_tune = ull`), reducing encoder buffering depth to 1 frame per [NVIDIA Video Codec SDK Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/12.1/nvenc-video-encoder-api-prog-guide/index.html).
  * **Rate control & VBV:** Configure constant bitrate (`NV_ENC_PARAMS_RC_CBR`). Set VBV buffer size (`vbvBufferSize = bitrate / fps`) and initial delay to exactly 1 frame. This forces the rate governor to distribute bits evenly across single-frame intervals without multi-frame bit pooling.
  * **B-frames & GOP:** Set `frameIntervalP = 1` (zero B-frames) to eliminate reordering latency. Set `gopLength = NVENC_INFINITE_GOPLENGTH` combined with periodic intra-refresh or IDR frames generated strictly on network loss via SPS/PPS repetition (`repeatSPSPPS = 1`).
  * **Intra-refresh:** Configure `enableIntraRefresh = 1`. Replacing periodic full-screen IDR bursts with progressive intra-macroblock columns prevents network interface card (NIC) buffer saturation as evaluated in [Sunshine video pipeline](https://github.com/LizardByte/Sunshine/blob/master/src/video.cpp).
* **Media Foundation H.264 MFT settings:**
  * **Low latency mode:** Set [`CODECAPI_AVLowLatencyMode`](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avlowlatencymode) to `VARIANT_TRUE` via `ICodecAPI`, which forces MFT hardware encoders to operate with zero lookahead buffers.
  * **Rate control & GOP:** Set `CODECAPI_AVEncCommonRateControlMode` to `eAVEncCommonRateControlMode_CBR`, `CODECAPI_AVEncMPVDefaultBPictureCount` to `0`, and GOP size to match session constraints per [Media Foundation Codec API](https://learn.microsoft.com/en-us/windows/win32/medfound/codec-api-properties).
  * **Zero-copy D3D manager:** Send [`MFT_MESSAGE_SET_D3D_MANAGER`](https://learn.microsoft.com/en-us/windows/win32/medfound/mft-message-set-d3d-manager) with an `IMFDXGIDeviceManager` wrapping the D3D11 device. This enables direct GPU-to-encoder hardware surface passing without host RAM copies.
  * **Async hardware MFT events:** Hardware encoders require asynchronous event processing (`MF_TRANSFORM_ASYNC = TRUE` and `MF_TRANSFORM_ASYNC_UNLOCK`). Feed textures on `METransformNeedInput` and fetch packets on `METransformHaveOutput`.
  * **Annex-B format:** Set `CODECAPI_AVEncH264VOutputNALUFormat` to `eAVEncH264VOutputNALUFormat_AnnexB` (value `1`). Many Intel/AMD driver transforms ignore this flag and emit AVCC (4-byte length headers), requiring manual in-memory rewrites to `0x00000001` start codes before sending over the wire.
* **Vendor MFT pitfalls:**
  * **Intel QuickSync:** Can buffer up to 4 frames unless `CODECAPI_AVLowLatencyMode` and `CODECAPI_AVEncCommonRealTime` are set prior to `SetOutputType` negotiation.
  * **AMD AMF MFT:** Wraps AMF with higher latency than the native AMF SDK. Fails to flush or deadlocks during shutdown if `METransformDrainComplete` is not explicitly awaited, as documented across [OBS Studio win-mediafoundation](https://github.com/obsproject/obs-studio/tree/master/plugins/win-mediafoundation).

#### Recommendation for Handview
* Target NVENC first with preset P1/P2, tuning ULL, CBR rate control, 1-frame VBV buffer, zero B-frames, and repeat SPS/PPS enabled.
* Use Media Foundation H.264 as a fallback: set `CODECAPI_AVLowLatencyMode` to true, zero B-frames, and bind D3D11 textures via `IMFDXGIDeviceManager`.
* Add an in-memory NALU prefix sanitizer that converts length-prefixed AVCC headers to Annex-B start codes (`00 00 00 01`) to guard against Intel/AMD MFT driver quirks.

---

### 3. Android MediaCodec low-latency decode

* **Core MediaFormat low-latency keys:**
  * **`KEY_LOW_LATENCY` (`"low-latency"`):** Added in Android 11 (API 30). Setting integer value `1` instructs modern hardware decoders to output frames immediately without internal display queuing per [Android MediaCodec documentation](https://developer.android.com/reference/android/media/MediaCodec).
  * **`KEY_PRIORITY` (`"priority"`):** Added in API 23. Setting value `0` requests real-time codec execution over background task power saving per [Android MediaFormat](https://developer.android.com/reference/android/media/MediaFormat#KEY_PRIORITY).
  * **`KEY_OPERATING_RATE` (`"operating-rate"`):** Setting to `Short.MAX_VALUE` or maximum display refresh rate (e.g., 120) prevents the mobile SoC dynamic frequency governor from running the video processing unit (VPU) at low power clocks during stream startup.
* **Vendor keys from moonlight-android:**
  * **Qualcomm:** Sets `vendor.qti-ext-dec-low-latency.enable = 1` and `vendor.qti-ext-dec-picture-order.enable = 1` in [`MediaCodecHelper.java`](https://github.com/moonlight-stream/moonlight-android/blob/master/app/src/main/java/com/limelight/binding/video/MediaCodecHelper.java) for Snapdragon low-latency paths.
  * **MediaTek & Exynos:** Sets `vdec-lowlatency = 1` and handles hardware-specific frame pacing workarounds listed in [Moonlight decoder-errata.txt](https://github.com/moonlight-stream/moonlight-android/blob/master/decoder-errata.txt).
* **Async vs Sync decoding:**
  * Async mode (`setCallback`) relies on Android `Handler`/`Looper` threads, introducing message loop jitter and GC-induced pauses.
  * Synchronous mode (`dequeueInputBuffer` / `dequeueOutputBuffer` with timeout 0) inside a dedicated decoding thread provides deterministic polling, sub-millisecond execution control, and immediate frame drop decisions.
* **Buffer presentation with `releaseOutputBuffer`:**
  * Calling `releaseOutputBuffer(index, true)` releases the decoded frame to SurfaceFlinger for immediate composition.
  * Calling `releaseOutputBuffer(index, renderTimestampNs)` targets a specific display VSYNC interval.
  * If a frame arrives late or fails timing checks, call `releaseOutputBuffer(index, false)` to drop the frame instantly and return the hardware buffer back to the decoder pool.
* **SurfaceView vs TextureView:**
  * `SurfaceView` provides a dedicated hardware layer composited directly by SurfaceFlinger, saving 1 full frame of composition latency (~16ms at 60Hz) with zero CPU/GPU memory copies.
  * `TextureView` operates inside the Android View hierarchy, rendering through OpenGLES/Vulkan into an off-screen buffer, adding 1 to 2 frames of latency.
* **OnFrameRenderedListener semantics:**
  * [`MediaCodec.setOnFrameRenderedListener`](https://developer.android.com/reference/android/media/MediaCodec#setOnFrameRenderedListener(android.media.MediaCodec.OnFrameRenderedListener,%20android.os.Handler)) returns `presentationTimeUs` and `nanoTime`.
  * `nanoTime` provides the monotonic timestamp (`System.nanoTime()`) when the hardware display subsystem received the buffer. Measuring `nanoTime - touchEventNanoTime` yields true hardware touch-to-display glass latency.

#### Recommendation for Handview
* Render exclusively into a `SurfaceView` to eliminate View hierarchy composition delay.
* Use synchronous decoding on a dedicated worker thread with `KEY_LOW_LATENCY = 1`, `KEY_PRIORITY = 0`, and `KEY_OPERATING_RATE = Short.MAX_VALUE`.
* Inject Qualcomm vendor keys (`vendor.qti-ext-dec-low-latency.enable = 1`) on Snapdragon hardware.
* Register `OnFrameRenderedListener` to calculate the difference between the touch event generation timestamp and the display presentation `nanoTime` for the latency overlay.

---

### 4. Godot 4.4+: Grabbing frames, texture sharing, and zero-copy interop

* **Frame grab at `RenderingServer.frame_post_draw`:**
  * The signal `RenderingServer.frame_post_draw` triggers on the main thread after rendering commands are dispatched to the GPU per [Godot RenderingServer documentation](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html).
  * `RenderingDevice.texture_get_data(rid, layer)` synchronously stalls the CPU while flushing the GPU pipeline, causing massive frame drop (5-15ms stall) per [Godot RenderingDevice](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html).
  * `RenderingDevice.texture_get_data_async(rid, layer, callable)` performs non-blocking asynchronous VRAM readback via staging buffers, invoking the callback with a `PackedByteArray` 1 to 2 frames later according to `rendering/rendering_device/vsync/frame_queue_size`. It prevents CPU stutter at the cost of pipeline delay.
* **Accessing native driver handles:**
  * `RenderingServer.texture_get_rd_texture(viewport_texture.get_rid())` extracts the low-level `RenderingDevice` RID from Godot high-level textures.
  * `RenderingDevice.get_driver_resource(RenderingDevice.DRIVER_RESOURCE_TEXTURE, rd_rid, 0)` returns the underlying graphics driver handle: a `VkImage` in Vulkan or an `ID3D12Resource*` in Direct3D 12.
* **Existing frame-sharing addons:**
  * [spout-gd](https://github.com/you-win/spout-gd): GDExtension for Spout on Windows. Targets D3D11/OpenGL shared textures. Under Godot 4 Vulkan, it relies on CPU readback or OpenGL compatibility fallback because Vulkan swapchains cannot be directly exported via standard Windows handles.
  * [godot-ndi](https://github.com/unvermuthet/godot-ndi): Integrates the NewTek NDI SDK via GDExtension, capturing Viewports via texture readback and feeding raw memory buffers to the CPU-based NDI encoding pipeline.
* **Vulkan external memory to D3D11 feasibility:**
  * Zero-copy sharing from Vulkan to D3D11/NVENC requires `VK_KHR_external_memory_win32` (`vkGetMemoryWin32HandleKHR`).
  * Stock Godot does not enable `VK_KHR_external_memory` at `vkCreateDevice` time, as highlighted in [Godot Proposal #15210](https://github.com/godotengine/godot-proposals/issues/15210) and [#13969](https://github.com/godotengine/godot-proposals/issues/13969).
  * `RenderingDevice.texture_create_from_extension` cannot import or export Windows NT shared handles unless the Vulkan device was initialized with external memory extensions.
  * When using Godot 4.3+ with the Direct3D 12 backend (`--rendering-driver d3d12`), zero-copy sharing to D3D11/NVENC is viable via native `ID3D12Device::CreateSharedHandle`.

#### Recommendation for Handview
* If running on the standard Vulkan backend, use `RenderingDevice.texture_get_data_async` to avoid CPU frame stalls, and accept 1-2 frames of staging buffer latency for encoding.
* For zero-copy encoding without CPU readback, launch the game with `--rendering-driver d3d12` (Godot 4.3+) and share the D3D12 resource handle directly with D3D11/NVENC via `ID3D11Device1::OpenSharedResource1`.
* Extract the viewport texture via `RenderingServer.texture_get_rd_texture` inside a `frame_post_draw` callback.

---

### 5. Godot game window: Rendering states, embedding, and power modes

* **Off-screen, unfocused, and tool window rendering:**
  * Exported Godot games do not throttle framerates when unfocused by default per [Godot issue discussions](https://github.com/godotengine/godot/issues/45049). Unfocused windows run at full speed unless `application/run/low_processor_mode_when_unfocused` is explicitly enabled.
  * Off-screen windows (e.g., positioned at `Vector2i(-10000, -10000)` via `DisplayServer.window_set_position`) remain active in the Windows Desktop Window Manager (DWM) composition tree. `DisplayServer.can_draw()` returns true, maintaining full rendering and buffer swap rates.
  * Applying the Win32 `WS_EX_TOOLWINDOW` style hides the game from the taskbar and Alt-Tab switcher while keeping standard top-level rendering behavior intact.
* **Minimized window behavior:**
  * When a Godot window is minimized, `DisplayServer::can_draw()` returns false, halting the render pipeline to save resources per [Godot DisplayServer](https://docs.godotengine.org/en/stable/classes/class_displayserver.html). Frame capture freezes completely.
  * In Godot 4.3+, `DisplayServer.register_additional_output()` keeps the render loop running for head-mounted displays (HMDs) or background feeds, but keeping an off-screen or borderless window is cleaner than minimizing.
* **Godot 4.4 game embedding in editor:**
  * Godot 4.4 introduces embedded game execution inside a dedicated Game workspace tab per [Godot 4.4 announcement](https://godotengine.org/article/godot-4-4-arrives-in-style/).
  * The running game executes as an independent OS process whose native window handle is reparented into the editor docking container using Windows child window APIs.
* **Editor low processor mode interference:**
  * The Godot editor runs with low processor mode enabled (`interface/editor/low_processor_mode_sleep_usec`, sleeping ~100ms when unfocused, dropping editor redraw to ~10 FPS) per [Godot OS class reference](https://docs.godotengine.org/en/stable/classes/class_os.html).
  * If the editor plugin owns the TCP listener and processes network packets on Godot's main thread, moving focus to the Android device or game window causes the editor to sleep 100ms per iteration, adding massive latency to any bridged messages.

#### Recommendation for Handview
* Run the game window off-screen (`DisplayServer.window_set_position(Vector2i(-10000, -10000))`) rather than minimizing it, ensuring `can_draw()` never suspends rendering.
* Run the editor plugin TCP listener on a dedicated background `Thread` so socket operations are unaffected by editor low processor mode throttling.
* Disable `application/run/low_processor_mode_when_unfocused` in the target game project settings.

---

### 6. Godot EditorPlugin: Process control, IPC, and debugging

* **Environment variables vs `main_run_args`:**
  * Child processes spawned by the editor inherit parent environment variables set via `OS.set_environment()`. However, setting environment variables in GDScript mutates the entire editor process state globally.
  * The standard approach uses [`EditorInterface.set_main_run_args(args)`](https://docs.godotengine.org/en/stable/classes/class_editorinterface.html#class-editorinterface-method-set-main-run-args) or modifies project setting `editor/run/main_run_args`. Custom arguments passed after `--` can be cleanly read inside the running game via `OS.get_cmdline_user_args()`.
* **Process lifecycle control:**
  * `EditorInterface.play_main_scene()` launches the primary project scene.
  * `EditorInterface.stop_playing_scene()` cleanly terminates the active running game process.
  * `EditorInterface.is_playing_scene()` allows checking execution state.
* **Detecting game exit and crash:**
  * Implement an [`EditorDebuggerPlugin`](https://docs.godotengine.org/en/stable/classes/class_editordebuggerplugin.html) and override `_setup_session(session_id)`.
  * Bind to [`EditorDebuggerSession.stopped`](https://docs.godotengine.org/en/stable/classes/class_editordebuggersession.html#signals), which fires when the child process terminates normally or crashes.
  * Track `EditorDebuggerSession.started` and `EditorDebuggerSession.breaked` to monitor engine health and execution traps.
* **Pausing and stepping frames from an addon:**
  * Godot's debugger protocol exposes execution control via `EditorDebuggerSession.send_message("message", [args])`.
  * In Godot 4.4, the Game workspace and debugger interface expose suspend and single-frame stepping commands via `scene_debugger` protocol messages (`request_embed_next_frame` / `embed_suspend_toggle_requested`).
  * Addons can register custom bidirectional debugger protocol handlers by calling [`EngineDebugger.register_message_capture("handview", callable)`](https://docs.godotengine.org/en/stable/classes/class_enginedebugger.html) in the game and sending `session.send_message("handview:pause", [])` or `session.send_message("handview:next_frame", [])` from the editor plugin.

#### Recommendation for Handview
* Pass the Handview TCP port and handshake metadata using `EditorInterface.set_main_run_args("--handview-port " + str(port))` and parse them in the game via `OS.get_cmdline_user_args()`.
* Use an `EditorDebuggerPlugin` listening to `EditorDebuggerSession.stopped` to detect game process crashes or unexpected terminations.
* Implement custom debugger messaging via `EngineDebugger.register_message_capture("handview", ...)` to provide pause, resume, and frame-stepping control from the editor UI.
