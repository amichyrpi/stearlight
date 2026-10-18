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
probe use the actual OpenXR buffer size. The app requests 72 Hz when supported,
and otherwise keeps the runtime's active refresh rate. Scene rendering still falls below 72 FPS
under load; the display refresh rate is not the scene update rate.

Noto fonts are bundled for broad script coverage, including CJK and emoji.
Japanese and Korean choose the matching regional CJK faces. Fonts work
offline; their cache is rebuilt only when the bundled fonts change.

Launch **Stearlight OS** from the Quest app library after installing the APK.
It installs the bundled rootfs and starts Linux, SteamVR, and Steam itself.
The PC launcher and an ADB connection are optional developer tools. First
installation takes longer while the rootfs is extracted; later launches reuse
it and preserve Steam's account data.

The Linux guest identifies itself as `frame` in Steam settings, Remote Play
and desktop applications. Its PRoot adapter changes only the guest's uname
nodename; Android's hostname and the real kernel version stay unchanged.

The app's PRoot adapter tracks real guest robust-mutex lists and handles owner
death, including an owner killed while another process is waiting. It handles
Android's inherited syscall traps while retaining syscall acceleration. Shared
memory uses anonymous memfd files inside the session's IPC namespace. Browser
launchers avoid zygote and separate audio service processes to reduce Android's
child-process count. Network and GPU services remain separate processes for
Steam's browser ownership checks and shared GPU textures.

Steam's native sign-in panel, separate left dock, bottom dock, and Touch lasers
are supplied by Valve's runtime.
The Touch pair advertises SteamVR's Frame gamepad capability. Select gamepad
input on a game window. Local SDL2 games receive a virtual gamepad inside the
game process; the Wine adapter also supplies it to SDL2-based Wine input.
This local fallback uses the mapping below; streamed games use Steam Input's
configuration. A/B stay
on the right Touch controller, X/Y on the left; both sticks and triggers keep
their normal gamepad positions. Grips supply LB/RB. Grip + stick click supplies
View on the left and Menu on the right. Touch has no physical D-pad; streamed
games can map a stick to it in Steam Input. Original per-hand VR
input paths remain available. Losing focus or a controller releases its inputs.
The local adapter adds no background process and needs no kernel input-device
permission. SDL3 and applications that bypass SDL are not covered by this fallback.
Enable Hand Tracking in Horizon settings and set the Touch controllers down to
use hands in Steam's dashboard. Quest supplies articulated hand meshes and aim
poses; index-thumb pinch selects, and holding a pinch drags Steam's controls.
The non-dominant hand's menu gesture toggles the dashboard when Horizon supplies
that event. Horizon keeps its system gestures. Tracking or focus loss releases
pinch input, and a hand must open before clicking again. Picking up Touch
controllers restores their models, buttons, sticks and gamepad input. Hands do
not synthesize gamepad buttons or sticks; game-specific skeletal hand input is
not forwarded. No hand-tracking history is recorded.
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
The Quest compositor adapter converts the shipped spatial-video final-pass
shader's reconstructed RGB to linear light before its sRGB output. It matches
that shader exactly and leaves packed depth, motion and tracking data intact;
an unrecognized Valve shader is left unchanged.
PC-initiated sharing uses Steam's X11 capture backend and a 1280 x 720 mirror
of the rendered left eye, cropped to fill the landscape view. The mirror updates at
up to 30 FPS only while capture is active; OpenXR keeps its existing resolution
and refresh rate. Steam encodes the shared view with libx264. The Quest's
passthrough camera is excluded, so transparent areas appear black on the PC.
Steam's small capture windows use separate offscreen Vulkan images and cannot
take ownership of the headset display.
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
The driver supplies floor and seated origins from OpenXR's STAGE space,
including its reported play-area rectangle. SteamVR's recenter action can
update the seated origin; Horizon keeps its Guardian boundaries. Without this
calibration, seated games such as VTOL VR receive invalid headset poses.
VTOL VR has passed a local launch from Steam with the full standalone APK:
its room tracks in the world, both controllers appear, and both triggers select
menu buttons. Flight gameplay and sustained performance remain unverified.
After the performance changes, the user confirmed that looking around its
room/menu and moving both controllers felt smoother. Runtime platform aliases
use relative links so Steam updates can move the platform without leaving
PRoot hard-link aliases pointing at a removed version.
Local Linux game panels use 960x540 at the game's 60 Hz, matching the 75%
Quest profile and reducing shared-memory frame copies by 44% from 1280x720.
The headset's display remains at 72 Hz. `STEARLIGHT_QUEST_GAME_PROFILE=0`
restores the original panel size as well as Celeste's original renderer.
Mesa's compiled shader cache is enabled for repeat game launches. Its default
disk limit is 128 MB; local game launchers replace Steam's 5 GB override with
that limit (`STEARLIGHT_SHADER_CACHE_MAX_SIZE` can change it).
Steam's offline shader preprocessing is disabled by default on Quest because
it competes with the dashboard during game launch. Mesa still compiles and
caches shaders as needed; `STEARLIGHT_SHADER_PRECACHE=1` opts into Valve's
single-worker preprocessing path.
Celeste's recognized FNA3D library uses a bundled, SDL 2.0.14-compatible
FNA3D 21.06 build with free-region merging and empty-pool reclamation.
The Quest build also searches completed dynamic-buffer slots from the
beginning; the old forward-only search continually allocates buffers during
Celeste's idle menu even when earlier slots have finished rendering.
Its original library is saved; `STEARLIGHT_QUEST_GAME_PROFILE=0` restores it.
Unknown game updates keep their own renderer. A 9.6-minute idle title-screen
measurement kept total game GPU allocation constant at 1,072,533,504 bytes,
with eight dynamic slots and no game swap use. Gameplay and longer sessions
still need separate checks.
The Mesa build also releases its owned DMA-heap/ION allocation descriptors
after import, so freeing an exported GPU buffer can reclaim its memory.
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
OpenXR collects tracking into a local sample before briefly publishing it;
controller queries do not hold the Linux readers' sequence lock. Only active
controller bindings are queried. Tracking publishes each new sample once; unchanged button and
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

