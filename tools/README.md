# Internal tools

| Tool | Location | Purpose |
| --- | --- | --- |
| Mock client | `tools/mock-client` | PC stand-in for the phone, with H.264 decoding (see below) |
| Mock host | `tools/mock-host` | Stand-in for the editor, for testing the Android app (see below) |
| Packaging | `tools/package` | Builds the addon zip and prepares release notes, see [package/README.md](package/README.md) |
| Spikes | `tools/spikes` | Godot projects behind the reports in `docs/spikes/` (see below) |
| Latency scene | `godot-addon/latency` | Test scene in the addon's dev project (see below) |

The mock client and the mock host are Windows C++20 executables for testing the
native Handview pipeline. They link `handview::core`; no Godot or phone is
needed for the loopback test. After the build below they are in
`native/build/windows-x64/tools/Debug/` as `handview-mock-client.exe` and
`handview-mock-host.exe`.

Build from `native` using the installed CMake and MSVC:

```powershell
cmake --preset windows-x64 -DGODOTCPP_TARGET=template_debug
cmake --build --preset windows-x64-debug --parallel
ctest --preset windows-x64-debug --output-on-failure
```

`HANDVIEW_BUILD_TOOLS` defaults to `ON`. Set it to `OFF` to omit these tools.
`HANDVIEW_BUILD_TESTS=ON` registers `tools_loopback_test`, which launches the
host for six seconds and the client for four seconds with Play, 300 ms taps,
and Media Foundation decoding. It requires a successful client exit, positive
frame and tap counts, and every generated tap resolved. Failure logs remain in
the Windows temporary directory, with paths printed by the test driver.

## Mock client

```powershell
handview-mock-client --loopback-file C:\Temp\handview-loopback.json --wait 30 --play --decode --tap-every 300 --stop-at-end --duration 10 --json stats.json
handview-mock-client --port 38500 --token <64-hex-characters> --play --decode
```

Use either `--port N --token HEX64` or `--loopback-file PATH`. The file contains
`{"port":38500,"token":"<64 hex characters>"}`. The client polls for a complete
credential file for up to `--wait` seconds (default 30), including changes to
an incomplete file. Failed connections or rejected stale credentials wait for a
changed file within the same timeout. Tokens are single use; use fresh credentials
for each run.

Other options:

| Option | Default | Meaning |
| --- | --- | --- |
| `--duration SECONDS` | 10 | Session observation time, positive integer |
| `--play` | off | Send Play once, after Welcome and a Stopped state |
| `--stop-at-end` | off | Send Stop before the normal disconnect |
| `--tap-every MS` | 0 | Send center Began/Ended pairs while Playing |
| `--screen WxH` | 1080x2400 | Reported screen size |
| `--decode` | off | Decode all frames with the Windows H.264 decoder MFT |
| `--json PATH` | none | Write the final summary as JSON |
| `--cycles N` | 0 | Play for `--cycle-seconds`, Stop, and repeat N times; every cycle must receive frames |
| `--cycle-seconds SECONDS` | 4 | Length of each cycle with `--cycles` |

The summary reports received frames, keyframes, H.264 payload bytes per second,
received FPS, decoder errors, current stream dimensions, generated/resolved
taps, tap latency p50/p95/max in milliseconds, and protocol errors. Rates use
the session observation time, including time spent Stopped. Tap latency measures
PC send to receipt of the first frame covering the Began sequence, using serial
sequence comparisons. It does not measure phone display latency. Taps stop
250 ms before completion so normal round trips can resolve.

Every frame is acknowledged immediately without `--decode`, or after decoder
output with it. The decoder is configured for low latency and drained at normal
completion. The next frame after a config change must contain SPS/PPS flags and
an SPS matching the announced dimensions. The first session frame and the first
frame after an error are checked too. Exit status is nonzero for transport,
protocol or decoder errors, or if Play was requested/observed without frames.
A connected session that stays Stopped may succeed without frames.

## Mock host

