# Stearlight ARM64 VM

The maintained VM uses Valve's ARM64 Steam Frame runtime extracted from the
recovery image. The recovery disk is a read-only content source; the VM boots a
separate Alpine root filesystem. Valve's `vrcompositor`, `vrdashboard`, and
VRHTML content own the dock and laser cursor. A small fake OpenVR driver
supplies test HMD and controller poses and opens the dashboard.
The VM pulses the right controller's system button once. Repeated A/trigger
selections are disabled by default; set `STEARLIGHT_FAKE_AUTO_SELECT=1` only
for an onboarding diagnostic. In passive mode,
`STEARLIGHT_FAKE_OPEN_DASHBOARD=0` also disables the initial system-button pulse.

The ARM64 session follows the Steam Frame recovery's service ordering:
Gamescope starts with `--backend openvr --allow-deferred-backend`, publishes
its application endpoints, then the session starts SteamVR and finally Steam.
The Windows GPU path prestarts SteamVR before Gamescope to finish virtual
headset activation before a VR client queries its settings. The original
ordering crashed `vrserver` during activation in the tested VM.
The session reuses that prestarted runtime instead of launching a second
`vrstartup` coordinator.
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
VM execution uses the native Windows `qemu-system-aarch64.exe` launched by
`test-arm64.ps1`. WSL2 is used for build and disk preparation tools; running
QEMU inside WSL does not satisfy this project's Windows test requirement.

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
The native capture waits another 60 seconds after process readiness while
checking failure markers. This catches the tested frontend/compositor exit
that occurred after an earlier background-only capture.
The session also reports Steam or VR daemon exits after readiness as capture
failures.

An October 2 standalone native OpenVR run rendered Valve's curved dashboard,
dock and laser in both eyes, captured in the accepted Windows GTK window.
That capture used the desktop dashboard: a driver resource-path error prevented
the Frame HMD profile from loading. The resource path is now corrected, but
the improved Frame dock and laser have not yet produced a verified capture.
That run shows the native "Launch Steam" surface, not a working Steam client
or Gamescope application overlay. A focused OpenVR Gamescope run created the
1920x1080 overlay at 2.67 metres, but the compositor aborted before a usable
application capture. Its crash dump reports `pthreads futex robust_list is
corrupt` in Valve's `ipcposix.cpp:216`. SteamVR then shut down Gamescope; the
exit was not an out-of-memory kill. This remains an unresolved runtime failure.
Gamescope
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
compositor now renders Valve's stereo background into the real 2880x1600
guest window and the accepted 720x400 GTK host window. A real PulseAudio
server with a null sink fixes the native audio client's startup assertion.
GNU's library cache is refreshed after runtime extraction so Steam's GNU
dependencies do not fall through to Alpine's musl libraries.

Steam's UI still fails in the complete session. Traces show a shared robust
mutex-list fault in `chromehtml.so`; other runs exhaust shared memory with
25 MiB Chrome IPC buffers. Matching the recovery's libc, increasing shared memory
to 3 GiB, and changing Chromium's child startup did not fix the UI. These
experiments are not production changes. Some native boots also time out
waiting for the compositor's OpenVR API. SteamVR requires present-wait and
async compute; removing either feature aborts this recovery runtime.

October 2 tests rebuilt the Windows renderer and fixed shared host-visible
buffers and opaque optimal image tiling. All eleven focused transport cases
passed. Full boots still failed with the corrected Frame profile and a single
startup coordinator. Restoring the client and UI files from the recovery
archive did not produce a working dashboard; the web helper restarted and
shared IPC allocations grew.

The Frame facet renderer also failed with its scene-graph wait disabled.
A temporary shim increased its 15 ms presentation wait to one second: four
actual presentations completed successfully, but the renderer still reported
`No Vulkan command buffer open in CGpuTiming::MarkEvent!`,
`Failed WaitEvent(CompositorPresent): bad event=1`, and a GPU-work watchdog
abort. That timeout change and the diagnostic settings were removed from the
test disk. They are not part of the maintained launcher. A usable modern
Frame dock/laser/Gamescope screenshot remains unverified.

