# Development

Building, testing and contributing. For using Handview, see the [README](../README.md).

## Build from source

Clone with submodules:

```bash
git clone --recursive https://github.com/h1dr0nn/handview-remote
```

### Native core and GDExtension

Needs Visual Studio 2022 or 2026 with the Desktop development with C++ workload, CMake 3.24 or newer and Python 3 (godot-cpp generates its bindings with it).

```bash
cd native
cmake --preset windows-x64 -DGODOTCPP_TARGET=template_debug
cmake --build --preset windows-x64-debug --parallel
ctest --preset windows-x64-debug
```

The GDExtension DLL is written to `godot-addon/addons/handview/bin/`. For a release build use `-DGODOTCPP_TARGET=template_release` and `--preset windows-x64-release`.

On an Apple Silicon Mac, with the Xcode Command Line Tools, CMake and Ninja (`brew install cmake ninja`) and Python 3:

```bash
cd native
cmake --preset macos-arm64 -DGODOTCPP_TARGET=template_debug
cmake --build --preset macos-arm64-debug --parallel
ctest --preset macos-arm64-debug
```

This writes `handview.macos.template_debug.arm64.dylib` to the same folder. For a release build configure with `-DGODOTCPP_TARGET=template_release` and build `--preset macos-arm64-release`. The mock client and mock host are Windows only and are not built on macOS.

To build and test only the core, on Windows or macOS and without godot-cpp:

```bash
cd native
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only
```

### Android app

Needs JDK 17 and the Android SDK (API 35). Point Gradle at the SDK with `ANDROID_HOME` or a `local.properties` file with `sdk.dir=...`.

```bash
cd android-app
./gradlew :protocol:test :client:test :app:assembleDebug :app:lintDebug
./gradlew :app:assembleRelease
```

Without the signing variables described in [tools/package/README.md](../tools/package/README.md), `assembleRelease` produces an unsigned APK. See [android-app/README.md](../android-app/README.md).

### Addon tests

With the debug DLL built, from the repository root and using a Godot 4.4 or newer executable:

```bash
godot --headless --path godot-addon --import
godot --headless --path godot-addon -s res://tests/run_tests.gd
```

`godot-addon` is also a Godot project with the plugin enabled. Its main scene is the latency test scene, see [tools/README.md](../tools/README.md).

### Quick end-to-end run with the mock client

The mock client is a stand-in for the phone. In PowerShell, from the repository root, after building the native project:

```powershell
$env:HANDVIEW_LOOPBACK_FILE = "C:\Temp\handview-loopback.json"
$env:HANDVIEW_LOOPBACK_AUTOSTART = "1"
godot --editor --path godot-addon
```

The editor starts a session on its own, lists a "Loopback (Test)" device and writes the port and token to the file. Then run:

```powershell
native\build\windows-x64\tools\Debug\handview-mock-client.exe --loopback-file C:\Temp\handview-loopback.json --play --decode --tap-every 300 --stop-at-end --duration 10
```

The other tools and their options are in [tools/README.md](../tools/README.md). Packaging and releasing are described in [tools/package/README.md](../tools/package/README.md).

## Project layout

| Folder | Content |
|---|---|
| `protocol/` | Wire format specification (`SPEC.md`) and shared test vectors (`vectors/`) |
| `native/core/` | C++ core: protocol, network, session server, flow control, encoders. No engine dependency |
| `native/godot-extension/` | GDExtension wrapping the core for Godot |
| `native/unity-plugin/` | Reserved for the Unity phase |
| `native/third_party/` | Submodules: godot-cpp, nv-codec-headers (Windows only) |
| `godot-addon/addons/handview/` | The addon: editor dock, runtime autoload, GDExtension binaries, bundled APK |
| `godot-addon/latency/` | Latency test scene of the addon's dev project |
| `godot-addon/tests/` | Headless GDScript tests for the addon |
| `android-app/` | Kotlin and Jetpack Compose app, with the `:client` and `:protocol` modules |
| `tools/` | Mock client and mock host |
| `tools/package/` | Release packaging script and changelog extraction |
| `tools/spikes/` | Spike projects behind the reports in `docs/spikes/` |
| `docs/` | `PLAN.md`, `DECISIONS.md`, spike reports and research notes |
| `.github/workflows/` | CI and release workflows |

## Contributing

Use [Conventional Commits](https://www.conventionalcommits.org/) for commit messages and add user-visible changes to [CHANGELOG.md](../CHANGELOG.md) under Unreleased. A change to the wire format must update [protocol/SPEC.md](../protocol/SPEC.md), the vectors in `protocol/vectors/` and both codecs (C++ and Kotlin) together. Decisions that settle an open choice go into [docs/DECISIONS.md](DECISIONS.md).

## How it works

- The Godot editor owns the session. While it runs it listens on one local port (38500 to 38509), forwards it to every plugged in phone with `adb reverse` and offers the phone app a one-time token with `adb shell am broadcast` (DECISIONS D18).
- When you press Play on the phone, the editor runs the project's main scene as a normal Godot play. The Handview autoload inside the game process stays inactive unless the editor has set up the link to it, which it does while a phone is connected.
- The game process captures each rendered frame, scales and converts it, and encodes it to H.264 with NVENC or Media Foundation on Windows, VideoToolbox on macOS.
- The editor relays the encoded frames to the phone over the USB connection. Both phone sockets connect to the editor, so the same phone session survives any number of Play and Stop cycles.
- The phone decodes with MediaCodec and draws on a SurfaceView. The editor sends it the game state, so the app shows "Not playing" and the Play button when the game is stopped.
- Touches on the phone go back to the editor, then to the game, which injects them as `InputEventScreenTouch` and `InputEventScreenDrag`.
- The phone measures the time from a touch to the first displayed frame that includes it and shows it as an overlay.

```txt
Phone                         PC
+-------------------+  USB   +----------------------+  loopback  +--------------------+
| Handview app      |<------>| Godot editor         |<---------->| Game process       |
| MediaCodec decode |  adb   | dock, session, relay |    link    | capture, encode    |
| touch, overlay    | reverse|                      |            | touch injection    |
+-------------------+        +----------------------+            +--------------------+
   video and state  <------- video, state                <------- H.264 frames
   touch, commands  -------> touch, commands             -------> touch
```

The wire format is described in [protocol/SPEC.md](../protocol/SPEC.md).
