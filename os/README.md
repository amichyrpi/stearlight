# Stearlight OS — Alpine Raspberry Pi image

This target builds a flashable aarch64 Alpine image for Raspberry Pi 4. It is
an appliance image, not an in-place conversion of Raspberry Pi OS.

## What the current image contains

- Alpine edge with OpenRC and the Raspberry Pi downstream `linux-rpi` kernel.
- Native aarch64 Mesa/V3DV, Gamescope, PipeWire/WirePlumber, and Monado/OpenXR.
- A Debian glibc runtime isolated with Bubblewrap for the Steam ARM64 beta.
- Valve's native ARM64 Steam client launched with the standard SteamOS Gamepad UI
  contract and rendered by the standalone Stearlight stereo shell. The OS image does not
  build or start the legacy custom Steam Link receiver; Valve's client owns
  discovery, pairing, authorization, transport, and its Steam Frame UI. On a
  custom Pi headset, device tracking and controller support still require the
  matching hardware integration described below.
  Direct Gamescope remains an explicit `STEARLIGHT_SESSION_MODE=gamescope`
  diagnostic override.
  The VM build runs Valve's bootstrap once and includes the extracted client
  payload, so VM boots do not repeat the 500 MB client download.
- A 2880x1600 side-by-side scanout at 60 Hz (1440x1600 per eye), pure black world, and
  one smaller curved floating Steam surface.
- `assets/boot.mkv` as the first userspace splash, followed by the looping
  `assets/loop.mkv` transition until Steam's first Gamepad UI frame appears.
  `steam_loading.mkv` remains available for Steam/connection transitions.
- SteamOS compatibility helpers (`timedatectl`, `localectl`, timezone,
  developer-mode, update/branch and firmware probes) are included so the
  native Gamepad UI can complete its first-run setup without a desktop
  session.
- Silent firmware/kernel configuration, no desktop and no console on the HMD.
- No local login prompt: the supervised Stearlight UI takes tty1 immediately.
- No password-based SSH service or bundled host credentials. The image has no
  remote login path enabled by default.

## Build

Use a Linux host or WSL2 with Docker Desktop and BuildKit. Expect the first
build to download several gigabytes because Steam and its glibc runtime are
baked into the image to avoid doing that work during the first headset boot.

```sh
bash ./os/build.sh
```

On Windows, the local wrapper invokes the WSL2 builder without importing host
credentials:

```powershell
.\os\build-local.ps1
```

Set `STEARLIGHT_BAKE_STEAM=0` for a small developer rootfs without Steam. The
result is `out/stearlight-os/image/stearlight-os-YYYYMMDDrp4.img.gz` with a
SHA-256 file.
The builder never writes to a disk. On Windows, the guarded flash helper checks
the USB disk serial number and exact size before elevating Raspberry Pi Imager:

```powershell
.\os\flash-windows.ps1 `
  -ImagePath .\out\stearlight-os\image\stearlight-os-YYYYMMDDrp4.img.gz `
  -DiskNumber 4 -ExpectedSerial 'SERIAL' -ExpectedSize 15836643328
```

The helper stages a raw image because Raspberry Pi Imager 2.0.11 can stall on
gzip files whose uncompressed size exceeds 4 GiB. It keeps Imager verification
enabled and deletes the raw staging image afterward.

## FEX and Proton 11 ARM64

The builder supports Valve's Steam-delivered compatibility tools:

- FEX app `3127680`, default branch `beta`.
- Proton 11.0 ARM64 app `4628740`, default branch `public`.

These ownersonly depots reject anonymous SteamCMD downloads. To bake them into
a local or CI image, set `STEAM_USERNAME` and `STEAM_PASSWORD` for an account
with the free Steam Frame compatibility package before running the builder.
Override `STEARLIGHT_FEX_BRANCH` or `STEARLIGHT_PROTON_BRANCH` when testing a
different Valve branch. Never commit those credentials. GitHub Actions reads
them from the optional `STEARLIGHT_STEAM_USERNAME` and
`STEARLIGHT_STEAM_PASSWORD` repository secrets.

## First boot welcome

