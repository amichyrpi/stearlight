# Quest 2 runtime (experimental)

This target bundles the ARM64 Stearlight rootfs and runs its Linux programs
through PRoot. Turnip renders on the Quest's Adreno 650; shared DMA-BUF images
are submitted to the two OpenXR eye swapchains. Valve's runtime owns the UI.

Hardware checks have verified native Linux GPU commands, separate stereo
images, real headset and Touch poses, and shared Vulkan timeline semaphores
between Linux processes. Steam's browser also initializes ANGLE with the real
Turnip Vulkan renderer. SteamVR's separate OpenGL context uses the matching
Mesa 26.1.6 Zink loader over Turnip. A GPU probe verifies opaque-fd image
sharing by rendering in OpenGL and reading every pixel back through Vulkan.
The eye buffers are 1374 x 1440 each: 75% of the Quest 2's native dimensions,
reducing scene pixels by 43.75%. The Vulkan display modes and startup GPU
probe use the actual OpenXR buffer size. The app requests 120 Hz; the
connected headset reports 72 Hz. Scene rendering still falls below 72 FPS
under load; the display refresh rate is not the scene update rate.

Noto fonts are bundled for broad script coverage, including CJK and emoji.
Japanese and Korean choose the matching regional CJK faces. Fonts work
offline; their cache is rebuilt only when the bundled fonts change.

Launch **Stearlight OS** from the Quest app library after installing the APK.
It installs the bundled rootfs and starts Linux, SteamVR, and Steam itself.
The PC launcher and an ADB connection are optional developer tools. First
installation takes longer while the rootfs is extracted; later launches reuse
it and preserve Steam's account data.

The app's PRoot adapter tracks real guest robust-mutex lists and handles owner
death, including an owner killed while another process is waiting. It handles
Android's inherited syscall traps while retaining syscall acceleration. Shared
memory uses anonymous memfd files inside the session's IPC namespace. Browser
launchers avoid zygote and separate audio service processes to reduce Android's
child-process count. Network and GPU services remain separate processes for
Steam's browser ownership checks and shared GPU textures.

Steam's native sign-in panel, separate left dock, bottom dock, and Touch lasers
are supplied by Valve's runtime.
The streaming player uses the matching Mesa 26.1.6 EGL vendor with a
surfaceless display. EGL/Vulkan pixel sharing is checked on the Quest;
the older rootfs EGL driver produces black shared images. Its V4L2 decoder
interface is adapted to Android MediaCodec for
H.264 and HEVC; Android owns the codec hardware. The adapter is loaded only
by the streaming player. A private headless allocator supplies shared video
buffers to Steam's CPU texture-upload fallback; it does not replace system
libdrm or take over display output. Pixel and timestamp checks pass for both codecs at
1280 x 720. Non-VR game streaming has been verified on Quest 2 with both
video and audio. VRChat's menus/world have been confirmed in clear stereo
with a connected stream after reopening the full standalone APK. The
dashboard, both docks, lasers, move/resize controls and passthrough also
passed that reopen check. Other PCVR games remain unverified.
The separate `vrlink` PCVR player uses the same decoder through its stateful
V4L2 interface, including source-change events and epoll notifications.
The decoder accepts packed VR video up to 8192 pixels along either axis,
with the same 16-megapixel allocation limit used for flat streams.
The Vulkan adapter corrects overlapping chroma planes and stale Qualcomm
compressed modifiers in the VR player and compositor when the decoder
supplies linear NV12. Frame's facet renderer and final-pass system-layer
composition are enabled for spatially encoded PCVR video.
The Quest driver publishes its active OpenXR refresh rate in the display-rate
list required by the host's wireless HMD handshake.
CPU-painted move and resize controls use Valve's explicit atlas bounds on Quest;
embedded scanline coordinates otherwise display gray blocks or unrelated text.
The adapter checks the matching UI code and preserves unknown runtime versions.
The Quest 2 render models use Valve's `openxr_grip` component transform to
convert tracked grip poses to the model origin for both hands.
Startup uses the OS's original boot movie and loading loop in a stereo OpenXR
layer, until the main panel and bottom dock are ready. Display buffers use
explicit Vulkan ownership transfers before Android reads them. A memory
barrier makes earlier compositor pass writes visible before the next pass reads
color and depth attachments, preventing stale right-eye trails on Turnip.
Each image carries the input eye poses sampled when Linux acquires it, and held
images keep those poses. These samples approximate SteamVR's final asynchronous
rendering pose; full motion stability remains under validation.

