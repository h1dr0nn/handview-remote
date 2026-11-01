# Handview

Game View remote for Godot and Unity.

Handview streams the game running in your editor on a Windows PC to an Android phone over USB. You press Play and Stop on the phone, see the game in portrait or landscape, and touch input goes back to the game. An overlay shows touch-to-display latency.

> Status: early development (milestone M0, project setup). Nothing is usable yet.

## Scope of the first release

- Host: Godot 4.4+ on Windows only. Unity support comes in a later phase.
- Connection: USB only (`adb reverse`).
- Video: H.264, encoded with NVENC on NVIDIA GPUs or Media Foundation elsewhere, decoded with MediaCodec on the phone.

## Repository layout

```txt
handview-remote/
├── protocol/            # shared wire format notes, test vectors (M1)
├── native/
│   ├── core/            # C++ Shared Native Core: protocol, network, encoders. No engine dependency
│   ├── godot-extension/ # GDExtension wrapping the core for Godot
│   ├── unity-plugin/    # reserved for the Unity Host phase
│   └── third_party/     # godot-cpp (git submodule)
├── godot-addon/
│   └── addons/handview/ # Godot editor addon + runtime, GDExtension binaries, bundled APK
├── android-app/         # Kotlin + Jetpack Compose app, :protocol module
├── tools/               # internal test tools (mock host, mock client, latency scene)
└── .github/workflows/   # CI
```

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/h1dr0nn/handview-remote
```

### Native core and GDExtension (Windows)

Needs Visual Studio with the "Desktop development with C++" workload, CMake 3.24+ and Python 3 (used by godot-cpp to generate bindings).

```bash
cd native
cmake --preset windows-x64 -DGODOTCPP_TARGET=template_debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

The GDExtension DLL is written to `godot-addon/addons/handview/bin/`.

To build and test only the core (any platform, no godot-cpp):

```bash
cd native
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only
```

### Android app

Needs JDK 17 and the Android SDK (API 35).

```bash
cd android-app
./gradlew :protocol:test :app:assembleDebug
```

See [android-app/README.md](android-app/README.md) for details.

## License

[MIT](LICENSE)