After `boot.mkv`, the standalone shell shows `loop.mkv` while it waits for
the first real Steam Gamepad UI frame, then displays the real Steam first-run
experience from the Steam Gamepad UI client. The small
  `steam-firstboot` helper prepares the per-user bootstrap tree and repairs its
  ABI-specific Mesa seed; it never draws a replacement welcome page or writes
  account credentials. The SteamOS helper commands satisfy the timezone,
  update, branch and firmware probes used by Valve's setup. Steam owns the
  complete welcome pipeline shown
by SteamOS: language, timezone, network, update, account sign-in, and the
initial tour. The VM image pre-extracts this payload at build time, while a Pi
image uses the already-installed ARM64 client and performs only the quick
local preparation at boot. No Wi-Fi profile or password is copied from the
build host; `iwd` and `dhcpcd` remain enabled so Steam can configure the
connection interactively.

The Steam Frame connection control is opened inside Valve's own client with
the registered `steamlink://lookup/` discovery URI. The native session starts
the installed Valve client with its `-steamframe` mode, and Valve owns discovery, pairing, authorization,
`streaming_client` and VRLink transport. The shell forwards mouse and keyboard
events to the client's private X11 surface and mirrors that surface into both
eyes; it does not implement a second pairing or streaming protocol. It does not
invent pose or controller data for custom hardware. `STEARLIGHT_STEAM_FRAME=0`
is an explicit diagnostic escape hatch for the old ten-foot X11 mode;
`SVRT_STEAM_FRAME` and `SVRT_START_IN_STREAMING_MODE` belong only to the
separately built legacy receiver.

For VM bring-up and boards with a normal SDL-mapped gamepad, the shell also
provides a mouse-like fallback: the left stick and D-pad move the blue laser,
A selects, B sends Steam's back key, and the right stick scrolls. This fallback
only drives the local Steam UI; it does not replace Valve's VRLink pose or
controller transport when a real Steam Frame-compatible headset is connected.

## Hide the Pi 4 EEPROM diagnostic screen

`DISABLE_HDMI` is stored in the board EEPROM, not in an operating-system
image. Before replacing Raspberry Pi OS, run this once on the target Pi:

```sh
sudo bash ./os/apply-quiet-eeprom.sh
sudo reboot
```

The script preserves all unrelated EEPROM keys and schedules only
`DISABLE_HDMI=1` and `BOOT_UART=0`. The image itself also sets
`disable_splash=1`, `quiet`, `loglevel=0`, `logo.nologo`, and removes the
visible framebuffer console. Local password gettys are disabled, so the screen
stays black until the startup movie can be shown
after DRM is ready.

## XR boundary

Monado is installed as the native OpenXR runtime, but it is not started by
default yet. The current Pi prototype has no specified IMU/camera tracking
driver, calibration, lens distortion profile, or display timing interface.
Those hardware-specific inputs are required before a real world-locked 6DoF
OpenXR shell can replace the present stereo renderer. Gamescope and Steam do
not provide head tracking by themselves, and Valve's VRLink transport cannot
turn an unspecified Pi display into a Steam Frame headset.

