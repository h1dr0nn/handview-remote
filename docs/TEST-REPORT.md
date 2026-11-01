# Test report

Results of the automated suites and the end-to-end runs. Phone results are added once real devices are tested.

## Machine

| Item | Value |
|---|---|
| OS | Windows 11 Pro 10.0.26200 |
| GPU | NVIDIA GeForce RTX 3060 (driver 610.88) + AMD Radeon iGPU |
| Display | 120 Hz |
| Godot | 4.7.2 stable (CI checks the addon on 4.4.1) |
| Compiler | MSVC 14.50 (Visual Studio 2026), CMake 4.4 |

## Automated suites (2026-11-01)

| Suite | Command | Result |
|---|---|---|
| Native core + tools | `ctest --preset windows-x64-debug` | 8/8 pass (protocol vectors, flow control, net, session, stream, encoders, loopback E2E) |
| Encoders | `test_encode` | NVENC, MF hardware and MF software each encode 120 frames that decode back with the right size and IDR placement |
| Android | `./gradlew :protocol:test :client:test :app:assembleDebug :app:lintDebug` | protocol and client tests pass, lint has no errors |
| Addon | `godot --headless --path godot-addon -s res://tests/run_tests.gd` | 38 passed |
| Packaging | `python tools/package/package.py --self-test` | pass |

## Encoder timings

1920x1080 RGBA input scaled to 1280x720, 60 fps, 8000 kbps, per-frame time including upload, scale and color conversion:

| Backend | p50 | p95 |
|---|---|---|
| NVENC (RTX 3060) | 4.0 ms | 8.1 ms |
| MF hardware | 5.0 ms | 8.5 ms |
| MF software | 5.3 to 6.7 ms | 7.5 to 8.7 ms |

Viewport readback: see `docs/spikes/01-gpu-interop.md` (2.3 to 2.6 ms per 1080p frame without vsync).

## End to end on the PC

Godot 4.7.2 editor with the addon, the dev project's latency scene (1280x720, canvas_items stretch), phone replaced by `handview-mock-client` over loopback, NVENC. "Touch latency" here is PC side: from the Touch message leaving the client to the first VideoFrame whose `lastTouchSeq` covers it, so it includes the game update, readback, encode and relay, but not phone decode or display.

| Run | Duration | Frames | FPS while playing | Taps resolved | Touch latency p50 / p95 / max | Errors |
|---|---|---|---|---|---|---|
| Single play with decoding | 15 s | 785 | ~60 | 42/42 | 15.8 / 29.7 / 32.2 ms | 0 |
| Soak | 30 min | 107,181 | 59.5 | 3533/3533 | 17.4 / 33.5 / 77.9 ms | 0 |
| 10 Play / Stop cycles | 10 x 4 s | 1,294 | ~60 | 71/74 (3 sent while stopping) | 16.9 / 17.5 / 37.4 ms | 0 |
| Game kills itself after 6 s | 20 s | 345 | ~60 | 19/20 | 17.0 / 18.2 / 30.8 ms | 0 |

Soak memory (sampled every 30 s): game process 860 to 864 MB private memory, editor about 1.79 GB, both flat; handle counts flat. Latency did not drift (p95 33.5 ms over 30 minutes against 29.7 ms in the 15 s run).

Crash run: the phone side received `StreamPaused{NotPlaying}` as soon as the game link dropped and `EditorState{Stopped}` when the editor saw the play end; the session stayed open.

Hidden window check: the streaming game window was visible to Windows, off-screen, with `WS_EX_TOOLWINDOW` set and no taskbar button (`docs/spikes/02-hidden-window.md`).

## Phone matrix (pending)

No phone was connected to the development machine, and the Android emulator needs a hypervisor that is not enabled, so the app has been built, unit-tested and linted but not yet run against the editor on a device. To fill this table, run a session with the latency scene for at least 5 minutes per device and read the overlay.

| Phone | Android | Decoder | Encoder (PC) | Orientation | Touch to display p50 / p95 | Notes |
|---|---|---|---|---|---|---|
| | | | NVENC | landscape | | |
| | | | MF hardware | landscape | | |
| | | | NVENC | portrait | | |

Not yet covered without a phone: USB cable pull mid-session, several devices plugged in, busy ports with real adb reverses, missing / old / differently signed APK installs, real MediaCodec behavior and the touch-to-display overlay.
