# protocol

Shared definition of the Handview wire protocol, used by the native core (C++) and the Android app (Kotlin).

## Wire Format v1 (baseline)

- Two TCP connections to the same port, both with `TCP_NODELAY`: control first, then video after `Welcome`.
- Little-endian. Every message starts with a 6 byte header: `[type: u16][length: u32]`, where `length` is the payload size in bytes.
- Payload limits: 64 KiB on the control socket, 32 MiB on the video socket. Larger means `Bye{ProtocolError}`.
- Fields are parsed one by one. They are not aligned, so code must never cast a buffer pointer to a struct.
- Unknown type: skip `length` bytes and log once. Bad enum value or wrong payload size: `Bye{ProtocolError}`.
- Timestamps on the wire are in microseconds.
- Sequence numbers are `u32`, start at 1 each session, and compare as serial numbers: `a` is newer than or equal to `b` when `(int32)(a - b) >= 0`.

## Status

M0 covers header framing, little-endian field access and seq comparison, implemented in:

- C++: `native/core/include/handview/protocol/wire.hpp`, tests in `native/core/tests/`
- Kotlin: `android-app/protocol/`, tests in `android-app/protocol/src/test/`

Both test suites use the same byte dumps. The full message table, enums and shared test vector files (`vectors/`) land in M1. Any wire format change must update this folder and the vectors together.
