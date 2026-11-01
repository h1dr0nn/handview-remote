# Internal test tools

Windows C++20 executables for testing the native Handview pipeline. They link
`handview::core`; no Godot or phone is needed for the loopback test.

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
Android installation. `latency-scene/` remains reserved for camera-based tests.
