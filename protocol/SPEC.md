# Handview protocol v1

Status: Wire Format v1 and the two-socket topology are a baseline approved for the MVP; changes must update this file, `vectors/` and both codecs together.

"Host" is the PC side (the Godot editor in the MVP). "App" is the Android app.

## 1. Topology

- Two TCP connections from the app to the same host `PORT` (through `adb reverse`), both with `TCP_NODELAY`.
  - **Control** opens first: handshake, play control, editor state, touch, `FrameAck`, stats, `Ping`.
  - **Video** opens after `Welcome` and sends `VideoAttach{sessionId}` first. After that it carries only `VideoFrame` (host to app).
- Do not rely on `SO_SNDBUF`: `adb reverse` buffers data in adb server and adbd. Flow control is done in the application (section 6.5).
- Video is sent on its own thread and never blocks a render thread.
- Inside the PC, the game process talks to the editor over a loopback **link** (section 7).

## 2. Framing rules

- Little-endian everywhere.
- Every message, `VideoFrame` included, starts with a 6 byte header: `[type: u16][length: u32]`. `length` is the payload size in bytes.
- Payload limits: control socket and link 64 KiB, except link `VideoFrame`; video socket and link `VideoFrame` 32 MiB. A larger `length` closes the session with `Bye{ProtocolError}`.
- Parse field by field. Fields are not aligned; never cast a buffer pointer to a struct.
- Unknown `type`: skip `length` payload bytes, log once per type.
- Known type with a wrong payload size or an out-of-range enum value: `Bye{ProtocolError}` and close.
- No magic bytes. A framing error closes the session.
- Timestamps on the wire are microseconds. App: Android monotonic clock. Host: `QueryPerformanceCounter`.
- Sequence numbers are `u32`, start at 1 in each session, and compare as serial numbers: `a` is newer than or equal to `b` when `(int32)(a - b) >= 0`.
- `f32` is IEEE 754 binary32, little-endian.
- Strings are UTF-8 without terminator, length given by a preceding field.

## 3. Messages

Sizes are payload sizes. `N` = `decoderCount`, `M` = `modelLen`, `C` = `count`, `D` = H.264 data length.

| Type | Name | Direction | Payload | Size |
|---|---|---|---|---|
| `0x0001` | Hello | App to Host | `u16 protoVersion`, `u8[32] token`, `u16 screenW`, `u16 screenH`, `u16 dpi`, `u8 refreshHz`, `u8 decoderCount`, `u16 modelLen`, then N x `{u8 codec, u8 profile, u8 level, u16 maxW, u16 maxH}`, then `utf8[M] model` | 44 + 7N + M |
| `0x0002` | Welcome | Host to App | `u8[16] sessionId`, `u8 host`, `u8 codec`, `u8 encoder`, `u16 width`, `u16 height`, `u8 orientation`, `u8 fps`, `u32 bitrateKbps`, `u8 editorState` | 30 |
| `0x0003` | Reject | Host to App | `u8 reason` | 1 |
| `0x0004` | VideoAttach | App to Host (video) | `u8[16] sessionId` | 16 |
| `0x0010` | Command | App to Host | `u8 cmd` | 1 |
| `0x0011` | CommandResult | Host to App | `u8 cmd`, `u8 result`, `u8 editorState` | 3 |
| `0x0012` | EditorState | Host to App | `u8 state` | 1 |
| `0x0020` | Touch | App to Host | `u32 touchSeq`, `u64 eventTimeUs`, `u8 count`, then C x `{u8 id, u8 phase, f32 x, f32 y, f32 pressure}` | 13 + 14C |
| `0x0021` | Sensor | App to Host | `u8 kind`, `f32 x`, `f32 y`, `f32 z` | 13 |
| `0x0022` | Key | App to Host | `u32 keycode`, `u32 unicode`, `u8 pressed` | 9 |
| `0x0030` | RequestKeyframe | App to Host | empty | 0 |
| `0x0031` | DecoderStats | App to Host | `u16 pendingOutputs`, `u16 droppedDisplays` | 4 |
| `0x0032` | StreamConfigChanged | Host to App | `u16 width`, `u16 height`, `u8 orientation`, `u8 fps`, `u32 bitrateKbps` | 10 |
| `0x0033` | StreamPaused | Host to App | `u8 reason` | 1 |
| `0x0034` | Error | both | `u8 source`, `u16 code` | 3 |
| `0x0035` | FrameAck | App to Host | `u32 frameSeq` | 4 |
| `0x0037` | StreamPrefs | App to Host | `u16 maxLongSide`, `u16 maxShortSide`, `u8 maxFps`, `u32 bitrateKbps` | 9 |
| `0x0036` | ViewSize | App to Host | `u16 width`, `u16 height`, `u16 safeLeft`, `u16 safeTop`, `u16 safeRight`, `u16 safeBottom` | 12 |
| `0x0040` | Ping | both | `u64 timeUs` | 8 |
| `0x0041` | Pong | both | `u64 timeUs` (echo of the Ping) | 8 |
| `0x00F0` | Bye | both | `u8 reason` | 1 |
| `0x0100` | VideoFrame | Host to App (video) | `u32 frameSeq`, `u32 lastTouchSeq`, `u64 ptsUs`, `u8 flags`, then `u8[D]` H.264 Annex-B | 17 + D |

