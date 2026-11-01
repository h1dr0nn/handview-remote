# android-app

Handview Android client. Kotlin + Jetpack Compose.

- `app/`: the app (`io.github.h1dr0nn.handview`).
- `protocol/`: pure Kotlin wire format codec, unit-tested on the JVM. Mirrors `native/core/include/handview/protocol`.

## Build

```bash
./gradlew :protocol:test :app:assembleDebug
```

Point Gradle at your SDK with `ANDROID_HOME` or a `local.properties` file containing `sdk.dir=...` (not committed).

## minSdk 26 (Android 8.0)

- The hard floor is API 23: touch-to-display latency takes `t1` from `MediaCodec.OnFrameRenderedListener`, added in API 23.
- `KEY_LOW_LATENCY` (API 30+) is only a decoder hint, and `MotionEvent.getEventTimeNanos()` (API 34+) has a fallback to `getEventTime()`. Neither needs a higher minimum.
- 26 instead of 23 drops the oldest devices, where hardware H.264 decoders are the least predictable, and lets the app use `java.time` and adaptive icons without desugaring. Android 8.0+ still covers nearly all devices in use.
- targetSdk / compileSdk: 35. API 34+ devices get nanosecond touch timestamps.

This is a proposal; lowering to 23 only needs the `minSdk` line in `app/build.gradle.kts` changed.
