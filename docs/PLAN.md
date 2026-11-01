# Plan

Handview first production release: Godot 4.4+ host on Windows, Android app, USB only. Scope and rules come from the design docs (Notion "handview") and the implementation brief; open choices are recorded in [DECISIONS.md](DECISIONS.md).

Agents: **Codex** writes code that needs no running editor. **agy** (Antigravity) researches, drafts and reviews. **Claude** runs Godot, the Android emulator and builds, integrates and does final review; Claude subagents use **Haiku** for mechanical work, **Sonnet** for features, tests and docs, **Opus** only for hard design or debugging.

Every task ends with build + tests run by Claude. Commits use Conventional Commits, local only.

## Phase 1. Foundation (v0.1.0)

Goal: every shared building block exists and is tested on its own; the open technical questions are answered by measurement.

| ID | Task | Owner | Depends on |
|---|---|---|---|
| F1 | M0 scaffold: monorepo, CMake, GDExtension skeleton, Android skeleton, CI | Claude | |
| F2 | Research: scrcpy, Sunshine/Moonlight, MediaCodec, Godot capture and window behaviour (`docs/research/`) | agy | |
| F3 | `protocol/SPEC.md` + shared test vectors `protocol/vectors/` + C++ codec for every message (`native/core/.../protocol`) with tests | Codex | F1 |
| F4 | Kotlin codec for every message (`android-app/protocol`) reading the same vectors | Codex | F3 |
| F5 | Core network: framed TCP reader/writer, loopback server and client, Winsock (`native/core/.../net`) with tests | Codex | F3 |
| F6 | Flow control logic: in-flight limit, keyframe rate limit, decoder backpressure controller, Resolution Cap math (pure, unit tested) | Sonnet | F3 |
| F7 | Spike GPU Interop: measure `texture_get_data_async` readback vs shared handle, pick encoder input path, report in `docs/spikes/` | Claude (Opus if stuck) | F1, VS toolchain |
| F8 | Spike Hidden Play Window: off-screen, no focus, no taskbar, embed mode; report | Claude | F1 |
| F9 | Spike Pause / Step from an addon; report only, no UI | Sonnet | F2 |
| F10 | Mock host and mock client (`tools/`), built on the core | Sonnet | F5 |
| F11 | Cross-review F3 to F10 | agy | all above |

Done when: native and Android test suites pass locally, both codecs agree on all vectors, mock client talks to mock host over loopback, three spike reports written with numbers and a recommendation, `v0.1.0` tagged.

## Phase 2. Features (v0.2.0)

Goal: the vertical slice. USB Launch, handshake, Play from the phone, capture, H.264, MediaCodec, display, touch back, touch-to-display overlay, portrait and landscape.

| ID | Task | Owner | Depends on |
|---|---|---|---|
| T1 | Encoder front end: D3D11 device, frame upload, Video Processor scale + NV12 (BT.709 limited) | Codex | F7 |
| T2 | NVENC backend (dynamic load, nv-codec-headers), low-latency config, VUI, forced IDR | Codex | T1 |
| T3 | Media Foundation backend: MFTEnumEx, async hardware MFT, software fallback, Annex-B check | Codex | T1 |
| T4 | Editor session server: Hello/Welcome/Reject, token, commands, relay to game link (D5, D7) | Codex | F5, F6 |
| T5 | Game runtime: capture via `frame_post_draw`, encode thread, link client, touch queue, `lastTouchSeq` | Claude + Sonnet | T1 to T4 |
| T6 | GDExtension bindings: `HandviewEditorSession`, `HandviewRuntime` | Sonnet | T4, T5 |
| T7 | Editor addon: dock UI, adb discovery, install / version check, reverse, launch intent, Play / Stop, crash detection | Sonnet | T6 |
| T8 | Runtime autoload: activation from env (D6), touch to `InputEventScreenTouch` / `InputEventScreenDrag`, hidden window per F8 | Sonnet | T6, F8 |
| T9 | Android session client: control + video sockets, handshake, FrameAck, DecoderStats, Ping | Codex | F4 |
| T10 | Android decoder: MediaCodec async on SurfaceView, drop rules, flush to IDR, reset on error | Codex | T9 |
| T11 | Android UI: USB Waiting, Cast portrait, Cast landscape, overlay, touch capture, latency tracker | Sonnet | T9, T10 |
| T12 | End-to-end run: Godot sample scene, emulator or phone, NVENC and MF | Claude | all above |
| T13 | Review T1 to T11 | agy | |

Done when: T12 works in both orientations on both encoder backends, all tests pass, `v0.2.0` tagged.

## Phase 3. Polish (v1.0.0)

Goal: production quality and release packaging.

| ID | Task | Owner | Depends on |
|---|---|---|---|
| P1 | Hardening: encoder / decoder errors, disconnect cleanup, game crash, repeated Play / Stop, cable pull, several devices, busy port, missing / old / differently signed APK | Claude + Sonnet | Phase 2 |
| P2 | 30 minute soak: latency drift, memory, leaked adb processes | Claude | P1 |
| P3 | Latency test scene (`tools/latency-scene`) | Sonnet | Phase 2 |
| P4 | Packaging: release GDExtension, signed APK from CI secrets, addon zip, release workflow | Sonnet | P1 |
| P5 | README (install, requirements, limits, troubleshooting), CHANGELOG, compatibility and latency table | agy draft, Claude final | P2 |
| P6 | Final review | agy + Claude | all |

Done when: Definition of Done of the brief holds, `v1.0.0` tagged.