Field notes:

- `Hello.decoderCount` may be 0. `Hello.model` is the device model for logs.
- `Touch.count` is 1 to 10. `x`, `y` are normalized 0..1 over the video area; values outside 0..1 are clamped by the host. `pressure` is 0..1.
- `DecoderStats` is sent every 250 ms.
- `Sensor` values are in Godot's convention for Android (the same rotation for the display orientation and signs as Godot's own Android port): m/s^2 for accelerometer and gravity, rad/s for gyroscope, microtesla for magnetometer. The app sends them while the user has the sensor turned on, at about 60 Hz.
- `Key.keycode` is a Godot `Key` value (letters as uppercase ASCII, special keys in the `KEY_SPECIAL` range), `unicode` the typed character or 0, `pressed` 1 or 0.
- `FrameAck` is sent for every frame the decoder has output, and for frames the app drops before decoding (while it waits for an IDR after a decoder restart).
- `StreamConfigChanged`: the next `VideoFrame` is an IDR.
- `StreamPrefs` holds the user's video choices, 0 meaning automatic. The app sends it after `Welcome` and when the user changes a setting. The host applies it at once: stream size = min(Resolution Cap, `maxLongSide` x `maxShortSide`), frame rate `maxFps`, and `bitrateKbps` replaces the host's default; the stream restarts with `StreamConfigChanged` and an IDR.
- `ViewSize` is the size in pixels of the area the app shows the video in. The app sends it after `Welcome` and whenever the area changes (rotation, full screen). A host may fit the game to its aspect ratio; the Godot host resizes the hidden window of a play started from the phone and leaves other windows alone. 0 x 0 means no preference. The `safe*` fields are the unsafe edges of that area (display cutout, system bars) in its pixels, 0 when the area is fully usable; the Godot host scales them to the game window and exposes them as `Handview.get_safe_area()`.
- `VideoFrame.lastTouchSeq` is the seq of the newest `Touch` the game had processed in the update that produced this frame, 0 if none yet.

## 4. Enums

| Enum | Values |
|---|---|
| `host` | 0 Unity, 1 Godot |
| `codec` | 1 H264 (2 HEVC reserved, not valid in v1) |
| `profile` | H.264 `profile_idc`: 66 Baseline, 77 Main, 100 High |
| `level` | H.264 `level_idc`, e.g. 31 = 3.1, 41 = 4.1, 51 = 5.1. Any value 9..62 is accepted |
| `encoder` | 0 NVENC, 1 MF hardware, 2 MF software, 3 VideoToolbox (macOS) |
| `orientation` | 0 Portrait, 1 Landscape |
| `editorState` | 0 Stopped, 1 Playing, 2 Paused, 3 Compiling, 4 Reloading |
| `cmd` | 1 Play, 2 Pause, 3 Step, 4 Stop, 5 Restart |
| `Sensor.kind` | 1 Accelerometer, 2 Gyroscope, 3 Gravity, 4 Magnetometer |
| `result` | 0 Ok, 1 Ignored, 2 Busy |
| touch `phase` | 0 Began, 1 Moved, 2 Stationary, 3 Ended, 4 Canceled |
| `Reject.reason` | 0 BadVersion, 1 BadToken, 2 Busy |
| `StreamPaused.reason` | 0 NotPlaying, 1 Paused |
| `Error.source` | 0 Encoder, 1 Decoder |
| `Error.code` | 1 InitFailed, 2 ProcessFailed, 3 DeviceLost, 4 Unsupported |
| `Bye.reason` | 0 Normal, 1 ProtocolError, 2 Compiling, 3 Replaced, 4 Reloading |
| `VideoFrame.flags` | bit0 Keyframe, bit1 CodecConfig (SPS/PPS inline); other bits must be 0 |