Steam's native client channel menu offers a Stable and Beta choice even when
the recovery client's account catalog is empty. These appear as Frame Hotfix
and Frame Hotfix Preview in Valve's combined channel controls. Stable selects
Valve's ARM64 `steamdeck_stable` feed; Beta selects the moving `publicbeta`
feed, rather than a recovery snapshot. Selecting one restarts Steam.
A channel change forces one native
update check, including when returning to an older Stable build. The choice
persists across launches; the client updater downloads and installs its own
packages. Startup and the update watcher restore the Quest browser and
streaming adapters after Valve replaces those files.

The OS channel separately chooses Valve's Stable or Beta ARM64 Frame image feed.
Steam's OS updater verifies the RAUC signature against Valve's pinned signing
certificate and verifies every downloaded chunk and the complete root image.
The Beta channel additionally trusts Valve's pinned development certificate,
shipped in its authenticated Frame image; Stable uses only the production key.
It extracts SteamVR into a staging directory, reapplies the Quest adapters,
and activates the runtime on the next launch. The previous runtime is restored
if the new dashboard fails to start. APK upgrades preserve an installed OS
runtime update. Empty image extents remain sparse. The required free space is
calculated from the selected image's data plus 2 GiB for staging and recovery;
interrupted downloads resume. Stable and Beta may publish the same OS image,
and the OS feed may lag SteamVR's separately distributed Beta releases.
The October 15 image (0.5.6 Beta, build `20261015.6141207`) has completed this
update path on Quest, and Steam's native client and OS update checks succeed.
The version page reports the confirmed installed Frame userspace runtime,
including its build ID. It does not report an available download as installed.
The updater does not flash Quest partitions or update Android firmware.

