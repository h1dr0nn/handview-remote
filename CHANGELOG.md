# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Pause, Resume and Step from the phone on host Godot, through the editor debugger (DECISIONS D14).
- Android app screens from the wireframes: Home, USB Waiting, Scan QR, Settings and Cast, in a warm paper style with the system font, light and dark. Settings are saved; items without a backend yet are marked Soon. Home lists the open projects with their name and icon.
- The game fits the phone's video area: a play started from the phone resizes its hidden window to that aspect ratio, like the Game workspace's Stretch to Fit (ViewSize message, DECISIONS D19). Full Screen shows only the game; Back leaves it.
- Wi-Fi sessions (DECISIONS D22): "Allow Wi-Fi" in the dock opens the listener to the LAN, shows a QR code and a PIN, and announces the editor with a UDP beacon. The app lists editors on the network, joins with the PIN or by scanning the code, and shows Wi-Fi in the status line.
- Phone keyboard and motion sensors: Key and Sensor messages; the runtime injects key events and sets the accelerometer, gyroscope, gravity and magnetometer.
- Resolution, FPS and Bitrate settings apply to the stream (StreamPrefs message).
- Reload on the cast screen (in place of Step) restarts the game with the latest code; the phone shows "Reloading" until the new run starts (Restart command).
- macOS on Apple Silicon: the GDExtension builds as `handview.macos.template_*.arm64.dylib` (CMake preset `macos-arm64`) and encodes with VideoToolbox. Encoder value 3 (VideoToolbox) is added to `Welcome.encoder`, so the app must be updated together with the addon (DECISIONS D23).
- `Handview.get_safe_area()` and `safe_area_changed` give a game the phone's cutout and system bar insets while it runs under Handview (DECISIONS D20).

### Changed

- No Start / Stop Session: the editor listens while it runs, prepares every plugged in phone and offers itself to the app, which joins on its own (DECISIONS D18). The X button only leaves the cast screen; a game started from the phone stops with it.
- The app opens on Home in portrait and joins when you tap a project; it rejoins on its own only after a dropped connection. Cast follows the stream orientation.
- Icons use Material Symbols Rounded (DECISIONS D21). The launcher icon is a cream phone with a touch ring on a terracotta gradient, as bitmap adaptive layers with a themed (monochrome) layer; the 1024 px source layers are in `android-app/icon/`. The app's versionCode is 3, so the addon installs the new app over older ones. Cast chrome is one status row and one control row so a 9:16 game fills the width.

### Fixed

- Taps landed at the wrong place in games with stretch mode canvas_items and a window smaller than the design size.
- The dock stayed on "native library is not available" when the addon was copied into an open editor.
- Video stayed black after the decoder restarted (Full Screen, rotation, decode errors): frames dropped while waiting for an IDR were never acked, so the host stopped sending. The app acks them and RequestKeyframe resets the host's in-flight count.
- After an adb server restart the phone could not join: reverses are checked on every refresh and set up again.
- An editor that moved to another port showed up twice on the phone.
- Settings' video options open a floating dropdown instead of pushing the page down; messages are toasts.
- The dock showed each device's state on the same line as its name.
- Touch-to-display never showed a value on Pixel phones: the Exynos decoder reports render times on CLOCK_BOOTTIME. Render times are now mapped onto the monotonic clock.

## [0.2.0] - 2026-11-01

Features: the full slice works end to end on the PC (Godot editor, hidden game, capture, H.264, relay, decoding client, touch back) with every encoder backend and both orientations. Verification on real phones is still pending.

### Added

- CI jobs for the native tests, the Android unit tests and lint, and the addon tests on Godot 4.4.1.
- Release packaging: signed release APK from environment secrets, addon zip with LICENSE and checksum (`tools/package`), and a tag-triggered release workflow.
- Android adaptive launcher icon.
- Mock client Play / Stop cycle mode, latency scene self-kill hook for crash runs.
- Production README, test report (`docs/TEST-REPORT.md`), naming and trademark research.

### Changed

- Media Foundation hardware encoding no longer polls for MFT events: p50 per 720p frame dropped from 20 ms to 5 ms.
- Capture pacing tolerates frame jitter, so the stream holds the target 60 fps.

### Fixed

- A Play command arriving right after Welcome launched the game without the link variables, so nothing streamed.
- Android: rotation no longer recreates the activity and replays a consumed token; decoder input size, FIFO frame matching, bounded latency samples, reject reasons, socket and display metrics handling.
- Native: unsupported force-keyframe on MF encoders is recoverable, NVENC input is flushed before mapping, finished server workers are pruned, phone Error messages are reported.
- Addon: launch timeout when the phone never connects, stale uninstall prompts, deferred results after shutdown, adb reverse list order, launch error detection.

### Measured

- 30 minute soak on the PC: 107,181 frames at 59.5 fps, 3533/3533 taps resolved, touch to frame on the PC p50 17.4 ms, p95 33.5 ms, flat memory.

## [0.1.0] - 2026-11-01

Foundation: every building block exists, is tested on its own, and the editor to game pipeline runs end to end on a PC.

### Added

- Protocol v1 specification (`protocol/SPEC.md`) with 108 shared test vectors, and codecs for every message in C++ and Kotlin that agree on all of them.
- Native core: loopback networking, editor session server with single-use tokens, session replacement, ping timeout and a relay to the game process; game stream controller with in-flight limit, keyframe rate limit, decoder backpressure, Resolution Cap and touch delivery; H.264 encoders for NVENC and Media Foundation (hardware and software) with a shared D3D11 front end.
- GDExtension classes `HandviewEditorSession` and `HandviewRuntime`.
- Godot addon: Handview dock, USB Launch over adb (device states, app install with confirmation before uninstalling, port pick, reverse, launch intent), Play / Stop from the phone, hidden play window, touch injection, viewport capture, latency test scene.
- Android app: USB session client, MediaCodec low-latency decoder on a SurfaceView, multi-touch, touch-to-display latency tracker, USB Waiting and Cast screens in portrait and landscape.
- Internal tools: mock client (PC stand-in for the phone, with decoding) and mock host (stand-in for the editor, for testing the app).
- Spike reports: GPU interop and encoder input path, hidden play window, editor control of the played game.

### Measured

- Godot 4.7.2 editor, RTX 3060 (NVENC), 1280x720 at 60 fps to the mock client over loopback: touch to frame on the PC p50 15.4 ms, p95 28.7 ms, no decode errors. Phone measurements are pending.
