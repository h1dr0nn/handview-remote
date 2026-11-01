# Handview

Handview streams the game running in the Godot editor on a Windows PC to an Android phone over USB. You press Play and Stop on the phone, see the game in portrait or landscape, and touch input goes back to the game as touch events. An overlay on the phone shows touch-to-display latency.

Status: version 0.1.0. MVP scope: Godot 4.4+ on Windows, USB only. Unity support comes in a later phase. Measurements on real phones are still pending (see [Latency](#latency)).

## How it works

- The Godot editor owns the session. The Handview dock starts it, launches the phone app with `adb` and listens on one local port (38500 to 38509) that is forwarded to the phone with `adb reverse`.
- When you press Play on the phone, the editor runs the project's main scene as a normal Godot play. The Handview autoload inside the game process stays inactive unless the editor has set up the link to it, which it does while a phone is connected.
- The game process captures each rendered frame, scales and converts it, and encodes it to H.264 with NVENC or Media Foundation.
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

The wire format is described in [protocol/SPEC.md](protocol/SPEC.md).

## Requirements

| Component | Requirement |
|---|---|
| PC | Windows 10 or 11, x64 |
| Godot | 4.4 or newer. Forward+, Mobile and Compatibility renderers work. Compatibility uses a slower readback path |
| GPU and encoder | An NVIDIA GPU (NVENC), or any GPU or CPU with a Media Foundation H.264 encoder. A software encoder works too and is shown as "Software" on the phone |
| Phone | Android 8.0 (API 26) or newer, with USB debugging turned on |
| adb | Android SDK platform-tools, set in Godot Editor Settings > Export > Android, or available on `PATH` |

## Install

### Godot addon

1. Download `handview-godot-addon-<version>.zip` from the [latest release](https://github.com/h1dr0nn/handview-remote/releases/latest). The release also has a `.sha256` file for the zip.
2. Copy the `addons/handview` folder from the zip into the `addons` folder of your Godot project.
3. Open the project in Godot, go to Project Settings > Plugins and enable Handview. The plugin adds the "Handview" autoload.

### Phone app

The Handview dock installs the app over USB from the APK bundled in the addon. It does this on Start Session when the app is missing or older than the bundled one. To install it yourself, download `handview-<version>.apk` from the release and install it with `adb install`, or copy it to the phone.

## Use

1. Connect the phone over USB and turn on USB debugging. Accept the USB debugging prompt on the phone.
2. In Godot, open the Handview dock. It is a tab in the right-hand dock area. Press Refresh if the phone is not listed.
3. Select the phone in the Devices list. A single device is selected automatically and the choice is remembered.
4. Press Start Session. The dock shows its progress ("Preparing the phone...", "Installing the Handview app..." when needed, "Starting the Handview app...", "Waiting for the phone to connect...") and then "Connected to <model>". It also shows the encoder line and the phone's screen size.
5. The phone opens Handview, shows "Connecting", and then the cast screen with the status line "Connected · USB · <encoder> · <fps> FPS" and the Play button.
6. Press Play on the phone. The project's main scene must be set; otherwise the dock shows "No main scene is set in the project."
7. Play with touch. Multi-touch is supported. The app locks the screen to portrait or landscape to match the game's aspect ratio.
8. Press Stop on the phone to stop the game. The session stays open and you can press Play again.
9. To end the session, press Disconnect on the phone (it returns to the "Waiting for USB Session" screen) or press Stop Session in the dock.

Window behavior on the PC:

- A play started from the phone hides the game window: it is moved off-screen, never takes focus and has no taskbar button. The editor keeps keyboard focus. Embedding the game in the Game workspace is turned off for these plays and restored afterwards.
- A play started from the editor (Play button or F5) keeps its normal window, and it streams to the phone as long as a phone is connected.

## Latency

The overlay on the phone shows "Touch to display p50 X ms · p95 Y ms". Each sample runs from the time of a touch event on the phone, on the phone's clock, to the time MediaCodec reports that the first frame containing that touch was rendered. The values are the 50th and 95th percentile of the samples from the last ten seconds. A "-" means there is no sample yet.

What the number does not include: the delay of the phone's display panel after the frame is rendered.

Measured so far, on the PC side only:

| Setup | Metric | p50 | p95 |
|---|---|---|---|
| Godot 4.7.2 editor, RTX 3060 with NVENC, 1280x720 at 60 fps, mock client over loopback, no decode errors | Touch sent on the PC to the first frame covering it arriving at the mock client | 15.4 ms | 28.7 ms |

Encoder timings from the encoder tests, per 720p frame scaled from 1080p on an RTX 3060 machine: NVENC about 4 ms, Media Foundation hardware about 5 ms, Media Foundation software about 5 to 7 ms. Reading the frame back from Godot costs about 2.5 ms per 1080p frame ([spike 1](docs/spikes/01-gpu-interop.md)).

Results measured on phones will be added to the release notes.

## Known limitations

- Windows only. The host is Godot 4.4 or newer on Windows 10 or 11 x64.
- USB only. There is no Wi-Fi connection.
- Pause and Step use the editor debugger, like the Game workspace. Its own pause button does not follow them ([DECISIONS D14](docs/DECISIONS.md)).
- One phone at a time. Starting a session for another device replaces the current one.
- Phone plays rely on finding the Game workspace item "Embed Game on Next Play". If a Godot version renames or removes it, the game runs embedded in the editor.
- Keyboard, sensors and the Back button are not forwarded to the game. Only touch is. The app shows these, Wi-Fi, QR pairing, video options and auto reconnect as "Soon".
- Viewports with HDR 2D enabled fall back to a slower readback path.
- The Compatibility renderer uses a slower readback path.
- Development and measurements ran on Godot 4.7.2. CI runs the addon tests on Godot 4.4.1. The Game workspace behavior on 4.4 was checked in the source only. Phone behavior has been tested with the mock tools; verification on real devices is pending.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The device shows as "unauthorized" | Accept the USB debugging prompt on the phone. If it does not appear, unplug and reconnect the cable, or revoke USB debugging authorizations in the phone's developer options |
| "Set the Android SDK path in Editor Settings > Export > Android, or add adb to PATH." | Handview did not find `adb`. Set the SDK path in Editor Settings > Export > Android, or add platform-tools to `PATH`, then press Refresh |
| Installing the app fails, or "The installed Handview app was signed with a different key." | The dock shows the full adb output. For a signature mismatch it asks before uninstalling, in the "Replace Handview App" dialog. "Uninstall and Install" removes the old app and its data from the phone. "Cancel" keeps it |
| "No free port between 38500 and 38509." | Another program or another Godot editor uses all ten ports. Close it and press Start Session again |
| "No H.264 encoder found" | The dock found neither NVENC nor a Media Foundation H.264 encoder. Update the GPU driver and check that the Media Foundation H.264 encoder is installed on this Windows edition |
| Video is black or frozen | The app asks the editor for a new keyframe by itself after a decoder error. Check the encoder line in the dock. Update the GPU driver; NVENC needs NVIDIA driver 531 or newer. Then stop and start the session |
| "Session ended: the phone stopped responding." | The editor received nothing from the phone for 5 seconds. Check the USB cable and port, avoid hubs, and start the session again |
| The game shows inside the editor | The "Embed Game on Next Play" item was not found, so the play is embedded. Turn embedding off in the Game workspace and play again |

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

To build and test only the core, on any platform and without godot-cpp:

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

Without the signing variables described in [tools/package/README.md](tools/package/README.md), `assembleRelease` produces an unsigned APK. See [android-app/README.md](android-app/README.md).

### Addon tests

With the debug DLL built, from the repository root and using a Godot 4.4 or newer executable:

```bash
godot --headless --path godot-addon --import
godot --headless --path godot-addon -s res://tests/run_tests.gd
```

`godot-addon` is also a Godot project with the plugin enabled. Its main scene is the latency test scene, see [tools/README.md](tools/README.md).

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

The other tools and their options are in [tools/README.md](tools/README.md). Packaging and releasing are described in [tools/package/README.md](tools/package/README.md).

## Project layout

| Folder | Content |
|---|---|
| `protocol/` | Wire format specification (`SPEC.md`) and shared test vectors (`vectors/`) |
| `native/core/` | C++ core: protocol, network, session server, flow control, encoders. No engine dependency |
| `native/godot-extension/` | GDExtension wrapping the core for Godot |
| `native/unity-plugin/` | Reserved for the Unity phase |
| `native/third_party/` | Submodules: godot-cpp, nv-codec-headers |
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

Use [Conventional Commits](https://www.conventionalcommits.org/) for commit messages and add user-visible changes to [CHANGELOG.md](CHANGELOG.md) under Unreleased. A change to the wire format must update [protocol/SPEC.md](protocol/SPEC.md), the vectors in `protocol/vectors/` and both codecs (C++ and Kotlin) together. Decisions that settle an open choice go into [docs/DECISIONS.md](docs/DECISIONS.md).

## License

[MIT](LICENSE). Copyright (c) 2026 h1dr0n.

Godot and the Godot logo are trademarks of the Godot Foundation. Unity is a trademark or registered trademark of Unity Technologies or its affiliates in the U.S. and elsewhere. Handview is an independent project and is not affiliated with, sponsored by, or endorsed by either.