Local x86 Linux games use Valve's FEX tool and the installed Steam Linux
Runtime. The Quest launcher restores the runtime's content-addressed files and
aliases without using privileged pressure-vessel namespaces. Bundled x86 glibc
and C++ libraries meet FEX's GPU bridge requirements even for older soldier
games. Turnip renders game graphics; a dedicated Gamescope compositor presents
each local game as a SteamVR panel and selects it when its window first paints.
Single-application focus prevents helper windows from selecting an empty panel.
The pinned Gamescope adapter refreshes reused shared-memory buffers on each
commit, preventing old intro frames from flashing during local games.
Mesa's X11 shared-memory presentation connects
the emulated game to Gamescope's Xwayland display. FNA games default to Vulkan and SDL audio
uses PulseAudio. Explicit game overrides are preserved. Shader pre-caching
uses one replay worker to stay within Android's child-process limit.

Local Frame Android games are not supported yet. Steam launches VRChat's
ARM64 APK through Lepton. Its Android container needs private namespaces and
Binder services unavailable to the Quest app's unprivileged PRoot session.
The unchanged, signed VRChat APK was also tested directly on Quest 2:
its OpenXR loader rejects Horizon OS's APK-contained runtime library path
with `XR_ERROR_RUNTIME_UNAVAILABLE`, and Steam API initialization cannot find
Lepton's Steam client library. A newer loader reached OpenXR instance creation,
but repackaging the APK triggered VRChat's `Tampering Detected by App` shutdown.
That test is not shipped as a game-launch fix. Supporting the original Frame
edition requires a compatible Android container and Steam/OpenXR integration;
replacing the APK's loader alone is insufficient.

Local Windows VRChat is also not supported yet. Proton 11 ARM64 can reach
Unity initialization, and the full x86 Proton stack runs through FEX, but both
protected launches stop with `Failed to initialize Wine helper buffer`.
The x86 launcher requests buffers at `0x700100000000` and higher; Wine detects
this Quest's host address limit as `0x7fffff0000`. A direct allocation at the
requested address fails with `ENOMEM`. Installing Proton's official EAC runtime
and completing the game's EAC setup did not resolve the address-range mismatch.
The diagnostic launcher is not shipped. PCVR streaming remains the verified
VRChat path; local Linux games such as Celeste use the separate launcher above.

Steam's Wi-Fi page uses Android's actual network name, signal strength,
connection state, and cached scan results through a NetworkManager D-Bus adapter.
Scanning asks for Android's location permission when needed. Wi-Fi switching and
connection changes open Horizon's Wi-Fi settings; Android owns network credentials
and Steam follows its observed connection. The adapter shares the runtime
monitor's device-services thread and does not add a background Linux process.

Steam and Plasma receive the Quest's real battery percentage and charger state
through the same thread's UPower adapter. Android also supplies battery voltage,
temperature, and time to full charge when available. Changes are published over
D-Bus once per second; Android continues to manage charging and low-battery
shutdown. Unsupported battery-health and remaining-time estimates stay unknown.

Passthrough uses a real OpenXR compositor layer. Native room-view style,
monochrome hue, edge color and strength settings are forwarded to that layer.
Quest 2's cameras remain monochrome; its compositor's edge and tint effects
approximate the Frame controls without exposing raw camera images to Linux.
Wi-Fi, headset battery,
charging, and Bluetooth availability come from Android. The BlueZ bridge
exposes real Classic and connectable BLE discoveries and remembered bonds in
Steam's Bluetooth Settings. Pairing, cancellation, forgetting, and profile
connection requests go to Android; device operations wait for the actual
bond or connection state. Steam's nearby-login advertisement still uses the
same radio and reports success only after its callback. Device enumeration
has been verified through Steam's native API; physical pairing has not been
verified. Linux gamepad input forwarding is not implemented. Android owns Bluetooth profiles; no
GATT server or Linux Bluetooth audio stack is provided. Linux PulseAudio sends
48 kHz stereo PCM to Android's AudioTrack and receives the Quest microphone
through AudioRecord as a 48 kHz mono source. Allow microphone access in the
headset; denying it leaves speaker output available. Both streams stop when
the app loses OpenXR or Android audio focus. No microphone recordings are saved.
Speaker PCM is drained on its own audio-priority thread into a 60 ms queue;
after a stall, excess old frames are discarded instead of accumulating delay.
PulseAudio's output sink disables rewinds and caps idle bursts at 50 ms. The
Android track is primed before playback, starts with 40 ms of buffering, and
can grow to 80 ms after underruns (or the device minimum if larger). These
are individual buffer limits, not a measured end-to-end sync guarantee.
`adb logcat -s StearlightAudio` reports underruns, queue depth, dropped frames
and the longest receiver gap every 30 seconds while audio is active.
The native PCVR player uses PipeWire. Its speaker and microphone endpoints
tunnel through the same PulseAudio bridge; WirePlumber links the streams on
each launch without a host-side audio service.
The VR Store browser starts on the Store's real origin to avoid CEF rejecting
the first navigation from its empty data page. A guard for the verified CEF
binary handles a failed udev monitor without dereferencing it. Android denies
that monitor's netlink socket. A second guard skips empty shared texture
entries during SteamVR's browser cleanup. Both guards require an exact binary
checksum; unknown binaries remain unchanged. Touch input uses OpenXR.
The Steam adapters are reapplied to the
installed Steam files after updates, preserving Valve's matching UI chunks.
Touch battery levels are unknown and are not invented.