Further diagnostics found that the shim's synthetic Vulkan display makes the
compositor choose direct mode even with `direct_mode.enable=false`. Removing
that display did not select a usable desktop fallback: the native WSI window
failed, the reported refresh rate became zero, and rendering timed out.
Disabling async compute also failed renderer initialization. These diagnostic
changes were reverted.

The recovered Steam SDL library initializes X11 successfully, and its XRandR,
Vulkan, EGL and GL libraries resolve. Steam's updater replaced an experimental
CPU-affinity change; a second run confirmed all four VM CPUs were available
to its web helpers. That run still exhausted the 2 GiB shared-memory mount
with `std::bad_alloc`, before the Steam browser exposed a page. Process
readiness alone therefore does not verify the modern Frame UI.

The root filesystem now includes the full `lsof` package. Valve's WebUI
transport uses `lsof -P -F upnR` to validate the browser's socket-owning
process. BusyBox's applet returns incompatible output, causing HTTP 403
and `Checked: 0/<helper PID>` in the transport log. Installing full `lsof`
allowed the real Frame browser's WebSocket handshake without changing
Valve's authentication. Complete-session tests after this fix still failed:
selecting Valve's System browser composer or removing the helper's VR
composer preference did not produce a usable modern dashboard capture.

The queue-family shim now preserves the backing graphics queue's real compute
capability when it adds a compute-only alias for single-family drivers.
Previously, Gamescope could reject all graphics families and query surface
support for `UINT32_MAX`. A focused mock test covers both query versions,
truncated output and multi-family pass-through. The RTX 3050 has separate
graphics and compute families, so this alias correction does not fix the
remaining Windows Frame compositor failure.

The screenshot check also rejects uniform color captures. A solid blue
background is not evidence of a working dock, laser or application window.

An October 3 native Windows diagnostic rendered the modern Frame Steam
sign-in frontend through Gamescope's SDL backend in the accepted 720x400 GTK
window. It retained `-deckard` and `-gamepadui`, removed `-vrgamepadui` for
isolation, selected Valve's System browser composer (`--composer-mode=1`) and
used `-cef-disable-gpu` for CPU browser painting. These are disposable
diagnostic settings, not defaults for the complete VR session. The actual
capture is `F:/Stearlight-Frame-Session-2026-09-30/windows-gamescope-frame-cpu-host.png`.
A direct Xorg test first painted into the guest while GTK stayed black;
cycling the guest mode restored host presentation. The host client was then
returned to 720x400. Guest captures must respect the XWD pixel depth: this
frontend's capture reported 24 bits per pixel, rather than the 32 bits assumed
by an earlier diagnostic converter. Flat frontend rendering does not verify
the modern VR dock, laser, curved panel or synchronized stereo output.

Fresh-boot attempts to reproduce that frontend still exhausted the 2 GiB
shared-memory mount and exited with `std::bad_alloc`. Omitting the Vulkan
timeline shim, using the original launcher name, and pinning Steam and its
helpers to one guest CPU did not resolve the failure. The standalone launcher
remains an F: diagnostic; it is not a maintained tester option. Neither the
one successful flat screenshot nor the focused loading/animation previews
establish a working full Frame session. VM execution must use native Windows
QEMU; Linux cross-compilation and disk preparation do not change that target.

Later October 3 traces isolated the repeated allocations: Steam's master
response stream waited only 500 ms while its helper created two 25 MiB
connection buffers. The Windows GPU test now prepares a guarded adaptation
of the recovery's `chromehtml.so`, increasing that reply wait to 30 seconds.
It checks both original and patched SHA-256 hashes and refuses unknown builds.
The Windows GPU test keeps the prepared recovery client with
`-noverifyfiles -inhibitbootstrap`; its bootstrap otherwise reports mismatched
cached executables and can replace the guarded VM adaptations. The adaptation
preserves the library's ownership and mode, and the tester reports preparation
failure immediately for an unsupported client build.
With this change, a fresh native Windows boot rendered the flat Frame sign-in
UI and kept shared memory around 100 MiB. A full VR test also avoided shared
memory exhaustion and initialized the native VR frontend, but still reported
presentation wait failures and heap assertions in its combined process log.
A later full-session diagnostic ran for six minutes without an observed abort
in the three attached main processes; its debugger timeout terminated those
processes. Its host capture still showed only the stereo background. The dock,
laser and curved application panel remain unverified in the full session.
The maintained launch with the original browser composer subsequently reached
the VR frontend and kept shared memory below 180 MiB through a longer run.
Even explicit native `IVROverlay::ShowDashboard` requests returned visible
while Windows still displayed only the stereo loading scene. The automatic
capture check passed its process/color checks; visual inspection failed the
dock/panel requirement. The rebuilt passive controller driver also loaded in
native Windows QEMU, reported `auto_select=0 open_dashboard=1` for both hands,
and delivered the single system-button pulse without input errors.

