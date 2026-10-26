# Shared-image performance checks - 2026-10-26

The abandoned eye-read-fence experiment is removed. The GPU bridge now samples
the three imported display buffers directly. Protocol version 4 tracks accepted
frames separately from allocation releases. A displayed allocation stays leased
until both eye reads have completed; a newer unread frame can replace an older
pending one. Reconnects preserve the displayed allocation's lease. Android and
the Linux WSI must be updated together. The existing eye completion and Vulkan
foreign-ownership barriers remain; decoder timing and stream quality are intact.

On the Quest 2 dashboard, Android reported graphics allocation decreasing from
181016 KiB to 88236 KiB. The user confirmed correct stereo graphics, docks,
lasers and audio, a more native feel with direct buffers, and a small further
navigation improvement with the revised handoff and two Steam raster workers.
Steam's Vulkan compositing and GPU rasterization were confirmed through its
browser diagnostics. Android's immersive CPU group still limits the app to
cores 4-6; removing the launcher's fixed mask does not bypass that policy.

PRoot also passes nonblocking futex REQUEUE, CMP_REQUEUE and WAKE_OP directly to
the kernel. Robust waits and all inherited Android restrictions remain intact.
The repository checks and shared-buffer ownership stress tests pass, including
reconnection, stale releases, latest-frame selection and concurrent stereo reads.
Celeste's picture, music and gamepad menu controls pass. USB streaming stereo,
audio and Touch controls pass. The signed full APK was installed and launched
through QuestActivity; a fresh capture confirms the signed-in library and both
docks. Streaming motion comparison remains inconclusive: the user reported a
warm headset and reduced smoothness. Battery temperature was 48.7 C, while
Android reported thermal status 0. The app was closed after verification to
allow cooling. No sustained frame-rate or motion-to-photon claim is made.

# USB streaming checks - 2026-10-20

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