## Desktop and developer access

The bottom dock's **+ > Desktop** action opens Valve's KDE Plasma workspace in a
headset window. **Return to Gaming Mode** on the desktop closes it and restores
Steam. The workspace includes Dolphin, Konsole, System Settings, Discover, VLC,
KDE utilities, Valve's Vapor theme, and the Frame wallpaper catalog.
Chromium is bundled separately from pinned Debian ARM64 packages, with private
font and NSS libraries. It uses software rendering to avoid another GPU service.
PRoot cannot supply Chromium's Linux namespace sandbox; the surrounding Android
app sandbox still applies. Chromium retains separate renderer processes.
Headset browsing disables per-site renderer isolation to fit alongside SteamVR
within Android's unchanged process cap. Remote Desktop retains site isolation.
The browser uses the native address-bar popup and avoids the unavailable KDE
notification service; speculative renderers, the page back/forward cache, and
on-device AI model execution are disabled. Large pages and multiple simultaneous
apps remain resource limited.
Discover and `flatpak` use a signed, user-owned Flathub installation. ARM64 GUI
apps launch in a separate PRoot runtime, with their own configuration directories.
The first launch explains compatibility mode: installed apps share the desktop's
files and session because Horizon cannot provide Flatpak's Linux namespace
sandbox. Accepting it applies to subsequent compatibility-mode launches.
Apps requiring privileged services, a working portal, or unsupported GPU APIs
may still fail; this does not provide full Flatpak sandbox compatibility.

Konsole opens Bash in the active desktop session, so GUI commands use the correct
display. Git, curl, Nano, Python and SSH tools are included. Use
`flatpak install flathub APP_ID`, `flatpak run APP_ID`, `flatpak update`, and
`flatpak uninstall APP_ID` for desktop applications. The underlying OS is Alpine;
additional native terminal packages use `apk add --usermode --no-scripts PACKAGE`
after `apk update`. These commands change the persistent Linux files inside the
APK, not Horizon OS. Kernel drivers, systemd services and namespace-based
containers cannot be installed into the running Quest kernel this way.

Plasma includes a system tray and keeps the user's language and Konsole profile
choices. Appearance, wallpaper and file settings use KDE. **Headset Settings**,
**Wi-Fi Connections** and **Bluetooth Devices** in the application menu open the
real Horizon hardware settings; Android retains control of radios, charging,
sleep and device permissions. Tools requiring a full system service session
remain unavailable.
A nested KWin compositor keeps windows and menus inside
the headset panel. The desktop service owns its application processes directly
to leave room within Android's unchanged child-process budget.
Radio, network, and battery bridging share the runtime monitor, and headset KDE reuses the
app's private D-Bus. A code-verified KIO adapter releases idle file workers after
five seconds instead of retaining them for three minutes; active jobs are unchanged.

