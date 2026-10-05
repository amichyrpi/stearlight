# Stearlight ARM64 Steam Frame VM

Valve's ARM64 Steam client, SteamVR and Gamescope own the dashboard, docks,
laser and sign-in. A small test driver supplies the virtual headset and
controllers. This is an alpha test VM.

## Download and start

1. Download the **stearlight-frame-windows** artifact from a successful
   **ARM64 Frame VM** GitHub Actions run, or the ZIP from a `vm-*` release.
2. Extract the artifact ZIP, then extract `stearlight-frame-windows.zip`
   into a writable folder. Keep its files together.
3. Double-click **start-vm.cmd**. Running the download needs no Docker, WSL,
   QEMU installation, administrator account or particular drive letter.
4. Wait for the stereo boot movie, loading animation and native Steam login.
   Choose **More Options... > Mobile App - Scan QR code**, or password sign-in.

Use Windows 10/11 **x64**, a Vulkan-capable GPU with its vendor driver, at least
8 GB host RAM and about 12 GB free disk space for the package and account disk.
The GPU bridge has been tested on NVIDIA RTX 3050. Other GPUs require testing.
ARM64 CPU execution uses QEMU emulation; startup can take several minutes.
Sustained 30 FPS has not been achieved on the tested Ryzen host.

Account/settings are in **data/user.qcow2**, separate from the fresh base image.
Closing the window retains them. Keep `data/` when moving the whole folder.
To return to fresh sign-in, close the VM and move `data/` aside.
Run only one instance per extracted folder.

## Dashboard controls

Move the mouse to aim the right controller's native laser. Left click is
Trigger, Enter is A/Select, right click or Escape is Back, arrow keys navigate
and F1 toggles the SteamVR dashboard.

The **vertical dock beside the main panel** is Valve's per-frame navigation
dock. It is separate from the **Steam button on the bottom dock**, which opens
Steam's main menu. Startup waits for actual Steam overlay textures before
selecting the dashboard, including the dynamically named frame-menu surface,
and refreshes Windows scanout once those surfaces appear. The native rail can
hide when the laser leaves its frame; aim back at the panel to show it.
The virtual headset reports its proximity sensor as worn so SteamVR does not
enter standby and hide the panels while the stationary VM is loading.

Internet uses QEMU's Ethernet connection. Valve's globe icon represents that
wired connection; the VM has no Wi-Fi device. QR sign-in needs no Bluetooth.
Optional real Bluetooth USB/IP requires a separately shared adapter:
`powershell -File .\start-vm.ps1 -BluetoothBusId YOUR_BUS_ID`.
Windows loses access to that radio while the guest uses it.

Logs: `data/serial.log`, `data/qemu.log`, `data/renderer.log`.
Optional `-DebugConsolePort` and `-QmpPort` bind debugging to localhost.
The native runtime has had occasional shared-mutex crashes; logs help inspect
failed starts, and restarting retains the account disk.

## Build

The GitHub workflow builds the image on `ubuntu-24.04-arm`, builds Windows
GTK/Venus QEMU from pinned sources on MSYS2 UCRT64, then assembles the runnable
ZIP with checksums, notices and host source. Push a `vm-*` tag to also attach
the ZIP to a GitHub release.

The public Valve recovery is pinned in the repository's
`packages/sources.lock.json` (also copied into the download). Its compressed
SHA-256 is verified before mounting its read-only root partition. Images are
freshly built and sanitized, with no developer account data.

On Linux, install Docker/Buildx, Python 3, pax-utils, btrfs-progs, zstd,
e2fsprogs and qemu-utils:

```sh
git submodule update --init --recursive
sudo bash build_files/build-image.sh dist
# Reuse a decompressed recovery for local development:
sudo bash build_files/build-image.sh dist /path/to/recovery.raw
```

In MSYS2 UCRT64, install the packages listed in `.github/workflows/build.yml`,
then run `bash packages/windows-runtime/build-runtime.sh /path/to/new-build dist/qemu`.

`build_files/build-arm64.ps1` and `tests/test-arm64.ps1` retain the developer
extraction and capture path. The root `Containerfile` assembles the maintained
ARM64 download. The repository's `tests/README.md` describes validation.
