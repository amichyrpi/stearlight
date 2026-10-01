# Stearlight ARM64 VM

The maintained VM uses Valve's ARM64 Steam Frame runtime extracted from the
recovery image. The recovery disk is a read-only content source; the VM boots a
separate Alpine root filesystem. Valve's `vrcompositor`, `vrdashboard`, and
VRHTML content own the dock and laser cursor. A small fake OpenVR driver
supplies test HMD and controller poses and opens the dashboard.

The ARM64 session follows the Steam Frame recovery's service ordering:
Gamescope starts with `--backend openvr --allow-deferred-backend`, publishes
its application endpoints, then the session starts SteamVR and finally Steam.
The `steamframe` session profile uses 1280x720 application defaults, 1920x1080
overlay output, and a 2.67-metre initial overlay width. It enables the native
control bar, keyboard control and click stabilization. Curvature stays under
SteamVR's control, as in the recovery; these settings do not establish an exact
visual match. The cursor and icon are extracted from `/usr/share/steamos` in
the recovery. They are included in new builds and can be added to a cached
runtime using `extract-frame-runtime.ps1 -SessionAssetsOnly`.

On Windows, use Docker Desktop with Buildx and WSL2, plus a GTK-enabled QEMU
ARM64 build with Venus-capable virglrenderer. The build copies the ARM64
SteamVR runtime and Steam client archive out of the recovery image, then
creates a separate guest disk. Keep the recovery image, decompressed client archive, build files,
QEMU, logs, and screenshot on F: or G:.

```powershell
.\build-arm64.ps1 `
  -RecoveryRawImage 'F:\Stearlight-VM-Test-2026-09-26\cache\recovery\steamframe-recovery.raw' `
  -OutputDirectory 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame' `
  -ZstdExecutable 'F:\Stearlight-VM-Test-2026-09-26\winq-arm-build\msys64\usr\bin\zstd.exe'

.\test-arm64.ps1 `
  -OutputDirectory 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame' `
  -QemuDirectory 'F:\Stearlight-VM-Test-2026-09-29\qemu-arm64-gtk' `
  -DiskPath 'F:\Stearlight-VM-Test-2026-09-26\arm64-frame\stearlight-arm64.ext4'
```

The build preserves Linux symlinks, builds the guest and fake tracking driver,
and writes a journal-free ext4 disk to the output folder. The test boots with a
disposable QEMU snapshot, waits for SteamVR's compositor and dashboard, then
writes serial logs and a PNG capture beside the disk. Inspect the screenshot
before treating the dock, cursor, and Gamescope window as visually verified.

The native OpenVR session still has no verified dock/laser capture. Gamescope
comes from the recovery, with its glibc loader and libraries in
`/opt/frame-gamescope`. The guest Venus ICD is built from pinned Mesa 26.1.6
with the X11 software-presentation patch under `os/driver/mesa`.

October 1 Windows tests passed real two-process Vulkan image sharing for both
DMA_BUF and OPAQUE_FD, checking all 4096 pixels. The opt-in guest timeline
bridge passed real GPU waits and completion across two processes, including
Vulkan 1.1 KHR entry points. These tests establish the transport primitives;
they do not establish dashboard rendering or synchronized stereo output.

The isolated presentation test reproduced Mesa's missing software-upload
completion and a timeout reported as device loss. The patched library passed
present-wait and a pixel readback from the actual X11 window. The native
Frame stack is being checked separately. SteamVR requires present-wait and
async compute; removing either feature aborts this recovery runtime.

The custom Windows renderer's source patches and build instructions are in
[host/windows-venus](host/windows-venus/README.md). Use the matching patched
bundle explicitly. `test-arm64.ps1 -WindowsGpuBridge` enables image export,
the guest timeline bridge, scoped glibc Vulkan loading, the guarded Gamescope
upload adaptation, and CPU X11 presentation. It saves renderer diagnostics in
`windows-venus.log` beside the disk. It preserves the accepted GTK window
behavior. The original bundle remains available for comparison.

The startup FIFO has a bounded wait even if Gamescope exits before opening
it; the emulated ARM64 session allows 120 seconds for initial startup.

`test-arm64.ps1 -GamescopePreview` runs a focused SDL test with Valve's loading
asset inside Gamescope, without starting SteamVR. It uses the same GTK window
and writes `stearlight-gamescope-preview.png` beside the test disk. This mode
requires the custom renderer and updated guest scripts. Its upload adaptation
checks the exact recovery binary hashes and edits only a disposable copy.
It does not change the UI or replace Valve's original binary. A successful
preview does not validate the native dock, laser, curvature, or stereo output.
The fresh preview capture passes, but repeated GTK captures can still be blank
or displaced. Direct guest Gamescope captures confirm animated 1920x1080
content; the remaining host presentation/capture issue is separate.

The guest QEMU scanout/mirror stays at 2880x1600 at 60 Hz; the Frame Gamescope
overlay output is separately 1920x1080. The host
window matches the original GTK version of `test-vm.ps1` (commit `e35df19`):
`zoom-to-fit=on,show-menubar=off,window-close=on`. GTK chooses the initial size
and scales the guest image while preserving its aspect ratio. The tester keeps
the current window width and corrects only its height at launch and again when
Gamescope becomes ready. It does not force a 1280-pixel width or resize again
at capture. `-SecondaryMonitor` uses the original x86 placement behavior.

