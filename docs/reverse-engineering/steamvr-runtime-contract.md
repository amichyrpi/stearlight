# SteamVR dashboard runtime contract

This records behavior recovered from Valve's Linux SteamVR runtime. The
maintained VM uses the ARM64 runtime extracted from Steam Frame recovery; the
older analysis below used authorized x86_64 SteamVR binaries. The depot
binaries are stripped and do not include Valve's original C++ source. Keep the
extracted runtime and web bundle as the implementation of record; this note
describes runtime behavior rather than replacing its source.

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

## VM visual validation

The native implementation is Valve's SteamVR runtime; source/layout checks alone
cannot prove the dashboard dock or controller laser is visible. The ARM64 test
harness boots with Virtio-GPU/Venus, waits for SteamVR's compositor and
dashboard, then saves a PPM framebuffer for visual inspection.