Loading can request a short OpenXR CPU boost, limited to 15 seconds per 75
seconds. Sustained rendering retains the existing performance levels; Horizon
controls clocks, thermal limits and whether a boost is granted. This does not
enable unsupported Quest 3 performance features on Quest 2 or raise Android's
child-process limit.

The experimental USB transport requests Horizon's consent-based NCM network
while the app is foreground and a PC cable is connected. Horizon must support
that API and the user must approve its system dialog. Linux receives the actual
IPv4 and IPv6 link-local interfaces. Sockets addressed to the USB subnet or
scoped IPv6 link are bound to that Android network; Wi-Fi remains the internet
connection. Socket bindings refresh after cable reconnection. If Horizon drops
an established USB network, the app renews its request after a short recovery
interval; unavailable requests retain their retry delay.
On Windows, run `post_process/quest/usb-network.ps1 -Adb /path/to/adb.exe`
from an Administrator PowerShell after opening Stearlight and approving the
USB-network prompt. It assigns a compatible IPv4 address only to the USB NCM
adapter, without a default gateway or DNS change. Use `-WhatIf` to preview it
or `-Undo` to remove the address and restore the adapter's original DHCP state.
Direct PC-to-Quest streaming has passed stereo video and audio playback, with
the host's USB cable indicator visible and both VR endpoints reporting the USB
addresses as their primary link. Wi-Fi remains available for internet traffic.
`usb-network.json` in the
private state directory records connection status and negotiated bandwidth.

Enable **Settings > System > Developer Mode**, then use **Set User Password**
in Developer settings to enable authenticated remote access while Stearlight
is open. Connect Windows Remote Desktop to `QUEST_IP:3389`, or use
`ssh -p 2222 stearlight@QUEST_IP`. Both use the `stearlight` account and your
desktop password; SSH also supports SFTP and `~/.ssh/authorized_keys`.
The RDP server uses TLS and a certificate generated on first setup.
Quest allows one active Plasma session. Return to Gaming Mode before connecting
Remote Desktop; its listeners pause while the headset desktop is open to leave
room for applications. SSH remains available. A Remote Desktop login releases
the headset renderer. Disconnecting/logging out
returns to Steam; XRDP allows up to a minute before ending a disconnected
session, then the headset runtime starts again.

USB ADB remains Horizon OS's normal ADB connection. To enter the bundled Linux
environment, use `post_process/quest/shell.ps1 -Adb /path/to/adb.exe` on Windows,
or `ADB=/path/to/adb sh post_process/quest/shell.sh` on Linux/macOS. An optional
command argument runs a single Linux command. These scripts locate the installed
APK's runtime automatically and require no paths from the build computer.

## Building the APK

GitHub Actions builds the ARM64 rootfs on `ubuntu-24.04-arm`, then builds the
Linux adapters, Mesa and Android APK on `ubuntu-24.04`. PRs and pushes to `main`
produce the `stearlight-quest` workflow artifact. Only successful default-branch
pushes publish to the `continuous` release. A manual run can also publish from
the default branch using `publish_continuous`.

The download contains `stearlight-quest.apk`, its SHA-256, dependency sources,
`BUILD.json`, this guide and the license. `SHA256SUMS` covers every download
file. CI verifies all rootfs chunks against the installation SHA-256, checks
the bundled runtime components and verifies the APK signature before upload.
The base rootfs and runtime overlay are separate assets, installed in that
order on first launch, so KDE and the GPU libraries are not duplicated.

To produce the initial build rootfs on Linux with Docker/Buildx, Python 3,
`btrfs-progs`, `zstd`, `pax-utils` and `patchelf`:

```sh
git submodule update --init --recursive
sudo bash build_files/quest/build-rootfs.sh build/rootfs-input
mkdir -p build/quest-root
tar --zstd -xf build/rootfs-input/frame-rootfs.tar.zst -C build/quest-root
```

The rootfs step mounts the checksum-pinned public Frame recovery read-only,
assembles the clean rootfs and stages its Plasma and Chromium files. It builds
no VM disk. ARM64 Docker execution is required; CI uses a native ARM runner.
Use `build/quest-root` as `--root` below; it already contains the staged desktop,
so `--frame-desktop-root` can be omitted. Build directories need substantial
free space; CI keeps the rootfs and APK stages on separate runners.

