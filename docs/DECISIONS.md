# Decisions

Decisions taken during implementation where the brief or the design docs left a choice open. Newest last. Each entry: decision, reason, status.

## D1. License holder is "h1dr0n"

`LICENSE` says `Copyright (c) 2026 h1dr0n`, as confirmed by the owner. Status: final.

## D2. M0 commits stay as pushed

The first commits on `main` predated the commit convention. On the owner's request their messages were rewritten to Conventional Commits with `git commit-tree` (same trees, authors and dates). The pushed copy on `origin/main` still has the old messages until the owner force pushes; the old history is kept locally in `backup/pre-reword`. Work stays local until the owner asks for a push. Status: final.

## D3. Android minSdk 26

Hard floor is API 23 (`MediaCodec.OnFrameRenderedListener`, needed for touch-to-display). 26 drops the oldest, least predictable hardware decoders and allows `java.time` and adaptive icons without desugaring. `KEY_LOW_LATENCY` (30+) and `getEventTimeNanos()` (34+) are optional with fallbacks. compileSdk / targetSdk 35. Status: final.

## D4. Local toolchain: MSVC

Native code builds with MSVC (Visual Studio "Desktop development with C++" workload), the same compiler CI and official Godot builds use. CMake and Ninja come from pip (`python -m pip install --user cmake ninja`). Status: final.

## D5. Video path: the editor process relays the game's video

Both phone sockets (control, video) connect to one `PORT` on the PC, and the Godot Editor owns that listener, but frames are captured and encoded inside the separate game process. Options:

1. Editor relays: the game sends whole `VideoFrame` messages to the editor over an internal loopback link, a native thread in the editor forwards them to the phone.
2. Socket handoff: the editor passes the accepted video socket to the game with `WSADuplicateSocket`.

Chosen: 1. A loopback copy of a few MB/s costs well under a millisecond. The relay forwards only complete messages, so a game crash mid-frame never corrupts the phone's stream, and the same phone session survives any number of Play / Stop cycles without socket ownership changes. The relay runs on a native thread inside the GDExtension (loaded in the editor), so it does not depend on the editor main loop, which throttles itself in low processor mode. Status: final.

## D6. Editor to game parameters go through environment variables

The runtime autoload activates only when it finds `HANDVIEW_LINK_PORT` and `HANDVIEW_LINK_TOKEN` in its environment. The editor sets them with `OS.set_environment()` while a phone is connected and clears them when the phone session ends; every game the editor launches in that window inherits them, so a play started with the editor's own Play button streams too. `HANDVIEW_HIDE_WINDOW=1` is set only right before a play started from the phone and cleared on the next frame, so only those plays hide their window. The brief mentions command line arguments; the only way to add those from an addon is the project setting `editor/run/main_run_args`, which would write to `project.godot` and show up in the user's git diff. Spike 3 confirmed that variables set before `play_main_scene()` reach the game and that cleared variables do not leak into later plays. Status: final.

## D7. Internal editor to game link reuses the wire format

The loopback link uses the same framing and message codec as the phone protocol, plus link-only message types in the `0x0200` range (never sent to the phone). One codec, one set of tests. The link listener binds `127.0.0.1` on an OS-assigned port and requires the per-run link token. Status: final.

## D8. Session port range

Phone sessions use `PORT` from `38500` to `38509`: the first free port is used, and the same number is used on both sides of `adb reverse`. The listener binds `127.0.0.1` only. Status: final.

## D9. Token encoding in the launch intent

The 32 byte one-time token is generated in the editor (`Crypto.generate_random_bytes(32)`) and passed as a 64 character lowercase hex string extra `token`; the app decodes it back to 32 bytes for `Hello`. Status: final.

## D10. Color: BT.709 limited range, signalled in the stream

Godot's root viewport holds 8-bit sRGB-encoded RGB. Conversion to NV12 uses BT.709 limited range, and the SPS VUI carries the color description so Android decoders do not guess BT.601. Status: final, verify on devices.

## D12. Encoder input: CPU readback, then D3D11 upload

Spike 1 (`docs/spikes/01-gpu-interop.md`) measured sync readback of the root viewport at 2.3 to 2.6 ms per 1080p frame without vsync, while async readback adds a full frame and zero-copy sharing has no synchronization point in Godot's public API. The game runtime reads the frame back with `RenderingDevice.texture_get_data` (Forward+, Mobile) or `Viewport.get_texture().get_image()` (Compatibility), disables vsync and sets `Engine.max_fps` to the stream FPS in Handview-launched runs, and hands the bytes to an encoder thread that uploads them to its own D3D11 device. Status: final.

## D13. Hidden Play Window: no embedding, off-screen tool window