A native Windows ARM probe also passed 400,000 protected updates using explicit
`CASAL` and `SWPAL` instructions across two processes and four threads. This
does not reproduce Valve's complete IPC implementation or establish its
correctness. The recovered SwiftShader ICD could not initialize Steam's Xlib
Vulkan surface; its subsequent shared-memory failure is not a valid graphics
backend comparison. No successful full-session screenshot is claimed.

The custom Windows renderer's source patches and build instructions are in
[host/windows-venus](host/windows-venus/README.md). Use the matching patched
bundle explicitly. `test-arm64.ps1 -WindowsGpuBridge` enables image export,
the guest timeline bridge, scoped glibc Vulkan loading, guarded Gamescope and
VRHTML upload adaptations, and CPU X11 presentation. The VRHTML change checks
the exact recovery binary hashes and selects coherent CPU-visible memory for
linear uploads. It is prepared inside the disposable guest before launch;
an unknown recovery build is rejected. It saves renderer diagnostics in
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
content. One black native window was traced to the guest scanning out its old
640x480 console framebuffer while Xorg still reported 2880x1600. Cycling the
Xorg mode restored the real stereo dashboard in GTK; the cause of that guest
scanout reset remains under investigation.

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
| ARM64 Steam, SteamVR, VRHTML, dock/laser assets and controller models | Standalone Valve dashboard, dock and laser captured in stereo on Windows. The complete Steam session remains blocked. |
| Frame Gamescope OpenVR session | Native 1920x1080, 2.67-metre overlay created. Application rendering remains unverified after the compositor's robust-list abort. |
| Stereo output | Real Valve stereo background captured at 2880x1600, with two 1440x1600 eyes. Synchronization and motion latency remain unmeasured. |
| Game compatibility and complete runtime | FEX, Proton and the complete game runtime have not been integrated or tested. |
| SteamOS platform and headset services | Arch/systemd, updates/recovery, hardware tracking/cameras, wireless streaming, power/battery and native audio services are not ported. Some are hardware-specific and unnecessary for a display VM. |

Windows is the required host. WSL2 and Docker Desktop provide extraction and
build tools on Windows; a separate Linux machine is not part of this setup.
A working native dock/laser/Gamescope capture remains the acceptance criterion.

## Native stereo path

The recovery's hardware driver is `cv`/`XRService`; `frame_hmd` is a resource-only
profile. Extraction now preserves that profile and its input resources. The VM
driver loads Valve's unchanged `frame_hmd_additional.vrsettings` through OpenVR's
`Prop_AdditionalDeviceSettingsPath_String`, using the named resource path
`{stearlight-fake}/frame_hmd_additional.vrsettings`. This brings in the Frame
dashboard, curvature, background and Linux async compositor defaults.
The VM keeps its own desktop-display geometry and requests direct mode off,
with a 60 Hz default to match QEMU. The experimental Windows Vulkan shim
still presents a synthetic direct-mode display to the compositor.
The VM also selects Valve's desktop renderer
and separate system-layer pass: the Frame facet path fails GPU timing checks
on this virtual display. These overrides retain the Frame UI settings but
do not establish a working Frame dashboard. The physical headset's optics, panel calibration,
tracking and display timing are not emulated.

The fake HMD now advertises a single 2880x1600 output with adjacent 1440x1600
eye viewports and matching per-eye render targets. Eye transforms use a 63 mm
virtual IPD. It no longer claims to report hardware vsync that it does not
provide. SteamVR owns stereo composition and presentation timing; Gamescope
provides the flat application overlay. The software boot preview is still
mono and cannot verify the native stereo path. Runtime synchronization and
lag require a working GPU path and measured compositor timings.

The older `build.ps1` and `test-vm.ps1` are outside the maintained ARM64 path.
