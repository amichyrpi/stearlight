# Quest streaming compositor checks - 2026-11-25

The Quest compositor uses Mesa's automatic submit-thread selection. Startup
refreshes the launcher after SteamOS activation and applies the tested SPIR-V
edits only to shaders with the matching original hashes. The Quest 2 headset
check passed in the virtual room and with passthrough enabled.

The test PC's SteamVR Video setting was 34.2%, which reported 1228×1288 per
eye on that PC and matched the Quest's local SteamVR render target. This
percentage is specific to that host's recommended resolution.

# Streaming compatibility and latency checks - 2026-11-16

The native VR player now advertises the Frame protocol through its HMD model
query. The actual Quest driver, projections, refresh rate, serial and Touch
controller identities remain unchanged. Both endpoints negotiate
`SteamVRClientUnified`; the headset capture and user check confirm linked PC
windows in the local dock. A host SteamVR session created with the former
identity can retain that identity until SteamVR is restarted.

VR decoding presents the newest completed capture instead of retaining an
older presentation backlog. Encoded reference packets, timestamps and buffers
already dequeued by the renderer are preserved. Flat streaming keeps FIFO.
The Android 1 ms output-wait experiment reduced decoder request time, but the
user reported unchanged or worse motion. It was rejected; the original 5 ms
hardware wait is retained.

The exact SE2 color-shader match now also recognizes the SteamOS 0.5.7 runtime.
The inverse sRGB conversion preserves the original depth, motion and alpha
instructions. The corrected real shader passes SPIR-V validation. Unknown
shader binaries are left unchanged.

Ownership/queue tests cover both decoder transports, and the SDK forwarding
test covers Quest 2/3/3S identification, unchanged native objects, display
geometry, controller/serial properties and short output buffers. Decoder
backpressure and source-change tests pass. Bitrate stays adaptive.

The user reports a substantial improvement and confirms the linked dock, but
still considers movement unusable. The user confirmed passthrough required
the streamed-scene hiding fix, so that exact fix is retained. Compression quality and usable
motion latency are not yet verified. No sustained 72 FPS or motion-to-photon
latency claim is made.

The host-only 8-bit encoder comparison produced good colors and passthrough,
but the user reported worse, still unusable motion. It is not a latency fix
or an automatically negotiated APK capability.

Controller prediction now receives OpenXR grip/aim linear and angular
velocities through an optional 224-byte shared mapping. The driver selects
the same grip/aim origin as its calibrated model pose and applies the angular
velocity cross-offset correction. Missing, stale, hand-mode and disconnected
samples produce zero velocity. Pose/velocity tests and both native builds pass;
the signed full APK is installed. Its native library, Linux driver and retained
passthrough adapter match the packaged binaries. Real OpenXR motion records and
nonzero controller velocities are visible through both local and host OpenVR.
The user reported unchanged or worse motion after this change. Local streaming
composition is missing frame deadlines; controller prediction does not resolve
the head/video delay.

An isolated 1216 x 4864 HEVC decoder check compared the current NV12 output with
Qualcomm's native linear output. Both preserved red pixels and timestamps; the
native-format test did not improve decoding, so it was not applied to the app.
Operating-rate hints likewise showed no repeatable benefit in this small check.

The VR streaming allocator now prefers CPU-coherent DMA-BUFs exported through the
active Vulkan driver, allowing the player to import NV12 video instead of
uploading ordinary shared-memory buffers. Unsupported allocators keep the old
memfd fallback; `QUEST_VIDEO_DMABUF=0` forces that path. Flat streaming retains
its existing allocator. The existing decoder
cache synchronization, queue ownership and passthrough filtering remain intact.

A standalone Quest 2 probe exported a full-size 1216 x 4864 allocation, imported
it as linear NV12, and verified both planes through a GPU readback. The exported
FD remained valid after its producer objects were destroyed. The maintained
allocator builds, and mocked lifecycle/failure tests pass. The headset then
disconnected after reporting 6% battery and a hot battery. The integrated
allocator and its streaming latency still require testing after recharge.