GTK enforces its native minimum size: a quarter of the guest surface, or
720x400 client pixels once the 2880x1600 mode is active. The September 30 window
check confirmed this limit, rejected a 100x80 resize, and confirmed that both
testers' aspect functions produce a 1280x711 client from the same 1280x800
window. Captures keep their actual dimensions. An SDL-only QEMU build is
rejected because its resizing behavior differs.

The installed GTK bundle above retains WINQ-EMU's Windows Venus renderer DLL.
When collecting GTK dependencies, do not overwrite that DLL with MSYS2's
standard `libvirglrenderer-1.dll`; it cannot initialize this Windows Venus
path. The earlier GTK/SDL session run reaches Gamescope but still hits the
compositor failure described above. Window parity does not establish native
dashboard readiness.

QEMU's EDID refresh rate is explicitly 60 Hz so the guest does not inherit
the host monitor's refresh rate.
Gamescope does not expose the Wayland screencopy protocol, so the PNG captures
the visible host window. For troubleshooting, `-DebugConsolePort 23001` opens an
optional guest shell on a host loopback TCP port.

## Boot and loading animations

New builds include a software X11 splash after the guest display is ready:
Valve's `steamui/movies/steamframe_startup_450x450.webm` plays once, then the
animated PNG `steamui/images/steam_spinner.png` loops. The files come unchanged
from the recovery's Steam client. Playback keeps their native dimensions on a
black background. This is a flat VM startup preview; it does not establish
headset projection, curvature or SteamVR readiness. Early kernel boot still
uses the console, and playback is muted. Steam retains its native startup UI.

ARM64 uses the earlier x86 appliance `inittab`: quiet OpenRC startup and no
getty or Alpine login prompt. The kernel command line uses the same quiet
console flags. Four brief startup lines precede the graphical session;
detailed logs remain in `serial-test.log`. Normal launcher output reports
startup and the capture path; window-fitting details are available with
`-Verbose`. The GTK sizing and minimum-size behavior are unchanged.

The splash stops when Steam becomes ready or the Gamescope session fails. The
build validates both animation files, the transition PNG and the player. Use
`test-arm64.ps1 -AnimationPreview` with the usual paths for a focused animation
boot; it skips Gamescope/SteamVR and saves `stearlight-animation-preview.png`
beside the disk. The normal test still requires a working native dashboard.

## Steam Frame coverage

The VM contains extracted runtime components, not a complete SteamOS port.
The recovery inspected on September 30 is SteamOS VR 0.3.0, build
`20260922.5152327`; this VM uses Alpine/OpenRC and a virtual hardware driver.

| Component | VM status |
| --- | --- |
| Frame startup movie and loading loop | Packaged; software VM display playback added. Native VR playback remains unverified. |
| ARM64 Steam, SteamVR, VRHTML, dock/laser assets and controller models | Extracted and wired into startup; a working native dock/laser image is still blocked. |
| Frame Gamescope OpenVR session | Valve's glibc Gamescope and OpenVR client load; isolated image transport and presentation pass, native session capture remains unverified. |
| Stereo output | Virtual HMD geometry corrected to one 2880x1600 output with two 1440x1600 eyes; live stereo rendering and synchronization remain unverified. |
| Game compatibility and complete runtime | FEX, Proton and the complete game runtime have not been integrated or tested. |
| SteamOS platform and headset services | Arch/systemd, updates/recovery, hardware tracking/cameras, wireless streaming, power/battery and native audio services are not ported. Some are hardware-specific and unnecessary for a display VM. |

Windows is the required host. WSL2 and Docker Desktop provide extraction and
build tools on Windows; a separate Linux machine is not part of this setup.
A working native dock/laser/Gamescope capture remains the acceptance criterion.

## Native stereo path

The recovery's hardware driver is `cv`/`XRService`; `frame_hmd` is a resource-only
profile. Extraction now preserves that profile and its input resources. The VM
driver loads Valve's unchanged `frame_hmd_additional.vrsettings` through OpenVR's
`Prop_AdditionalDeviceSettingsPath_String`. This brings in the Frame dashboard,
curvature, background, facet renderer and Linux async compositor defaults.
The VM keeps its own desktop-display geometry, disables direct mode and uses
a 60 Hz default to match QEMU. The physical headset's optics, panel calibration,
tracking and display timing are not emulated.

The fake HMD now advertises a single 2880x1600 output with adjacent 1440x1600
eye viewports and matching per-eye render targets. Eye transforms use a 63 mm
virtual IPD. It no longer claims to report hardware vsync that it does not
provide. SteamVR owns stereo composition and presentation timing; Gamescope
provides the flat application overlay. The software boot preview is still
mono and cannot verify the native stereo path. Runtime synchronization and
lag require a working GPU path and measured compositor timings.

The older `build.ps1` and `test-vm.ps1` are outside the maintained ARM64 path.
