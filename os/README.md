# Stearlight OS — Alpine Raspberry Pi image

This target builds a flashable aarch64 Alpine image for Raspberry Pi 4. It is
an appliance image, not an in-place conversion of Raspberry Pi OS.

## What the current image contains

- Alpine edge with OpenRC and the Raspberry Pi downstream `linux-rpi` kernel.
- Native aarch64 Mesa/V3DV, Gamescope, PipeWire/WirePlumber, and Monado/OpenXR.
- A Debian glibc runtime isolated with Bubblewrap for the Steam ARM64 beta.
- Steam Gamepad UI inside Gamescope, captured by the Stearlight stereo shell.
- A 2880x1600 side-by-side scanout (1440x1600 per eye), pure black world, and
  one smaller curved floating Steam surface.
- `assets/os/os_boot.mp4` as the first userspace splash and
  `steam_loading.mkv` for Steam/connection transitions.
- Silent firmware/kernel configuration, no desktop and no console on the HMD.
- No local login prompt: the supervised Stearlight UI takes tty1 immediately.
- SSH as `stearlight@ROOT`, password `stearlight`, with passwordless `doas`.

The password is intentionally the requested development default. Change it
before putting a headset on an untrusted network.

## Build

Use a Linux host or WSL2 with Docker Desktop and BuildKit. Expect the first
build to download several gigabytes because Steam and its glibc runtime are
baked into the image to avoid doing that work during the first headset boot.

```sh
bash ./os/build.sh
```

On Windows, the local wrapper first exports the currently connected Wi-Fi
profile without printing its password, then invokes the WSL2 builder:

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

## Local Wi-Fi provisioning

`capture-wifi.ps1` exports the selected Windows WLAN profile into the ignored
`os/generated/wifi.env` file using base64-safe values. The file is mode
0600 in the image. On first boot, the `stearlight-wifi` OpenRC service gives it
to IWD, deletes the clear provisioning file after a successful connection, and
lets IWD retain its protected auto-connect profile.

## Hide the Pi 4 EEPROM diagnostic screen

`DISABLE_HDMI` is stored in the board EEPROM, not in an operating-system
image. Before replacing Raspberry Pi OS, run this once on the target Pi:

```sh
sudo bash ./os/apply-quiet-eeprom.sh
sudo reboot
```

The script preserves all unrelated EEPROM keys and schedules only
`DISABLE_HDMI=1` and `BOOT_UART=0`. The image itself also sets
`disable_splash=1`, `quiet`, `loglevel=0`, `logo.nologo`, and moves the hidden
kernel console to tty12. Local password gettys are disabled, so the screen
stays black until the startup movie can be shown
after DRM is ready.

## XR boundary

Monado is installed as the native OpenXR runtime, but it is not started by
default yet. The current Pi prototype has no specified IMU/camera tracking
driver, calibration, lens distortion profile, or display timing interface.
Those hardware-specific inputs are required before a real world-locked 6DoF
OpenXR shell can replace the present stereo renderer. Gamescope and Steam do
not provide head tracking by themselves.

The current service therefore proves the OS, silent boot, Steam/glibc boundary,
Gamescope session, 1440x1600-per-eye output, startup media, and curved shell
without falsely claiming synthetic tracking as 6DoF. The next hardware port is
to implement the headset driver in Monado and render the Steam surface as an
OpenXR quad/cylinder layer.

## VM smoke test

The x86_64 VM uses the same Alpine receiver build and a small EFI image; it is a
fast check for boot, service startup, and UI composition. It does not
emulate the Pi 4 GPU, firmware, headset timing, or ARM64 Steam runtime.
`test-vm.ps1` uses VirtualBox when available and falls back to QEMU with an SDL
window and serial smoke test.

```powershell
.\os\build-vm.ps1
.\os\test-vm.ps1 -BootSeconds 45
```

The VM-only files are kept at the `os/` root (`Dockerfile.vm`,
`vm-overlay/`, `genimage-vm.cfg`, and `grub-vm.cfg`) so there is no separate
Alpine or VM project tree.
