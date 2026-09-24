# SteamVR dashboard runtime contract

This records behavior recovered from the authorized x86_64 Linux SteamVR
depot at `F:\StearlightBuildArchive\steamvr-linux`. The depot binaries are
stripped and their referenced `vrdashboard.dbg` file is not present. The
analysis used Ghidra on `vrdashboard`, `vrstartup`, `vrserver`, and
`vrcompositor`; it did not recover Valve's original C++ source. Keep the
runtime binaries and web bundle as the implementation of record instead of
treating this note as a source replacement.

## Dashboard startup

`vrstartup` is the coordinator. It checks whether `vrserver` is already
running, initializes the OpenVR runtime as a system process when needed, then
waits for server/HMD events and uses the applications interface for the
SteamVR tutorial/home-app startup flow. Starting the individual dashboard
binary without that runtime/session setup bypasses this lifecycle.

`vrdashboard` calls `VR_InitInternal2` with
`{"is_system_process" : true }`. Its startup path validates and acquires
`IVRSystem_026`, `IVRCompositor_029`, `IVROverlay_029`, and `IVRSettings_003`;
it also requires the HMD property container and an internal mailbox. Failure
to acquire the compositor, overlay, settings, or HMD-container path aborts
dashboard startup. Settings read during startup include
`steamvr.loglevel` and `steamvr.startOverlayAppsFromDashboard`.

The dashboard creates a `Dashboard Main` thread. Its main loop alternates
`RunSteamFrame` and `ProcessVREvents`, drains up to 60 events per poll, and
dispatches quit and screenshot events. The screenshot path distinguishes
triggered, taken, failed, and submitted events; a quit event exits the loop
before OpenVR shutdown. This is why a static HTML/CSS copy by itself cannot
provide the dashboard's controller navigation, overlay focus, or lifecycle.
The Valve JavaScript bundle contains 425 `VRHTML` references across 184
distinct member paths. They cover tracked poses/device roles; overlay state,
input method, focus and transforms; dashboard visibility/keyboard/lifecycle;
application launch and scene state; controller haptics; audio, battery,
chaperone, camera and volume events; typed settings/properties; VRLink; system
power operations; and OpenXR runtime/layer management. Important call sites
include
`CalcDashboardTransform`, `GetPose`,
`VRDashboardManager`, `VROverlay`, `VRApplications`, and
`VRLink.SendFocusDashboardBarRequest`. A browser host without that bridge is
not a compatible runtime, even if it serves every original CSS/HTML file.

That private bridge is the boundary for any independent dashboard host. The
bundle expects stateful getter/setter services and asynchronous event
registrations, not just a few static shims. Implementing it correctly would
also require reproducing overlay hit testing, device-index mapping, input
focus, controller state and compositor-owned transforms. The runtime in this
project therefore launches Valve's `vrstartup`/`vrserver`/`vrcompositor`/
`vrdashboard` rather than substituting a hand-built implementation.

## Runtime ownership

The SteamVR compositor contains the `CLaserMouse` and `CDashboardTransform`
implementations. Decompilation shows the laser setup reading
`dashboard.laserLength`; cursor setup reads `cursorScale`, `cursorOpacity`,
and `laserRayWidthScale`. It loads `overlay_pointer_ibeam.png` for the I-beam
cursor and `overlay_pointer_circle.png` for the pointer cursor. Its per-frame
ray path resolves an overlay hit to surface UV coordinates, optionally applies
overlay filtering and magnetism, then updates cursor position/state. The
settings `input.overlayMagnetismEnabled`, `overlayMagnetismLockDistance`, and
`overlayMagnetismDeadZone` govern that native interaction path. The shipped
Valve `laser_pointer` image/model files are therefore runtime inputs, not
stale copies of the removed SDL cursor.

`vrserver` owns dashboard start/started messages and dashboard overlay state;
the compositor owns rendering, the dashboard transform graph and laser
geometry. The `vrdashboard` process is a client of those runtime services,
not the source of the compositor's laser geometry. The binaries also carry
VRLink laser-mouse overlay/state/event channel names, so cursor movement and
controller interaction are transported through the runtime rather than
simulated by CSS or an SDL pointer.

The shipped defaults in `resources/settings/default.vrsettings` include
`allowCurvature=true`, `dashboardScale=1.0`, `laserLength=0.5`,
`laserRayWidthScale=1.0`, `cursorScale=1.0`, `cursorOpacity=1.0`,
`overlayMagnetismEnabled=true`, `overlayMagnetismLockDistance=0.025`, and
`overlayMagnetismDeadZone=0.6`. Keep these Valve-owned settings and code on
the native SteamVR path; do not replace them with SDL geometry or a fake
VRHTML shim.

The compositor also reads the non-default `steamvr.useFacetRenderer` switch
with a false fallback, and contains separate Facet/Vulkan renderer paths.
That alone does not establish that Facet avoids the shared-semaphore failure
or preserves the reference UI, so the VM configuration does not enable it.
Valve's Linux issue tracker describes Facet as not officially supported for
non-Steam-Link use: <https://github.com/ValveSoftware/SteamVR-for-Linux/issues/909>.

## Current VM bring-up blocker

The existing QEMU screenshot and serial log were checked before another image
build. The fake HMD and Frame controllers register, and Valve starts the
compositor/dashboard processes, but the compositor then reports
`VUID-VkExportSemaphoreCreateInfo-handleTypes-01124`: the selected llvmpipe
device does not support exportable opaque-FD semaphores. SteamVR subsequently
aborts in `vulkanrenderer.cpp` with `VkResult UNKNOWN_ERROR`, so there is no stable
native dashboard frame to compare with the reference yet. The earlier
`Failed to create direct mode surface` warning is expected for the tracking-
only HMD; the opaque-semaphore fatal is the blocker.

On 2026-09-24, the existing VDI was also tried with this host's QEMU
`virtio-gpu-gl-pci,venus=on` path using a disposable overlay. QEMU exited
before the guest booted because its `virglrenderer` was built without render
server support and could not initialize Venus. The base VDI was not modified.
Docker Desktop was also probed with the host RTX 3050 exposed to an Ubuntu
container; `vulkaninfo` still enumerated only llvmpipe, so that container path
does not currently expose the host GPU to SteamVR's Vulkan renderer either.
The Ubuntu QEMU package accepts the Venus device, but cannot initialize its
EGL renderer in Docker because no DRM render node is available, including with
software/surfaceless EGL. Neither Docker route currently provides a usable
Vulkan device to the guest.
Until the VM has a Vulkan device that supports the semaphore export SteamVR
requests, rebuilding it cannot produce the screenshot needed for visual
acceptance.

The launcher now waits for both the Steam client and `steamwebhelper` before
starting `vrstartup` (bounded by `STEARLIGHT_NATIVE_STEAM_WAIT_SECONDS`, 150
seconds by default). This avoids the separate early-start failure recorded
in `vrserver.txt` as `Steam is probably not running`.

## Local analysis

`tools/re/ExportSteamDashboard.java` is a Ghidra headless script that exports
decompiled functions which call OpenVR imports or reference dashboard,
laser, or curvature strings. It accepts the output
path as its first script argument. Analysis dumps and the temporary Ghidra/JDK
installation are kept outside the repository under
`F:\StearlightBuildTemp\ghidra`. The selected
function exports are `vrdashboard-decomp.txt`, `vrstartup-decomp.txt`,
`vrserver-decomp.txt`, and `vrcompositor-decomp.txt`; the installed tool and
download archives are disposable and not part of the runtime image.
