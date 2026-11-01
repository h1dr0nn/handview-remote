# Spike 1: Godot GPU interop (encoder input path)

## Question

How does the encoder get the game frame: share the GPU texture (Vulkan / D3D12 handle to D3D11) or read it back to the CPU, and at what cost?

## What was done

`tools/spikes/capture` is a Godot project rendering a 1920x1080 3D scene with UI. On `RenderingServer.frame_post_draw` it reads the root viewport texture (`RenderingServer.texture_get_rd_texture`) with `RenderingDevice.texture_get_data` (sync) or `texture_get_data_async`, or with `Viewport.get_texture().get_image()`. 600 frames after a 60 frame warm-up.

Machine: Windows 11, NVIDIA GeForce RTX 3060 (driver 610.88) + AMD Radeon iGPU, 120 Hz display, Godot 4.7.2.

```bash
godot --path tools/spikes/capture [--disable-vsync] [--rendering-driver d3d12] [--rendering-method mobile|gl_compatibility] -- mode=sync|async|image|none window=normal|offscreen|nofocus|minimized frames=600
```

## Numbers (1920x1080 RGBA8, 8.3 MB per frame)

| Renderer / driver | VSync | Mode | FPS | Data ready after | Main thread cost p50 / p95 |
|---|---|---|---|---|---|
| Forward+ / Vulkan | on | none | 118 | - | - |
| Forward+ / Vulkan | on | sync | 119.5 | 7.1 ms | 7.1 / 7.6 ms |
| Forward+ / Vulkan | on | async | 119.8 | 16.6 ms (1 frame) | 0.05 ms |
| Forward+ / Vulkan | off | none | 1447 | - | - |
| Forward+ / Vulkan | off | sync | 274 | 2.6 ms | 2.6 / 2.9 ms |
| Forward+ / Vulkan | off | async | 322 | 6.1 ms (1 frame) | 0.04 ms |
| Forward+ / D3D12 | off | sync | 249 | 2.3 ms | 2.3 / 2.8 ms |
| Forward+ / D3D12 | off | async | 264 | 7.5 ms (1 frame) | 0.04 ms |
| Mobile / Vulkan | off | sync | 266 | 2.6 ms | 2.6 / 2.9 ms |
| Compatibility / OpenGL | off | image | 153 | 4.7 ms | 4.7 / 5.4 ms |
| Forward+ / Vulkan | off | image | 107 | 8.2 ms (RGB8) | 8.2 / 9.4 ms |
| Forward+ / Vulkan, 1280x720 | on | sync | 119.5 | 7.5 ms | 7.5 / 7.8 ms |

Findings:

- Sync readback costs about 2.5 ms at 1080p when the game does not wait for vsync. With vsync on, the same call waits for the swapchain and costs about 7 ms regardless of resolution.
- Async readback is cheap on the main thread but always delivers one frame later (16.6 ms at 120 Hz, 33 ms at 60 Hz). That is too much against a 30 to 50 ms touch-to-display target.
- Compatibility has no `RenderingDevice`; `get_image()` works there at 4.7 ms.
- Zero-copy sharing is not reachable through public APIs: Godot creates the viewport image without external memory export flags, and `RenderingDevice` offers no way to signal a semaphore or fence that D3D11 could wait on, so the encoder could not know when a GPU copy has finished. `texture_create_from_extension` can wrap an exportable image we create, but the synchronization gap remains.

## Conclusion

CPU readback is fast enough. It costs 2 to 3 ms per 1080p frame, less at the capped stream size, and keeps every renderer and driver on one code path.

## Recommendation (adopted, see DECISIONS D12)

1. On `frame_post_draw`, when the in-flight limit allows a frame: `texture_get_data` (sync) on Forward+ / Mobile, `get_image()` on Compatibility.
2. In a game launched by Handview, turn vsync off and set `Engine.max_fps` to the stream FPS: the PC window is hidden, and this removes the 7 ms swapchain wait from the readback.
3. Copy the RGBA bytes to the encoder thread; the encoder uploads them to its own D3D11 device, scales and converts to NV12 with the D3D11 Video Processor, then encodes with NVENC or Media Foundation.
4. Revisit zero-copy only if measurements on target machines show readback as the bottleneck.