`Hello.decoders[].codec` and `profile` use the same tables; an unknown profile value is a protocol error. In `Hello`, `codec` must be 1.

## 5. USB offer (host side)

The host listens for the whole editor session and offers itself to every plugged in phone; there is no session button (DECISIONS D18).

1. adb comes from the Android SDK path in Godot Editor Settings, else `PATH`. Every call uses `-s <serial>`.
2. On start the host listens on the first free PC port from 38500 to 38509 (`PORT`) and arms a one-time 32 byte token. Several editors on one PC get different ports.
3. `adb devices -l` every 3 s. `unauthorized`: the dock tells the user to accept the prompt on the phone. Each device in state `device` is prepared once until it is unplugged:
   1. Check the app is installed and current (`dumpsys package`). Missing or older: `adb -s <serial> install -r <addon>/android/handview.apk`. Install failure: show the full adb output. Signature mismatch: ask the user before uninstalling, never uninstall silently.
   2. `adb -s <serial> reverse tcp:PORT tcp:PORT`.
   3. Offer: `adb -s <serial> shell am broadcast -n io.github.h1dr0nn.handview/.SessionOfferReceiver -a io.github.h1dr0nn.handview.SESSION_OFFER -f 32 --es token <64 hex chars> --ei port <PORT> --es name_hex <project name, UTF-8 as hex>`. Flag 32 also reaches an app that was never opened.
4. When a phone uses the token, the host arms a new one and offers it to every prepared device, so a phone that leaves can join again.
5. The app keeps the newest offer per port, lists it under Devices, and connects on its own while it is in front (unless the user just left a session). `Reject{BadToken}` or a refused connection before `Welcome` drops that offer.
6. The app also accepts `am start -n io.github.h1dr0nn.handview/.MainActivity --es token <hex> --ei port <PORT>` (`singleTop`, read in `onCreate` and `onNewIntent`); test tools use it.
7. Editor exit: `adb -s <serial> reverse --remove tcp:PORT` for every prepared device. On start, remove Handview-range reverses whose PC port has no listener (an editor that exited without cleanup).

The offer is an explicit broadcast to the app's own receiver, so no other app receives the token, and the receiver requires `android.permission.DUMP`, which only the shell and the system hold, so no other app can plant an offer. Tokens expire after their first use.

### 5.1 Wi-Fi (host side)

With "Allow Wi-Fi" on in the editor (DECISIONS D22):

1. The listener binds every IPv4 interface instead of loopback only, on the same `PORT`.
2. Every 2 s the editor broadcasts a UDP beacon to `255.255.255.255:38510`: `{"handview":1,"name":"<project>","port":<PORT>,"hosts":["<LAN IPv4>",...]}`. It holds no token.
3. The dock shows a QR code with `handview://wifi?h=<LAN IPv4>&p=<PORT>&t=<64 hex token>&n=<project name, UTF-8 as hex>` and a six digit PIN. The app scans the code (Google code scanner) and joins with that token.
4. A phone that heard a beacon, or got an address typed by hand, joins with the PIN: `Hello.token` = SHA-256 of the UTF-8 string `handview-pin:<PIN>`. The PIN token is a second one-time token next to the regular one. After five wrong tokens the host drops it and the editor shows a new PIN; a new PIN and QR code also follow every connection.
5. Everything after `Hello` is the same as over USB.

## 6. Session

### 6.1 Handshake

1. App opens control, sends `Hello`.
2. Host checks `protoVersion == 1` (else `Reject{BadVersion}`), then the token: valid only if it equals the token of the latest launch and has not been used (else `Reject{BadToken}`). A valid `Hello` while a session is active replaces that session: the old control socket gets `Bye{Replaced}` and both old sockets close. `Reject{Busy}` is for hosts that cannot take a session right now (Unity while compiling or reloading); the Godot host never sends it. After `Reject` the host closes the socket.
3. Host sends `Welcome` with a fresh random `sessionId`. App opens video and sends `VideoAttach`. A wrong `sessionId` closes the video socket.

### 6.2 Codec and resolution

