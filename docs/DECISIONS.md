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

The runtime autoload must only activate when Handview launched the game. The editor sets `HANDVIEW_LINK_PORT` and `HANDVIEW_LINK_TOKEN` with `OS.set_environment()` right before `EditorInterface.play_main_scene()`; the child process inherits them, and the editor clears them after launch. The brief mentions command line arguments; the only way to add those from an addon is the project setting `editor/run/main_run_args`, which would write to `project.godot` and show up in the user's git diff. Status: final.

## D7. Internal editor to game link reuses the wire format

The loopback link uses the same framing and message codec as the phone protocol, plus link-only message types in the `0x0200` range (never sent to the phone). One codec, one set of tests. The link listener binds `127.0.0.1` on an OS-assigned port and requires the per-run link token. Status: final.

## D8. Session port range

Phone sessions use `PORT` from `38500` to `38509`: the first free port is used, and the same number is used on both sides of `adb reverse`. The listener binds `127.0.0.1` only. Status: final.

## D9. Token encoding in the launch intent

The 32 byte one-time token is generated in the editor (`Crypto.generate_random_bytes(32)`) and passed as a 64 character lowercase hex string extra `token`; the app decodes it back to 32 bytes for `Hello`. Status: final.

## D10. Color: BT.709 limited range, signalled in the stream

Godot's root viewport holds 8-bit sRGB-encoded RGB. Conversion to NV12 uses BT.709 limited range, and the SPS VUI carries the color description so Android decoders do not guess BT.601. Status: final, verify on devices.

## D11. Versioning

SemVer, tags `vX.Y.Z`. Foundation ends at `0.1.0`, Features at `0.2.0`, Polish (first production release) at `1.0.0`. CHANGELOG follows Keep a Changelog. Status: final.