After recharge, inspection of the live 2.19.1 VR player showed that it already
uses DMA-BUFs and does not load this synthetic DRM allocator. The allocator
change therefore cannot remove the current player's video delay. Its standalone
GPU import result must not be treated as an integrated streaming improvement.
The eye-camera trace showed advancing, valid render cameras during head turns,
while local composition missed frame deadlines. Native decoder output and 75%
internal composition did not improve the user's delay; the latter reduced
sharpness. Moving eye completion before `xrEndFrame` also did not improve delay,
and the user reported a missing computer view and passthrough return failure.
All three experiments were rejected. The full baseline APK was restored and its
native/decoder/passthrough binaries checked; the user confirmed both features
worked again.

Standalone capability probes accepted HIGH-priority Vulkan queues and an EGL
HIGH-priority context on the Quest 2. A temporary GPU scheduling test preserves
native resolution, the original decoder and original eye-completion order.
It remains unverified; no usable streaming latency or frame-rate claim is made.

The user confirmed that video inside the streamed PC-desktop window stays smooth
while the streamed 3D scene lags during head turns. The decoder preserves the
original frame timestamps, and inspection of the player confirmed that it reads
those timestamps as frame identifiers. SteamVR's SE2 reprojection vertex shader
is active. Bounded camera captures show transient differences between that
pass's output camera and the local geometry camera used for presentation.
An isolated camera-association test observes both output-eye cameras from the
streamed pass without changing shaders, bitrate, decoder waits or image leases.
Its mock checks cover matrix layout, dynamic uniform offsets, queued images and
consumed camera metadata. The user reported substantially better 3D response,
but the local dashboard was unstable. A temporary follow-up aligned every local
geometry uniform with its eye's streamed output camera before submission,
touching only validated, mapped, host-coherent camera matrices. Model and
projection matrices remained intact. Its tests included multiple panel buffers
and noncoherent/unmapped/freed storage guards, but the user still reported
jitter. Both panel-retargeting versions were rejected. The next temporary test
kept the dashboard cameras unchanged and aligned only the streamed 3D pass's
output-camera matrices to them before submission. Its transform/inverse, stereo
and storage-guard checks passed, but the user still rejected motion quality.
All uniform-retargeting experiments were removed. Bounded render-pass tracing
on the original rendering path is now investigating whether scene and system
layers can retain independent presentation cameras. No streaming comfort fix
is claimed.
The GPU priority experiment and diagnostic libraries were removed from the
running test; the full baseline APK and original decoder were restored first.

The PC-window selection log identified Valve's explicit automatic room-view
disable rule for remote frames. An exemption for Quest native passthrough was
tested, but the user reported unwanted window switching and requested its
removal. The exemption and its tests were reverted, and the original rule and
startup adapter restored on the headset. Normal passthrough is retained.

The resumed automated render-graph capture confirmed RenderPass2, not dynamic
rendering, for the current Frame compositor. The scene-distortion and local
geometry draws share the final stereo target. Initial dashboard passes allocate
2-sample color/depth targets; active streaming also uses single-sample targets.
A temporary `renderSystemLayerInDistortPassMSAA=1` configuration test did not
establish a motion improvement and was removed. The original color/passthrough
WSI was restored after tracing. These captures do not prove that MSAA causes the
reported lag, nor establish a motion-comfort fix. Next profiling needs a matched
actual head-motion interval to distinguish GPU stalls from mixed camera poses.

The subsequent camera tests did not resolve the motion issue. Following uniform
buffer uploads on the CPU, then reading camera constants after the existing GPU
completion fence, both failed the user's head-motion comparison. Hiding local
geometry, panels and controllers also left the streamed world lagging/jittering.
This rules out local UI mixing as the sole cause; it does not justify a separate
OpenXR layer implementation as a proven fix. All three experiments remain local
diagnostics, and the original renderer and normal UI were restored.

The active runtime loads `distort_vs_achromatic_se2_reproject.spv`. Its source-pose
ring and selected video-frame ID are the next diagnostic target. An attempted
bounded capture did not establish a new stream and produced no metadata, so no
frame/pose mismatch is claimed. The headset reached 53 C; Stearlight was stopped
and the baseline WSI restored while the user cools/recharges it. The prepared
capture now checks temperature and restores the renderer on exit. Offline tests
cover completion-time snapshot values, stereo cameras, in-flight snapshot
ownership, bounds, and paired source-ring/target-camera descriptor capture.
These tests validate the instrumentation, not streaming latency or comfort.

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