Spike 3 (`docs/spikes/03-editor-control.md`) found that Godot 4.4+ embeds the played game into the editor's Game workspace by default, which shows the game on the PC. The `game_embed_mode` setting and project metadata are read only once at editor start, so they cannot be changed per Play. For Handview plays the addon turns embedding off through the Game workspace's own "Embed Game on Next Play" menu item (found by its translated label, not by id) and restores the previous state after launch. The runtime then moves the window off-screen, sets no-focus, and adds `WS_EX_TOOLWINDOW` so it has no taskbar button. Spike 1 showed off-screen and no-focus windows keep rendering at full rate, while a minimized window stops drawing, so minimizing is never used. If the menu item cannot be found, the game runs embedded and the README documents that fallback. Status: final.

## D14. Pause / Step on host Godot

Spike 3 showed Pause and Step work from an addon through `EditorDebuggerSession.send_message("scene:suspend_changed", [bool])` and `("scene:next_frame", [])`. The owner approved the feature on 2026-11-02 (asked for the full wireframe controls). The addon registers an `EditorDebuggerPlugin` and sends those two messages; Pause toggles (SPEC 6.4), Step works only while paused, and both answer `Ignored` when no debugger session is active. On Pause the phone gets `StreamPaused{Paused}` and `EditorState{Paused}`. A suspended game keeps rendering the same image, so the game runtime stops capturing on node notification 9003 (suspended) and captures exactly one frame after 9004 (unsuspended), which is what a Step produces. The Game workspace's own pause button is not updated by these messages. Status: final, verified on a phone.

## D15. Game lifecycle signals

The loopback link is the primary signal that the game runs (LinkHello) or ended (link closed). `EditorDebuggerSession.stopped` and `EditorInterface.is_playing_scene()` back it up. A crashing game process stays alive about 7 s while showing its crash handler; the link drop or the debugger stop then reports `EditorState{Stopped}`. Status: final.

## D16. Store naming and trademark wording

