# Handview

Play the game from your Godot editor on an Android phone. Handview streams the running game to the phone over USB and sends your touches back, so you can test a mobile game on a real screen without exporting it.

## Features

- Play, Pause, Step and Stop from the phone.
- Multi-touch input.
- Portrait and landscape. The game fits the phone screen, and Full Screen uses all of it.
- No setup per session: open the project and the phone finds it.
- Touch-to-display latency shown on the phone.
- Light and dark theme.

## Requirements

- Windows 10 or 11 (x64) with Godot 4.4 or newer.
- An NVIDIA GPU, or any PC with a Media Foundation H.264 encoder (most Windows PCs).
- An Android 8.0 or newer phone with USB debugging turned on.
- adb from the Android SDK platform-tools. Handview uses the SDK path in Editor Settings > Export > Android, or adb on `PATH`.

## Install

1. Download `handview-godot-addon-<version>.zip` from the [latest release](https://github.com/h1dr0nn/handview-remote/releases/latest).
2. Copy its `addons/handview` folder into your project's `addons` folder.
3. In Godot, open Project Settings > Plugins and enable Handview. Restart the editor once.

The phone app is installed for you over USB the first time you plug the phone in. You can also install `handview-<version>.apk` from the release yourself.

## Use

1. Plug in the phone and accept the USB debugging prompt.
2. Open the project in Godot. The Handview dock (right side) shows the phone as "Ready".
3. Open Handview on the phone. Your project appears under Devices and the app opens the game screen.
4. Press Play on the phone and play with touch.

Buttons on the game screen:

| Button | What it does |
|---|---|
| Play / Stop | Start or stop the main scene |
| Pause / Resume | Pause the game, then continue |
| Step | Advance one frame while paused |
| Full Screen | Show only the game. Press Back to leave |
| X | Leave the game screen. A game you started from the phone stops |

Good to know:

- A game started from the phone runs with its window hidden on the PC. A game started from the editor keeps its normal window and also shows on the phone.
- Several open projects each show up under Devices.
- The project's main scene must be set.

## For game code

While a game runs under Handview, `Handview.get_safe_area()` returns the phone's safe area (notch and system bars) in window pixels, and the `safe_area_changed` signal fires when it changes. Use it where you would use `DisplayServer.get_display_safe_area()`:

```gdscript
var safe: Rect2i = Handview.get_safe_area() if has_node("/root/Handview") else DisplayServer.get_display_safe_area()
```

Games run unchanged without this; only notch-aware layouts need it.

## Troubleshooting

| Problem | Fix |
|---|---|
| The phone shows "unauthorized" | Accept the USB debugging prompt. If it does not appear, reconnect the cable |
| "Set the Android SDK path..." in the dock | Set the SDK path in Editor Settings > Export > Android, then press Refresh |
| The dock says the native library is not loaded | Restart the editor once after installing the addon |
| The project does not appear on the phone | Check that the dock shows the phone as "Ready", open the app once, then press Refresh in the dock |
| "The installed Handview app was signed with a different key" | Choose "Uninstall and Install" in the dialog. This removes the old app and its data |
| Black or frozen video | Update the GPU driver (NVENC needs NVIDIA driver 531 or newer), then leave the game screen and join again |
| "The phone stopped responding" | Check the cable and avoid USB hubs. The phone joins again when the cable is back |

## Limitations

- Windows and USB only for now. Wi-Fi, QR pairing, sensors and keyboard input are marked "Soon" in the app.
- One phone plays at a time.
- The Game workspace's own pause button does not follow Pause from the phone.
- Projects using the Compatibility renderer or HDR 2D use a slower capture path.

## Development

Building from source, tests and the project layout are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). The wire format is in [protocol/SPEC.md](protocol/SPEC.md).

## License

[MIT](LICENSE). Copyright (c) 2026 h1dr0n.

Godot and the Godot logo are trademarks of the Godot Foundation. Handview is an independent project and is not affiliated with, sponsored by, or endorsed by the Godot Foundation.