- H.264 Baseline or Main, Annex-B.
- Stream size = min(game size, phone screen, decoder `maxW` x `maxH`), keeping the game's aspect ratio (Resolution Cap). Both dimensions are rounded down to even numbers. Orientation is Landscape when width > height, else Portrait.
- Aspect change during a session: `StreamConfigChanged`, then an IDR.

### 6.3 Stream

- The first frame of a session, the first frame after `StreamConfigChanged`, and the first frame after an error are IDR frames with SPS/PPS inline, flagged `Keyframe | CodecConfig`.
- Not playing: no frames; the app shows "Not playing" and the Play button. Paused (if supported): no frames, the app keeps the last frame with a "Paused" label.

### 6.4 Play control

| Command | Valid in | Effect |
|---|---|---|
| Play | Stopped | Start the main scene |
| Pause | Playing, Paused | Toggle pause (only if supported) |
| Step | Paused | Advance one frame (only if supported) |
| Stop | Playing, Paused | Stop the game |
| Restart | Playing, Paused | Stop the game and start the main scene again, picking up code and scene changes. The host reports `EditorState{Reloading}` until the new run starts, then `EditorState{Playing}`; the app ignores the `StreamPaused{NotPlaying}` from the old run meanwhile |

Other states: `CommandResult{Ignored}`. Compiling or Reloading: `CommandResult{Busy}`. Unsupported command for this host: `CommandResult{Ignored}`.

Pause while Playing answers `CommandResult{Ok}` with state Paused, then `StreamPaused{Paused}` and `EditorState{Paused}`; Pause while Paused resumes (`EditorState{Playing}`). Step while Paused advances one frame and sends that frame. Host Godot supports both while the editor debugger is attached to the game; its Game workspace pause button is not synchronized with them.

Stop or game exit: `StreamPaused{NotPlaying}`, then `EditorState{Stopped}`; the session stays open. Game crash: `EditorState{Stopped}` once the editor sees the process gone.

### 6.5 Flow control

- In-flight = last sent `frameSeq` minus the newest acked `frameSeq`. The host captures and encodes a new frame only when in-flight <= 1; otherwise it skips the frame **before** encoding. `RequestKeyframe` resets the in-flight count, since the app is resyncing and has dropped what it had.
- An encoded frame is never dropped alone. If dropping is unavoidable, drop everything up to the next IDR.
- Decoder backpressure: `pendingOutputs > 1` continuously for more than 500 ms makes the host lower FPS, then bitrate, and recover step by step once stable (thresholds are config values). Severe lag on the app: flush the decoder, drop every frame until the next IDR, send `RequestKeyframe`.
- `RequestKeyframe` is ignored if an IDR went out less than the minimum interval ago (config, default 500 ms until measured).

### 6.6 Errors

- Encoder failure: re-initialize once and send an IDR; if that fails, `Error{Encoder, code}` and stop the stream.
- Decoder failure: the app resets MediaCodec, sends `Error{Decoder, code}` and `RequestKeyframe`.

### 6.7 Disconnect

The host releases every held touch, removes the `adb reverse`, and restores anything it changed. `Ping` every 1 s on control; 5 s without any message from the peer ends the session.

## 7. Editor to game link (PC only)

The game process connects to the editor over loopback (`127.0.0.1`, port and token from the environment variables `HANDVIEW_LINK_PORT`, `HANDVIEW_LINK_TOKEN`). Same framing as section 2. Link-only types live in `0x0200..0x02FF` and never reach the app.

| Type | Name | Direction | Payload | Size |
|---|---|---|---|---|
| `0x0200` | LinkHello | Game to Editor | `u8[32] linkToken`, `u32 processId` | 36 |
| `0x0201` | LinkStart | Editor to Game | `u16 maxW`, `u16 maxH`, `u8 maxFps`, `u32 bitrateKbps` | 9 |
| `0x0202` | LinkStop | Editor to Game | empty | 0 |

- `LinkStart`: a phone session is active; start (or restart) streaming with these caps, reset `frameSeq` to 1, first frame IDR. Sent on game connect if a session is active, and on each new session.
- `LinkStop`: no phone session; stop encoding and release all touches.
- Game to editor, forwarded to the app unchanged: `VideoFrame` (to video), `StreamConfigChanged`, `Error`.
- Editor to game, forwarded from the app unchanged: `Touch`, `FrameAck`, `DecoderStats`, `RequestKeyframe`.
- The editor forwards only complete messages. If the link drops mid-message, the partial message is discarded and the app gets `StreamPaused{NotPlaying}`.
