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
The eye buffers
are 1832 × 1920 each. The app requests
120 Hz; the connected headset has so far reported 72 Hz.

**This is not yet a standalone Steam Frame APK.** Android's ordinary app
syscall policy blocks Linux robust futex registration. SteamVR needs that
IPC contract. The debug launcher inherits ADB's syscall policy and passes
the owner-death test; it currently requires ADB to remain connected. SteamVR
initializes with the Quest driver and displays its native loading animation
in both eyes. The native Steam sign-in panel, separate left dock, and bottom
dock are now visible in the headset, and both Touch lasers respond to triggers.
Startup uses the OS's original boot movie and loading loop in a stereo OpenXR
layer, until the main panel and bottom dock are ready. Display buffers use
explicit Vulkan ownership transfers before Android reads them. Each image
carries the input eye poses sampled when Linux acquires it, and held images
keep those poses. These samples approximate SteamVR's final asynchronous
rendering pose; full motion stability remains under validation.

Dashboard readiness lasts for the VR session, including page changes that
temporarily replace browser textures. These changes do not replay startup.
Linux buffers are acknowledged even when OpenXR skips drawing or stops its
session temporarily. The HMD stays connected during visibility changes, so
removing the headset for phone sign-in does not simulate unplugging it.

The ADB launcher also publishes the app's real kernel socket rows into the
guest because Android denies application access to `/proc/net`. This helper
runs only with the developer launcher and excludes other apps' socket rows.

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
ffprobe), and libdbus-1 development headers. Create a
Python environment with `build_files/quest/requirements.txt`. Mesa's code
generators also need those Python modules visible to the Python interpreter
selected by Meson.

```sh
python3 -m venv --system-site-packages /path/to/quest-tools
/path/to/quest-tools/bin/pip install -r build_files/quest/requirements.txt
/path/to/quest-tools/bin/python build_files/quest/prepare-payload.py \
  --root /path/to/mounted-clean-rootfs \
  --work /path/to/linux-build-cache \
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

Wear the headset and wake both controllers. Keep the launcher running.
`-TimelineProbe` runs the cross-process GPU synchronization check instead of
Steam. To inspect logs in another terminal:

```sh
adb shell run-as net.stearlight.quest tail -n 30 files/logs/linux.log
adb logcat -s StearlightXR StearlightGPU StearlightQuest
```

When OpenXR reports Quest room-view support,
the driver enables SteamVR's native bottom-dock room-view action. A small
platform adapter forwards that action to Quest's camera layer while preserving
Valve's event handlers and UI.

The dashboard, docks, controller graphics, and lasers stay over the real room.
The Turnip adapter selects a separate system-layer composition pipeline for
room view, omits SteamVR's virtual sky, and preserves the layer's alpha. The
native construct floor is hidden during room view and restored afterward.
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