Display events follow OpenXR's refresh timing instead of spinning SteamVR's
vblank thread. The Frame compositor also receives a VsyncEvent for each new
visible OpenXR tracking sample, avoiding its one-second display-IRQ timeout.
Tracking publishes each new sample once; unchanged button and
display properties are not resent. PRoot caches process lookups and passes
Adreno GPU ioctls, futex wakeups, and waits with an expected owner TID of
0, 1 or 2 directly to the kernel. Potentially guest-owned robust-mutex waits
retain their owner-death handling. OpenXR reprojects previously released eye
images between Linux updates, with the same image poses. GPU ownership
transfers and completion waits remain explicit.
A shared Android graphics context copies incoming frames and acknowledges
Linux independently of OpenXR's frame wait. Three history slots separate the
image being sampled by both eyes, the newest pending image, and an ongoing
copy. Copy completion precedes publication and acknowledgement. Unused pending
images can be replaced; the displayed image stays protected until both eye
draws have completed. Image copies, color conversion and stereo completion
waits retain the original rendering path.

`adb logcat -s StearlightGPU` reports `Scene updates`, the rate of new Linux
frames adopted for display, and `Linux handoffs`, the copy thread's input rate.
Handoffs can include pending frames replaced before display.
The headset's OpenXR submission rate also includes repeated images and should
not be used as the scene frame rate. Linux's compositor log separately reports
command recording, GPU completion, and display handoff times.

Dashboard readiness lasts for the VR session, including page changes that
temporarily replace browser textures. These changes do not replay startup.
Linux buffers are acknowledged even when OpenXR skips drawing or stops its
session temporarily. The HMD stays connected during visibility changes, so
removing the headset for phone sign-in does not simulate unplugging it.

The app publishes its actual loopback socket endpoints, inodes, and UIDs into
the guest because Android denies app access to `/proc/net`. Linux passes real
socket descriptors to an app-owned registry; kernel credentials and the
sender's descriptor are checked before publishing a row. No host socket-table
helper is required.
Steam's CEF owner query reads those socket inodes and the actual process
descriptors directly, avoiding repeated shell forks under PRoot. Other `lsof`
queries retain the original GNU implementation.

Steam's native **Settings > System > System Update Channel** selector offers
Stable and Beta. These select Valve's ARM64 Steam client feeds, respectively
`steamdeck_stable` and `publicbeta`. Quit Stearlight from the Quest menu and
reopen it after changing the channel. A channel change forces one native
update check, including when returning to an older Stable build. The choice
persists across launches; the client updater downloads and installs its own
packages. Startup and the update watcher restore the Quest browser and
streaming adapters after Valve replaces those files. The Stearlight OS/rootfs
itself is updated by installing a newer APK; SteamOS image updates do not apply.

Passthrough uses a real OpenXR compositor layer. Wi-Fi, headset battery,
charging, and Bluetooth availability come from Android. A BlueZ LE advertising
bridge passes Steam's original nearby-login advertisement to Android's actual
radio and reports success only after its callback. It does not implement
Bluetooth pairing, GATT servers, or Bluetooth audio. Linux PulseAudio sends
48 kHz stereo PCM to Android's AudioTrack and receives the Quest microphone
through AudioRecord as a 48 kHz mono source. Allow microphone access in the
headset; denying it leaves speaker output available. Both streams stop when
the app loses OpenXR or Android audio focus. No microphone recordings are saved.
The VR Store browser starts on the Store's real origin to avoid CEF rejecting
the first navigation from its empty data page. A guard for the verified CEF
binary handles a failed udev monitor without dereferencing it. Android denies
that monitor's netlink socket. A second guard skips empty shared texture
entries during SteamVR's browser cleanup. Both guards require an exact binary
checksum; unknown binaries remain unchanged. Touch input uses OpenXR.
The Steam adapters are reapplied to the
installed Steam files after updates, preserving Valve's matching UI chunks.
NetworkManager and UPower services are still missing. Touch battery levels
are unknown and are not invented.

## Build

Use a mounted **clean** Stearlight ARM64 image as the input. On Ubuntu 24.04,
install Python 3.12, the ARM64 GCC/G++ cross compilers, Ninja, pkg-config,
Bison, Flex, glslangValidator, zlib headers, dpkg-deb, patchelf, FFmpeg (including
ffprobe), and libdbus-1 and libdrm development headers. Create a
Python environment with `build_files/quest/requirements.txt`. Mesa's code
generators also need those Python modules visible to the Python interpreter
selected by Meson. Install the Linux Android NDK as well; it compiles the
patched PRoot against the pinned Termux headers and talloc library.

```sh
python3 -m venv --system-site-packages /path/to/quest-tools
/path/to/quest-tools/bin/pip install -r build_files/quest/requirements.txt
/path/to/quest-tools/bin/python build_files/quest/prepare-payload.py \
  --root /path/to/mounted-clean-rootfs \
  --work /path/to/linux-build-cache \
  --android-ndk /path/to/android-ndk \
  --output /path/to/new-quest-payload
```