### Continuous signing

Configure the repository Actions secret `QUEST_SIGNING_KEYSTORE_BASE64` with
a base64-encoded PKCS12 keystore, alias `androiddebugkey`, and store/key password
`android`. The keystore itself is private; its password is the standard
development password. The workflow fails rather than publishing with a new,
incompatible key if the secret is missing. PR and nonpublishing builds use
temporary debug certificates and do not receive this secret.

For initial repository setup, generate a dedicated key outside the checkout
and store it in GitHub with the GitHub CLI:

```sh
mkdir -p "$HOME/.local/share/stearlight-signing"
keytool -genkeypair -storetype PKCS12 -keyalg RSA -keysize 2048 -validity 10000 \
  -alias androiddebugkey -storepass android -keypass android \
  -dname 'CN=Stearlight OS Continuous,O=Stearlight' \
  -keystore "$HOME/.local/share/stearlight-signing/quest-continuous.keystore"
chmod 600 "$HOME/.local/share/stearlight-signing/quest-continuous.keystore"
base64 -w0 "$HOME/.local/share/stearlight-signing/quest-continuous.keystore" | \
  gh secret set QUEST_SIGNING_KEYSTORE_BASE64 --repo OWNER/REPO
```

Keep a private backup of that key. Reuse it for all continuous APKs so Android
can install later builds as updates. Locally debug-signed APKs use a different
certificate unless built with `-PquestSigningStore=/private/path/to/the/key`.

### Payload and local Android build

Use a mounted **clean** Stearlight ARM64 image as the input. On Ubuntu 24.04,
install Python 3.12, the ARM64 GCC/G++ cross compilers, x86-64 GCC, CMake,
Ninja, pkg-config,
Bison, Flex, glslangValidator, zlib headers, dpkg-deb, patchelf, FFmpeg (including
ffprobe), and libdbus-1, libdrm, libpam, libX11, and libXext development headers. Create a
Python environment with `build_files/quest/requirements.txt`. Mesa's code
generators also need those Python modules visible to the Python interpreter
selected by Meson. Install the Linux Android NDK as well; it compiles the
patched PRoot against the pinned Termux headers and talloc library.

```sh
python3 -m venv --system-site-packages /path/to/quest-tools
/path/to/quest-tools/bin/pip install -r build_files/quest/requirements.txt
/path/to/quest-tools/bin/python build_files/quest/prepare-payload.py \
  --root /path/to/mounted-clean-rootfs \
  --frame-desktop-root /path/to/read-only-steam-frame-recovery-root \
  --work /path/to/linux-build-cache \
  --android-ndk /path/to/android-ndk \
  --output /path/to/new-quest-payload
```

Dependencies are pinned by URL and SHA-256. A nonempty output directory is
rejected to prevent old rootfs chunks from being included. Build output is
recorded in `quest-payload-build.log` inside the work directory. The script
also writes an adjacent dependency source archive; keep it with the APK.
`--rootfs-archive` can reuse a previously generated clean rootfs archive.
Plasma is extracted from the original ARM64 Frame OS mount, together with its
matching Qt/KDE libraries, themes and licenses. The staging manifest records
each file's checksum. If the clean rootfs already contains a staged
`/opt/stearlight-desktop` and `/opt/stearlight-browser`, `--frame-desktop-root`
can be omitted.

The Linux build host also needs X11/OpenGL development headers for Zink's
GLX loader. On Ubuntu 24.04:

```sh
sudo apt-get install libglvnd-dev libx11-dev libx11-xcb-dev libxext-dev \
  libxrandr-dev libxxf86vm-dev libxcb-glx0-dev libxcb-shm0-dev libxcb-dri2-0-dev \
  libxcb-randr0-dev libxcb-render0-dev libxcb-shape0-dev libxrender-dev \
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
native construct observes room-view state to hide its regular, distant and
theater floors without changing the user's saved floor preferences.
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
