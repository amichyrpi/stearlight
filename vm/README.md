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
through its user-mode network; the VM has no Wi-Fi adapter. Valve's globe
icon is its native wired-network indicator, including when internet works.
Steam's recovered Frame mobile-discovery login uses Bluetooth advertising.
Use **More Options… → Mobile App - Scan QR code**, or password sign-in.
The QR route renders in the native dashboard and does not need Bluetooth.

The LTS kernel includes Bluetooth and USB/IP support. To temporarily assign
the approved Intel radio, install `usbipd-win` on Windows, find its bus ID
with `usbipd list`, and run `usbipd bind --busid 3-10` as administrator.
Add `-BluetoothBusId '3-10'` to the VM command (use the actual listed ID).
The guest attaches that radio and starts BlueZ before launching Steam.
Windows loses access to that radio while it is attached. After closing the
VM, run `usbipd unbind --busid 3-10` as administrator to remove sharing.
The tester uses `vmlinuz-lts`/`initramfs-lts` when both are available;
older output directories still boot their `virt` kernel.
The tester seeds UTC at boot because this LTS kernel lacks QEMU's PL031
clock driver. The clean boot verified Ethernet and the real Intel controller
powered on, including control by the normal Steam user. Steam's native mobile
login registered one real Bluetooth LE advertising instance. HCI monitoring
confirmed successful advertising commands, and the service data matched the
native login session and verification image. Android testing reported
"Unable to load headset data" before showing a device list. In Valve's
published Android app, a single scanned device is selected automatically;
that error follows a failed `Authentication.GetAuthSessionInfo` lookup.
The user then confirmed that waiting resolved the error and nearby-headset
Bluetooth sign-in succeeded. Native Home, the profile, bottom dock, left
navigation rail, and laser rendered; all 54 Home images reported loaded. The
boot clock seed now includes elapsed kernel time instead of losing the
initial service-start delay.

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

Optional `-BrowserGpu` enables the native Steam browser's Vulkan compositing
and rasterization through the Windows GPU bridge. It requires
`-WindowsGpuBridge` and the maintained Frame recovery's ANGLE library. A
small interposer corrects ANGLE's X11 visual metadata for compatible ARGB
windows; Valve's EGL library stays intact. Launching again without
`-BrowserGpu` restores the original browser launcher. This is an experimental
option: native Home has not demonstrated a speedup over the default path.

Startup displays Valve's original movie and animated loading spinner centered
in each eye of the stereo output. The display stays blank before playback;
startup diagnostics remain in `serial-test.log`.

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

October 4 added Bluetooth USB/IP, the LTS boot clock correction, and cheaper
process lookups. The startup path refreshes the Windows virtual output once
after the native compositor becomes ready. This recovered a blank host
window while the guest framebuffer continued rendering. The mobile login
panel measured 7.12 FPS; 30 FPS has not been reached. Early native browser GPU
trials failed with an ANGLE X11 visual/config mismatch. The maintained F: disk was
updated in place and booted; a fresh Docker image rebuild was not run.
After Bluetooth sign-in, Home measured 5.50 FPS in an eight-second sample.
A two-worker software-renderer trial was inconclusive: the restarted browser
did not initialize Home during observation. The original launcher was
restored; the trial is not enabled by default.
The clean reboot restored the saved account without another sign-in. Home,
the native Steam-button menu, both docks, and laser rendered in both eyes;
host and guest framebuffer captures matched. Home initialization took about
20 minutes on this emulated CPU, so a loading panel alone does not establish
a failed launch. Fully initialized Home measured 7.50 FPS. Restricting the
software-renderer workers to two guest CPUs also measured 7.50 FPS; their
original affinity was restored. A short Windows priority trial measured
8.37 FPS, within the range of earlier observations; normal priority remains
the default. These trials do not establish 30 FPS or a reliable speedup.

The Windows bridge now enables an XCB MIT-SHM presentation interposer. Venus
cannot import host memory for Mesa's normal shared-memory image path here;
the interposer transfers completed CPU pixels through X11 shared memory.
The real GPU fence and X-server completion acknowledgement remain in place.
Unsupported image formats or unavailable MIT-SHM use the original XCB path.
At 1440x800, a six-second Vulkan clear/present/readback check measured
7.05 FPS with the interposer disabled and 15.77-16.61 FPS with it enabled;
both paths returned the expected pixel. Fully rendered native Home then
measured 13.48 FPS over eight seconds. This is progress toward 30 FPS;
the faster isolated X11 upload result does not establish native frame rate.

Reducing the Windows renderer's idle fallback from 5 ms to 1 ms and enabling
adaptive timeline polling produced a 23.37 FPS native Home sample, with all
55 images loaded at 1280x720 inside the 1440x800 stereo output. A later Home
sample measured 15.37 FPS, so 23.37 is an observed sample, not a sustained
minimum. Actual GPU completion still controls shared timeline values.

Idle audio now suspends the null sink's clock after five seconds and resumes
when a stream opens. The timeline bridge publishes its final real GPU
counter before releasing a shared payload; the two-process completion check
passed after an idle interval and immediate semaphore teardown.
An event-wake timeline trial measured 16.74 FPS on native Home, and a reduced
X-server acknowledgement trial measured 17.00 FPS. Neither established a
gain over the existing adaptive path, so both trials were rolled back.
The acknowledgement trial reached 30.31 FPS in an isolated Vulkan check;
that result does not establish 30 FPS in SteamVR.

The ANGLE visual adaptation subsequently enabled actual native-browser GPU
compositing, rasterization, and WebGL on the RTX 3050. Fully rendered Home,
both docks, and the laser measured 15.00 FPS. A clean GPU-browser boot with
shared-memory presentation and an experimental tracking update cache measured
12.74 FPS in two eight-second samples. The tracking trial is not in the source
default. These measurements do not establish 30 FPS or a GPU-browser speedup.
The adapter's real ARGB-window check verified the rendered pixel, EGL error
propagation, and unchanged lookups for Mesa's EGL. The browser helper also
passed recovery-hash rejection and byte-for-byte launcher restoration checks.

The current evidence is in
`F:\Stearlight-Windows-IPC-2026-10-03\fresh-native-ui`:
`signed-in-home-verified.png` (native Windows Home),
`signed-in-home-and-docks.png` (native Windows Home with both docks), and
`fast-qr-keyboard.png` (native QR popup).
October 4 Bluetooth evidence: `bluetooth-home-after-reboot.png` (saved login
and rendered Home), `bluetooth-home-guest.png` (matching guest framebuffer),
and `bluetooth-steam-button-center.png` (native left menu opened with the
virtual controller after reboot).
Shared-memory evidence: `shm-home-progress-latest.png` (fully rendered
native Home and dock with the new CPU transfer path).
`native-home-23fps.png` captures the faster Home sample.

ARM64 runs through CPU emulation on this AMD host. Performance must be
measured in the native compositor and window; a successful Vulkan transport
test does not establish an interactive frame rate. Keep checks focused and
do not claim physical Frame hardware behavior from a virtual device.