The image includes an opt-in `stearlight-xr` OpenRC service and
`/usr/local/libexec/stearlight/xr-run`. After a tested Monado hardware driver
has been installed and configured, enable the standalone runtime with
`STEARLIGHT_XR_ENABLE=1` in `/etc/conf.d/stearlight-xr`, start
`rc-service stearlight-xr start`, then launch an OpenXR application through
`/usr/local/libexec/stearlight/xr-run`. If the service is not already running,
the wrapper starts Monado on demand and cleans up only that instance when the
application exits. Set `STEARLIGHT_XR_AUTOSTART=0` when another supervisor owns
the runtime; if its socket is not present, the wrapper fails before launching
the application. The wrapper validates the application, sets `XR_RUNTIME_JSON`
and the user runtime directory, and leaves the default Steam Frame session
unchanged. When standalone XR is explicitly enabled, the normal session also
exports the same OpenXR variables to Steam-launched games. The wrapper uses a
startup lock, per-launch leases, and the OpenRC supervisor pidfile, so repeated
or parallel game launches share Monado without terminating it while another
game is active or touching an externally owned service. `STEARLIGHT_XR_RUNTIME_SOCKET`, `STEARLIGHT_XR_MONADO_SERVICE`, and
`STEARLIGHT_STEAM_LINK_URI` are available
for a board-specific runtime layout, test harness, or Valve client URI variant.
For a Steam library title that needs the standalone runtime, enable the
configuration and set this per-game launch option:
`/usr/local/libexec/stearlight/launch-xr-game %command%`. Steam still owns the
title and its Proton/FEX/Steam Input lifecycle; the prefix only starts or
reuses Monado for that command and cleans up an instance it owns.
Native OpenXR titles work with that prefix without another dependency. Older
OpenVR titles additionally need an ARM64 OpenVR-to-OpenXR bridge such as an
ARM64 build of xrizer or OpenComposite. Set
`STEARLIGHT_XR_OPENVR_RUNTIME=/home/stearlight/.local/share/stearlight/xrizer`
and, for a title that requires it, set
`STEARLIGHT_XR_OPENVR_REQUIRED=1`. The wrapper exports `VR_OVERRIDE`, creates
the missing `openvrpaths.vrpath` registry without replacing an existing one,
maps the OpenXR manifest into the Steam Linux Runtime container, and exposes
`monado_comp_ipc` through `PRESSURE_VESSEL_FILESYSTEMS_RW`. An x86_64 bridge
archive with only a `bin/linux64` library cannot run on the ARM64 Pi and is
rejected. Leaving the bridge path empty keeps native OpenXR support available
and fails only when a game has explicitly required the bridge.
Build the bridge on an ARM64 Linux target with its upstream release/build
instructions (the resulting directory must contain
`bin/linuxarm64/vrclient.so`) and keep it under the Steam user's home
directory so Pressure Vessel can see it. The OS build does not silently
download an architecture-incompatible third-party binary.
For a keyboard-only bring-up, set `STEARLIGHT_XR_ACTIVE_CONFIG=qwerty`; the
service and wrapper enable Monado's qwerty device and debug GUI together. This
They use the session's `:8` X display by default; override it with
`STEARLIGHT_XR_DISPLAY` when using another display. This is a diagnostic input
path, not a substitute for the target headset's tracked driver.
The default Steam Link URI is `steamlink://lookup/`, the discovery route
exposed by Valve's `streaming_client`. Do not enable this service as a
substitute for Valve's Steam Link or VRLink transport.

The current service therefore proves the OS, silent boot, Steam/glibc boundary,
Gamescope session, 1440x1600-per-eye output, startup media, and curved shell
without falsely claiming synthetic tracking as 6DoF. The next hardware port is
to implement the headset driver in Monado and render the Steam surface as an
OpenXR quad/cylinder layer.

The legacy receiver follows the same rule: synthetic pose is disabled by
default and is available only for a test harness with
`SVRT_ENABLE_SYNTHETIC_POSE=1`. It is not a replacement for an IMU, camera, or
controller driver.

The repository's OpenXR check compiles a loader probe on any Alpine build host:

```sh
bash os/tests/openxr-runtime-test.sh
STEARLIGHT_XR_TEST_DISCOVERY=1 bash os/tests/openxr-runtime-test.sh
bash os/tests/xr-bridge-env-test.sh
bash os/tests/xr-lifecycle-test.sh
```

The second command checks that the installed Monado package exposes a qwerty or
simulated device builder without starting a compositor. Set
`STEARLIGHT_XR_TEST_START_MONADO=1` only on a target with a configured headset
driver and compositor; Docker/QEMU cannot provide that hardware session.

## VM smoke test

The x86_64 VM uses the same Alpine standalone shell and includes Valve's
x86_64 Steam bootstrap so the Steam client is started in the guest too. It is
a fast check for boot, service startup, Steam first-run rendering, and UI
composition. It does not emulate the Pi 4 GPU, firmware, headset timing, or
ARM64 Steam runtime.
`test-vm.ps1` uses VirtualBox when available and falls back to QEMU with WHPX
(or multi-threaded TCG), GTK/OpenGL, a `zoom-to-fit` window, an aspect-ratio
correction for the title-bar height, and a serial smoke test. The guest
framebuffer remains 2880x1600 while the host window is scaled to fit a standard
monitor. Add `-MeasureFps` to verify boot animation frame changes.

```powershell
.\os\build-vm.ps1
.\os\test-vm.ps1 -BootSeconds 300
```

The VM-only files are kept at the `os/` root (`Dockerfile.vm`,
`vm-overlay/`, `genimage-vm.cfg`, and the systemd-boot loader entries) so there is no separate
Alpine or VM project tree.