```powershell
handview-mock-host --adb C:\Android\platform-tools\adb.exe --serial DEVICE_SERIAL --size 1280x720 --fps 60
handview-mock-host --no-launch --duration 6
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--adb PATH` | adb | ADB executable |
| `--serial S` | auto for one device | Required when multiple devices are authorized |
| `--port N` | first free 38500..38509 | Loopback phone listener |
| `--size WxH` | 1280x720 | Synthetic source dimensions, even numbers |
| `--fps N` | 60 | Source and advertised maximum FPS, 1..240 |
| `--duration SECONDS` | 0 | 0 runs until Ctrl+C |
| `--no-launch` | off | Skip all ADB calls and print credentials |

The credential line is `HANDVIEW port=P token=HEX64`. The host starts Stopped.
Play starts synthetic capture; Stop pauses capture; Pause and Step are ignored.
The source is a moving gradient, a 16-bit frame counter displayed as large
blocks across the top, and a white dot at the latest touch position. The native
stream controller handles resolution caps, encoding, frame pacing and flow
control. Session events and stream status are printed to the console.

The Android app must already be installed. With launch enabled, the host selects
one authorized device or uses `--serial`, installs the reverse, launches the
activity with token and port extras, and removes its reverse on normal exit or
Ctrl+C. Every device operation uses `-s`; device discovery uses `adb devices -l`.
Forced process termination cannot guarantee reverse cleanup. The token is
single use, so restart the host for a fresh phone connection after disconnect.

The tools assume Windows Media Foundation H.264 components are installed and a
core encoder backend is available. `--no-launch` requires neither ADB nor an
Android installation.

## Latency scene

`godot-addon/latency/latency.tscn` is the main scene of the addon's dev project
`godot-addon`. Every touch flips the screen between a dark and a light color,
and the scene shows a frame counter and the last touch position with a marker.
Use it to read the touch-to-display overlay on the phone, or to film the phone
next to a 240 fps camera.

1. Build the GDExtension (the commands above) so that `godot-addon/addons/handview/bin/` has the DLL.
2. Open `godot-addon` in Godot 4.4 or newer. The Handview plugin is enabled in this project.
3. In the Handview dock, press Start Session for the phone (or start it with the loopback variables below).
4. Press Play on the phone and tap the screen.

For an unattended run with the mock client, set `HANDVIEW_LOOPBACK_FILE` to a
file path and `HANDVIEW_LOOPBACK_AUTOSTART` to `1` before starting the editor.
The editor then starts a session by itself, lists a "Loopback (Test)" device
and writes `{"port":...,"token":"..."}` to that file, which is what
`handview-mock-client --loopback-file` reads.

```powershell
$env:HANDVIEW_LOOPBACK_FILE = "C:\Temp\handview-loopback.json"
$env:HANDVIEW_LOOPBACK_AUTOSTART = "1"
godot --editor --path godot-addon
```

## Spikes

`tools/spikes` holds the Godot projects used for the spike reports. They are
not part of the addon or the release.

- `capture` (spike 1 and 2, [01](../docs/spikes/01-gpu-interop.md), [02](../docs/spikes/02-hidden-window.md)): renders a 1920x1080 scene and measures viewport readback and window modes.

  ```bash
  godot --path tools/spikes/capture [--disable-vsync] [--rendering-driver d3d12] [--rendering-method mobile|gl_compatibility] -- mode=sync|async|image|none window=normal|offscreen|nofocus|minimized frames=600
  ```

- `editor-control` (spike 3, [03](../docs/spikes/03-editor-control.md)): an editor plugin that runs unattended experiments on environment variables, game lifecycle, embedding and pause / step, writes `results.txt` and quits the editor.

  ```bash
  HANDVIEW_SPIKE_RUN=1,2 godot --path tools/spikes/editor-control --editor
  ```

  `HANDVIEW_SPIKE_RUN` takes experiment numbers (1 to 4, comma separated) or `all`.

