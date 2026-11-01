# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- CI jobs for the native tests, the Android unit tests and lint, and the addon tests on Godot 4.4.1.
- Release packaging: signed release APK from environment secrets, addon zip with checksum (`tools/package`), and a tag-triggered release workflow.

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
