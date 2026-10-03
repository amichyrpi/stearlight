# Stearlight ARM64 VM

The maintained target is an ARM64 Steam Frame test VM. Valve's Steam client,
SteamVR, and Gamescope own the panels, docks, cursor, and their behavior. The
small OpenVR test driver supplies a virtual headset and controllers because
QEMU has no physical Frame hardware.

Run QEMU natively on Windows. WSL is used for build and disk tools. Keep the
runtime archives, disk, QEMU bundle, logs, and screenshots on F: or G:.

## Run the existing F: build

From the repository root:

```powershell
.\vm\test-arm64.ps1 `
  -OutputDirectory 'F:\Stearlight-Frame-Session-2026-09-30' `
  -QemuDirectory 'F:\Stearlight-GPU-Bridge-2026-09-30\qemu-arm64-gtk-performance' `
  -SteamClientArchiveTar 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame\steam-client.tar' `
  -ScreenshotToolsTar 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame\runtime-extras.tar' `
  -WindowsGpuBridge -KeepRunning
```

The tester defaults to six virtual CPUs with multithreaded TCG. `-CpuCount`
can reduce this on smaller hosts. Emulated SVE/SME is disabled; the guest
retains NEON. Use the optimized QEMU bundle and optimized guest libraries.
The default presents 1440x800 stereo instead of 2880x1600, reducing the
number of copied pixels to one quarter. Valve's application overlay remains
1920x1080. Use `-LowResolution:$false` for the original stereo resolution.

The Windows session stays open after capture checks. It uses a disposable
disk snapshot by default. Add `-PreserveDiskChanges` when account/settings
changes must survive closing the VM. Run only one VM against a writable disk,
and close it before rebuilding or mounting that disk.

The output folder contains `serial-test.log`, `qemu-stderr.log`,
`windows-venus.log`, and `stearlight-arm64.png`. Inspect the actual native
window and screenshot; process readiness alone does not verify the UI.
Optional `-DebugConsolePort 23001` exposes a local guest console for testing.

## Controls and sign-in

The QEMU tablet moves the virtual right controller. Left click is its
trigger, right click/Escape is Back, Enter is A, and F1 is the system button.
SteamVR renders the laser, tests its intersection, and dispatches clicks.
The driver opens the dashboard once; repeated automated selections are off.

NetworkManager manages `VM Ethernet` with DHCP. QEMU provides wired internet
through its user-mode network; the VM has no Wi-Fi or Bluetooth adapter.
Steam's recovered Frame mobile-discovery login uses Bluetooth advertising.
Use **More Options… → Mobile App - Scan QR code**, or password sign-in.
The QR route renders in the native dashboard and does not need Bluetooth.

## Build from the recovery image

Docker Desktop's Linux engine must be running. The build extracts Valve's
ARM64 runtime and client, then creates a separate Alpine root filesystem;
the recovery image is only a content source.

```powershell
.\vm\build-arm64.ps1 `
  -RecoveryRawImage 'F:\Stearlight-VM-Test-2026-09-26\cache\recovery\steamframe-recovery.raw' `
  -OutputDirectory 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame' `
  -ZstdExecutable 'F:\Stearlight-VM-Test-2026-09-26\winq-arm-build\msys64\usr\bin\zstd.exe'
```

The Windows GPU path requires the patched Venus renderer under
[`host/windows-venus`](host/windows-venus/README.md), the guest Vulkan shim,
and the matching Mesa CPU-presentation patch. It prestarts SteamVR before
Gamescope and initializes native modifier support before forwarding Steam's
VR surfaces. The guarded Chrome IPC timeout adaptation and disabled CEF hang
watchdog allow initialization under ARM emulation. GNU `libnm0` exposes the
real NetworkManager connection to Steam's native network controller.

## Verification status

October 3 native captures verified Valve's curved panels, bottom dock,
left Steam menu, laser, and QR sign-in popup in both eyes at 1440x800.
Personal QR sign-in succeeded. The profile and Home game artwork render;
all 58 Home page images reported loaded. Controller Back returned from the
profile, the dock's Steam button opened the left menu, and the system button
hid the dashboard. Account state was synced to the maintained F: disk.

The fast configuration measured 7.87 FPS during dashboard startup,
6.25–6.37 FPS with sign-in and docks visible, and 9.12 FPS with signed-in
Home visible, versus 1.38 FPS in the earlier four-CPU 2880x1600 run. These
are eight-second compositor samples, not a measure of navigation latency.
Steam's native network store reports connected Ethernet and internet access.

One interaction run ended in a native `vrclient.so` shared-mutex assertion;
stability is not yet verified. A black or stale host window was restored by
refreshing the guest's `Virtual-1` output while its native framebuffer
continued rendering. Compare host and guest captures before attributing a
stale image to failed input. Mouse aiming still has parallax relative to
the visible controls; physical Frame controller behavior is not verified.

The current evidence is in
`F:\Stearlight-Windows-IPC-2026-10-03\fresh-native-ui`:
`signed-in-home-verified.png` (native Windows Home),
`signed-in-home-and-docks.png` (native Windows Home with both docks), and
`fast-qr-keyboard.png` (native QR popup).

ARM64 runs through CPU emulation on this AMD host. Performance must be
measured in the native compositor and window; a successful Vulkan transport
test does not establish an interactive frame rate. Keep checks focused and
do not claim physical Frame hardware behavior from a virtual device.