Dependencies are pinned by URL and SHA-256. A nonempty output directory is
rejected to prevent old rootfs chunks from being included. Build output is
recorded in `quest-payload-build.log` inside the work directory. The script
also writes an adjacent dependency source archive; keep it with the APK.
`--rootfs-archive` can reuse a previously generated clean rootfs archive.

The Linux build host also needs X11/OpenGL development headers for Zink's
GLX loader. On Ubuntu 24.04:

```sh
sudo apt-get install libglvnd-dev libx11-dev libx11-xcb-dev libxext-dev \
  libxrandr-dev libxxf86vm-dev libxcb-glx0-dev libxcb-shm0-dev \
  libxcb-dri3-dev libxcb-present-dev libxcb-sync-dev libxcb-xfixes0-dev \
  libxshmfence-dev
```

The cross build uses these headers with the clean ARM64 rootfs's link
libraries. Host libraries are excluded from the payload.

To inspect the source display image, create `files/state/gpu-capture.request`
with `adb shell run-as net.stearlight.quest touch`. The next Linux frame is
saved privately as `files/state/gpu-capture.rgba`: three little-endian uint32
values (width, height, Vulkan format), then RGBA8 rows in OpenGL bottom-up order.
This one-shot capture stalls rendering while it reads and writes the pixels.

Install JDK 17, Gradle 8.9, Android SDK 35, NDK 27.3.13750724, and CMake
3.22.1. Set `JAVA_HOME` and `ANDROID_HOME` for your installation, then build:

```powershell
./post_process/quest/build-apk.ps1 -Payload C:/path/to/quest-payload `
  -Output C:/path/to/apk-build -Gradle C:/path/to/gradle/bin/gradle.bat
```

The APK, its SHA-256, and the developer launcher with its socket helper are
under `outputs/apk/debug/`. The launcher can be run from that folder without
a repository checkout. This build must include
all rootfs parts; small development APKs that reuse an already installed
rootfs are not fresh-install artifacts.

## Developer hardware checks

```powershell
adb install -r C:/path/to/apk-build/outputs/apk/debug/app-debug.apk
./post_process/quest/run-linux-debug.ps1 -Adb adb
```

For a normal launch, open Stearlight OS in the Quest app library. Wear the
headset and wake both controllers. The debug launcher above is an optional
alternative that inherits ADB's kernel syscall policy; keep it running only
when using that diagnostic path.
`-TimelineProbe` runs the cross-process GPU synchronization check instead of
Steam. To inspect logs in another terminal:

```sh
adb shell run-as net.stearlight.quest tail -n 30 files/logs/linux.log
adb logcat -s StearlightXR StearlightGPU StearlightQuest
```

For a one-shot graphics capture while the dashboard is visible:

```sh
adb shell run-as net.stearlight.quest touch files/state/xr-eye-capture.request
```

The app writes `gpu-capture.rgba` and `xr-eye-capture.rgba` in that
directory for the same fresh Linux frame. The first contains the Linux stereo
image; the second contains the right eye after Android color conversion and
before OpenXR composition. Check the matching frame IDs in `StearlightGPU`
logcat. Captures have three little-endian 32-bit header words (width, height,
pixel format) followed by four bytes per pixel. Passthrough camera pixels are
not included. Capturing reads back GPU pixels and briefly stalls rendering.

When OpenXR reports Quest room-view support,
the driver enables SteamVR's native bottom-dock room-view action. A small
platform adapter forwards that action to Quest's camera layer while preserving
Valve's event handlers and UI.

The dashboard, docks, controller graphics, and lasers stay over the real room.
The Turnip adapter recognizes Frame's final-pass system-layer composition for
room view, omits SteamVR's virtual sky, and preserves the layer's alpha. The
native construct floor is hidden during room view and restored afterward.
The shipped star mesh is excluded using its exact shader, depth state and draw
range; controller, laser and panel draws are preserved.
Normal VR uses the original SteamVR pipelines.

For a developer toggle check, use:

```sh
adb shell run-as net.stearlight.quest touch files/state/passthrough.enabled
adb shell run-as net.stearlight.quest cat files/state/passthrough.status
adb shell run-as net.stearlight.quest rm files/state/passthrough.enabled
```

The last command restores VR. `passthrough.supported` is created only after
OpenXR confirms support and creates the camera layer. The camera also waits
for the compatible system-layer pipeline and dashboard textures. Boot and
loading animations always use an opaque black background. The camera pauses
when loading or when the OpenXR session stops. Camera images stay in Quest's
compositor and are not exposed to Linux.

Bluetooth sign-in and sustained 120 Hz are not verified.
