# android-app

Handview Android client. Kotlin + Jetpack Compose.

- `app/`: the app (`io.github.h1dr0nn.handview`).
- `client/`: pure JVM session transport, latency tracking, Annex-B parsing and touch mapping, tested with a loopback fake host.
- `protocol/`: pure Kotlin wire format codec, unit-tested on the JVM. Mirrors `native/core/include/handview/protocol`.

## Build

```bash
./gradlew :client:test :app:assembleDebug :app:lintDebug
```

Point Gradle at your SDK with `ANDROID_HOME` or a `local.properties` file containing `sdk.dir=...` (not committed).

## minSdk 26 (Android 8.0)

- The hard floor is API 23: touch-to-display latency takes `t1` from `MediaCodec.OnFrameRenderedListener`, added in API 23.
- `KEY_LOW_LATENCY` (API 30+) is only a decoder hint, and `MotionEvent.getEventTimeNanos()` (API 34+) has a fallback to `getEventTime()`. Neither needs a higher minimum.
- 26 instead of 23 drops the oldest devices, where hardware H.264 decoders are the least predictable, and lets the app use `java.time` and adaptive icons without desugaring. Android 8.0+ still covers nearly all devices in use.
- targetSdk / compileSdk: 35. API 34+ devices get nanosecond touch timestamps.

The minimum SDK is final for the MVP (decision D3).

Launch with a 64-character hex `token` string and an integer `port` extra. A new token replaces the active session with `Bye{Normal}`. Rotation from the stream configuration preserves the activity and session. Disconnect returns to the USB waiting screen.

The app honors the stream's SPS color description (BT.709 limited range, decision D10). Decoder stats report dropped displays since the previous 250 ms report. Busy command results disable playback controls until a subsequent editor state or command result clears Busy. Latency percentiles use nearest-rank samples over ten seconds, capped at 500.

Device verification still needs a USB session to confirm vendor low-latency hints, decoded color, rotation, multi-touch and render timestamps on real hardware.
