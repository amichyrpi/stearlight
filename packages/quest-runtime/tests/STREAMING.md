# USB streaming checks - 2026-10-20

Presentation correction prepared on 2026-10-25: the native eye renderer now
checks completion of its own texture reads with a GL fence, retains the held
image while reads are incomplete, and selects the newest completed image after
waiting for both OpenXR eye buffers. It no longer calls `glFinish` after every
`xrEndFrame`. The copy worker's completion and Linux's foreign-ownership
barriers remain intact. Decoder timing and stream quality are unchanged.

The Android library builds, and history ownership and video-queue tests pass,
including the history test with AddressSanitizer and UndefinedBehaviorSanitizer.
Headset comparison is pending: the Quest reached 2% battery and reported
overheating, and the user is recharging it. Do not treat this candidate as a
verified latency improvement. Compare comfortable head movement, frame freezes,
black borders, both-eye texture correctness, audio and Touch input after charge.
The logged local image pose age excludes the remote encoder/decoder latency.

Quest 2, direct PC-to-Quest USB connection, existing signed APK and account data
preserved. The first ordered-buffer/copy change produced no noticeable latency
improvement. After installing the bounded capture queue and removing Frame's
additional 6 ms submission delay, the user reported better delay/stutter with
correct picture and sound. This is a subjective comparison, not a measured
motion-to-photon latency or a sustained 72 FPS result.

The source-check suite passes. The real Linux decoder adapter also passes
deterministic tests for completion order, overloaded queues, consumer-owned
buffer protection, timestamps, offsets and pixel bounds through both legacy
socket and shared-memory transports.

A temporary Android executable built from the current `video_bridge.cpp` was
run under Stearlight's UID on the Quest. Qualcomm's H.264 and HEVC decoders each
returned the expected red NV12 pixels and timestamp through both transports.
The shared-memory cases verified unchanged guard bytes and no raw frame payload
on the socket. These were 128 x 128 codec/transport checks, not full-size PCVR
performance measurements.

The new native library and ARM64 Linux driver compile. Protocol and skeleton
tests preserve measured joint positions for both hands and rotated pointer
roots. Actual streamed skeleton forwarding, finger orientation, and controller
switching remain unverified. The candidate requests native 1832 x 1920 eye
buffers; its sharpness and performance tradeoff also remains unverified.

Installing that candidate requires an APK signed with the installed app's
identity. The local key was lost, and a matching GitHub signing secret remains.
The user requested keeping this work local, so no CI build or release was
started, and the candidate native library has not been installed in the app.