Research (`docs/research/naming-and-trademarks.md`) found no conflicting "Handview" app, project or software trademark. Google Play titles are limited to 30 characters and listings must not claim features that do not work yet, so the Play title is "Handview" and the short description reads "Game View remote for Godot" until the Unity Host phase ships, when it becomes "Game View remote for Godot and Unity" (the brief's subtitle). Third-party names are used only descriptively ("for Godot"), never their logos, and the README carries the trademark notice. The registry searches must be repeated by hand before the first store submission. Status: final.

## D17. First-run editor exits and GDExtension docs

On Godot 4.7.2, a project's very first editor run that exits immediately (`--import`, or `--editor --quit`) segfaults on exit whenever the project contains a GDExtension that registers any class, even one trivial `RefCounted` class with a single method; with no registered class it exits cleanly, and later runs exit cleanly too. Letting the editor run longer (`--quit-after 1500`) also exits cleanly, so it is a race with the first-run documentation generation for extension classes, not Handview code. Normal interactive use is not affected. CI therefore prepares the project with `--editor --quit-after 1500` instead of `--import`. Separately, the plugin now starts its background adb tasks only after the editor's first filesystem scan has settled and never in headless editors (unless the loopback test hook is set), because worker threads running GDScript during the first import's script reload produced script errors. Status: final; revisit if a Godot release fixes the exit race.

## D18. No session button: the editor offers itself to every phone

The owner asked for a flow without Start / Stop Session: open a project with the addon and the phone sees it; Play on the PC opens the window as usual, Play on the phone runs it hidden. The editor now listens for its whole lifetime and prepares every plugged in device (app install or update, adb reverse) and hands the app a one-time token with an explicit `am broadcast` to an exported receiver guarded by `android.permission.DUMP` (only the shell and the system hold it). Options considered: keep `am start` (pops the app up on every plug in and every token change), a long-lived pairing secret (more state, no gain over USB), or the app polling the ports (any local app could answer). The app connects on its own while in front, except right after the user pressed the Disconnect button; the Auto Reconnect setting turns that off. When the phone leaves, a game it started (hidden window) is stopped, so nothing keeps running unseen on the PC. Stale reverses are removed only when no PC process listens on their port, so two editors on one PC do not break each other. This replaces SPEC section 5's launch-intent flow, which stays for test tools. Status: final.

## D19. Fit phone plays to the phone's video area

The owner asked for the game to fill the phone instead of keeping the project's window aspect ratio, like the Game workspace's "Stretch to Fit". The app reports the size of the area it shows the video in with a new control message, `ViewSize`, after `Welcome` and whenever the area changes (rotation, Full Screen). The editor keeps the latest one per session and relays it to the game link. A play started from the phone (hidden window) resizes its window to that aspect ratio, keeping the base width for a tall area and the base height for a wide one, so a game with stretch aspect "expand" lays out for the phone. Plays started on the PC keep their window, since the user sees it. Status: final.

## D20. Safe area through the runtime, not DisplayServer

In Full Screen the video covers the phone's cutout and system bars, so a game needs the phone's safe area. Scripts cannot override `DisplayServer.get_display_safe_area()`, so `ViewSize` carries the unsafe edges of the video area, and the runtime autoload scales them to the game window and exposes `Handview.get_safe_area()` (window pixels, the same convention as DisplayServer, falling back to it outside a phone play) and a `safe_area_changed` signal. Games opt in with one call. Status: final.

## D21. Material Symbols for the app's icons

The owner found the hand-drawn icons rough. The app uses `androidx.compose.material:material-icons-extended` (Rounded style, Apache 2.0). It is a large artifact, but release builds shrink it with R8 to the icons used. Status: final.

## D22. Wi-Fi sessions: LAN listener, beacon, QR code and PIN

The owner asked for the Phase 3 Wi-Fi flow. Options for getting the one-time token to a phone without adb: a long-lived pairing secret, an approval prompt in the editor (protocol change and a modal in the editor), or a QR code plus a short PIN. Chosen: the dock's QR code carries the address and the token; a phone that found the editor through its UDP beacon, or got the address typed in, joins with a six digit PIN whose SHA-256 ("handview-pin:<PIN>") is a second one-time token. Five wrong tokens drop the PIN and the editor shows a new one, which keeps guessing out of reach (five tries against a million values). "Allow Wi-Fi" is off by default and remembered per project, since it opens a port on the LAN; Windows asks to allow Godot through the firewall the first time. The QR code is generated in the GDExtension with Nayuki's QR Code generator (MIT, `native/third_party/qrcodegen`). The app scans with Google's code scanner (`play-services-code-scanner`), which needs no camera permission and adds little to the APK; phones without Google Play services use the IP and PIN instead. Status: final.

## D23. macOS on Apple Silicon: CPU frames into VideoToolbox

The owner moved to an Apple Silicon Mac and asked for Handview to work there as on Windows. Scope is macOS arm64 only (`macos.debug.arm64` / `macos.release.arm64` in `handview.gdextension`, plain `.dylib`); Intel Macs are not built. The core keeps one code path with a thin platform layer: BSD sockets with `poll()` and `SO_NOSIGPIPE` instead of Winsock (`WinsockInit` stays as a no-op so callers are unchanged; a POSIX bind without `SO_REUSEADDR` is already exclusive, matching `SO_EXCLUSIVEADDRUSE`), `arc4random_buf` for the session id, `steady_clock` for `now_us()`.

Encoder input stays the CPU readback of D12: the runtime's existing capture already returns RGBA8 bytes on both Mac renderers (Metal through `RenderingDevice.texture_get_data` for Forward+ and Mobile, `Viewport.get_texture().get_image()` for Compatibility / OpenGL), measured with correct channel order on Godot 4.7.2. Zero-copy from Godot's Metal texture to an `IOSurface` has no public API, the same reason as on Windows. The encoder scales and converts with vImage (`vImageScale_ARGB8888`, then `vImageConvert_ARGB8888To420Yp8_CbCr8` with the BT.709 matrix and limited range of D10) into a pooled NV12 `CVPixelBuffer`, and encodes with a hardware-only `VTCompressionSession`: real-time, no frame reordering, no automatic keyframes, BT.709 color tags, average bitrate with a 1.5x one-second cap, `ForceKeyFrame` for IDRs. `VTCompressionSessionCompleteFrames` after each frame keeps the one-frame-in, one-frame-out contract of the other backends. On an M5, 1080p in to 720p out takes about 3.5 ms per frame including scale and conversion; a test decodes the stream back and checks the colors. A software fallback was not added: every Apple Silicon Mac has the hardware encoder. VideoToolbox writes no VUI `bitstream_restriction`, and some decoders (Exynos on a Pixel 7a) then hold up to seven frames for reordering, which with one frame in flight stalls the stream after two frames; the backend rewrites each SPS with `max_num_reorder_frames` 0 so every frame is output at once.

The phone needs to know the encoder only for display, but `Welcome.encoder` is a validated enum, so value 3 (VideoToolbox) is a protocol addition: SPEC, vectors and both codecs change together, and an older app rejects a Mac editor's Welcome. The addon updates the app it finds on the phone, so the two ship together.

Hidden Play Window on macOS: AppKit keeps part of a titled window on screen whatever position it is given (the -32000 position of D13 ends up at about -1070), so `set_tool_window` also makes the window fully transparent and click-through, drops its shadow and switches the game to an accessory app (no Dock icon, no menu bar). Godot stops drawing a window macOS reports as occluded, which the editor's window would cause, so the invisible window also floats at status level on every Space. Measured on an M5 with a Pixel 7a over USB, release build, 1280x720: 59 fps, touch to display p50 37 ms, p95 47 ms. Status: final, verify on more Macs.

## D11. Versioning

SemVer, tags `vX.Y.Z`. Foundation ends at `0.1.0`, Features at `0.2.0`, Polish ends at `0.3.0`. `1.0.0` is cut only after the owner publishes the app (owner decision, 2026-11-02). CHANGELOG follows Keep a Changelog. Status: final.
