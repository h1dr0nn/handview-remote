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

## D14. Pause / Step stays off until the owner approves

Spike 3 showed Pause and Step work from an addon through `EditorDebuggerSession.send_message("scene:suspend_changed", [bool])` and `("scene:next_frame", [])`. The feature is still `Proposed` in the design docs, and the brief says not to build its UI before approval, so the Godot host answers Pause / Step with `CommandResult{Ignored}` and the app keeps those buttons hidden. Enabling it later means wiring those two messages in the addon, pausing capture while suspended, and showing the buttons for host Godot. Status: waiting for the owner.

## D15. Game lifecycle signals

The loopback link is the primary signal that the game runs (LinkHello) or ended (link closed). `EditorDebuggerSession.stopped` and `EditorInterface.is_playing_scene()` back it up. A crashing game process stays alive about 7 s while showing its crash handler; the link drop or the debugger stop then reports `EditorState{Stopped}`. Status: final.

## D16. Store naming and trademark wording

Research (`docs/research/naming-and-trademarks.md`) found no conflicting "Handview" app, project or software trademark. Google Play titles are limited to 30 characters and listings must not claim features that do not work yet, so the Play title is "Handview" and the short description reads "Game View remote for Godot" until the Unity Host phase ships, when it becomes "Game View remote for Godot and Unity" (the brief's subtitle). Third-party names are used only descriptively ("for Godot"), never their logos, and the README carries the trademark notice. The registry searches must be repeated by hand before the first store submission. Status: final.

## D17. First-run editor exits and GDExtension docs

On Godot 4.7.2, a project's very first editor run that exits immediately (`--import`, or `--editor --quit`) segfaults on exit whenever the project contains a GDExtension that registers any class, even one trivial `RefCounted` class with a single method; with no registered class it exits cleanly, and later runs exit cleanly too. Letting the editor run longer (`--quit-after 1500`) also exits cleanly, so it is a race with the first-run documentation generation for extension classes, not Handview code. Normal interactive use is not affected. CI therefore prepares the project with `--editor --quit-after 1500` instead of `--import`. Separately, the plugin now starts its background adb tasks only after the editor's first filesystem scan has settled and never in headless editors (unless the loopback test hook is set), because worker threads running GDScript during the first import's script reload produced script errors. Status: final; revisit if a Godot release fixes the exit race.

## D11. Versioning

SemVer, tags `vX.Y.Z`. Foundation ends at `0.1.0`, Features at `0.2.0`, Polish (first production release) at `1.0.0`. CHANGELOG follows Keep a Changelog. Status: final.
