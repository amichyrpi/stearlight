# Local Standable integration

Work in progress. This runs the owned Windows Steam installation of
[Standable](https://store.steampowered.com/app/2370570/) locally through ARM64
Proton and an ARM64 SteamVR adapter based on
[Ignition](https://github.com/BnuuySolutions/Ignition). It does not require a
streaming PC. Calibration and body estimation have not been verified in-headset.
The local test APK includes the bridge. The normal payload builder accepts separately built bridge artifacts through `STEARLIGHT_STANDABLE_BRIDGE`.

The settings GUI is presented as a managed SteamVR window without another
Gamescope/Xwayland process. Its X11 image is uploaded into a persistent Vulkan
texture only when the pixels change. On the Quest, settings clicks and native
move/resize controls work without the earlier flicker or moving dock line.
The OpenGL-sharing and browser network-service experiments were removed.
The native close button sends the GUI its normal window-close request, then
stops only its settings process if it remains alive. Hidden windows are not
raised again automatically. The Wine server and tracker driver stay running.

## Automated Quest checks

The Quest 2 prototype loads the unchanged Windows driver, passes bidirectional
ARM64/x86-64 shared-memory tests (including ring wraparound and embedded zero
bytes), and loads through SteamVR's normal driver registry. The interface and
driver share Steam's `compatdata/2370570` prefix and exchange their normal pipe
messages; the driver's authentication log reports success. All eight tracker
activations complete. This establishes registration, not calibrated pose quality.

SteamVR’s native Manage Add-ons switch controls loading the provider. It waits
for `dashboard.ready` in `/run/stearlight/quest` before starting the Windows host. The disabled-provider probe
completed 1,000 frame calls and cleanup in 1 ms on the Quest. Active driver work
is coalesced on a worker rather than blocking SteamVR's frame loop.

The first combined GUI/driver soak test exceeded Android's unchanged phantom
process limit and killed the Linux session. Removing the GUI's `steam.exe`
waiter and the host's Proton Python waiter allowed the combined session to run
for more than 90 seconds without another trim in the observed logs. This is
an initial startup check, not a sustained gaming soak. The test headset now uses SteamVR’s native startup overlay setting
(`steam.overlay.2370570`); its driver is listed under Manage Add-ons. Do not describe this as supported until calibration and load testing pass.

The bridge must hide its C++/OpenVR context symbols. Without isolation, loading
it replaces another driver's OpenVR context, misattributes its logging and
rejects its pose updates. The isolated build registers all eight trackers without
those cross-driver errors.

## Rebuild the prototype

Clone Ignition at `6af5d9ae36a5385161d7e0cc0d0b7b08b8bc6bcc`. The helper verifies
the revision and applies `build_files/quest/ignition-arm64.patch`. Use separate
Windows and Linux build directories. Dependencies are CMake, Git, Ninja and the
AArch64 GCC cross compilers on Linux; Visual Studio 2022 C++ tools on Windows.

```sh
python3 build_files/quest/build-standable-bridge.py native \
  --source /path/to/Ignition --build /tmp/standable-native --output /tmp/standable/native
```

```powershell
python build_files/quest/build-standable-bridge.py windows `
  --source C:/path/to/Ignition --build C:/build/standable-windows --output C:/build/standable/windows
```

The outputs include the upstream MIT license. No Standable executable, driver,
settings, Steam SDK DLL or user data is redistributed by these build commands.
The current private hardware prototype uses a genuine `steam_api64.dll` from
the owner's Windows SteamVR installation; the private test APK carries that same dependency. Public redistribution remains
unresolved; the payload builder accepts it only through the explicit
`STEARLIGHT_STANDABLE_STEAM_API64` input. Do not substitute an authentication emulator.

## Runtime wiring

Install Standable normally through Steam first. Its Windows DLL and resources
remain in the game's directory. The adapter belongs at
`bin/linuxarm64/driver_standable.so` beside an `ignition.json` pointing to the
real DLL, built `ignition_server.exe`, and the maintained `standable-host.py`
launcher. `wine_cmd` must pass `--game` with the installed game directory.
The host uses `runinprefix` and the GUI's existing prefix so their named pipes
and normal authentication can communicate. It must not initialize another
private prefix or launch another Steam client.

The Proton launcher changes apply only to app 2370570. They retain Wine RPC and
networking while disabling unused HID/USB services. Other games keep their
existing launch path. A shared prefix must already exist before the host starts.
The settings launch joins that prefix without a Python, Windows Steam, or
subreaper waiter. Windows external-driver paths are translated to Wine paths
without changing the native SteamVR registry.

For a diagnostic-only disabled-adapter check:

```sh
standable-native-probe /path/to/driver_standable.so --passive
```

Use SteamVR Settings > Startup/Shutdown > Manage Add-ons to disable the
Standable driver, and the startup overlay list to disable its interface.
`standable-startup --enable` enables the existing native overlay registration;
it does not create a replacement startup system. Choices persist in SteamVR.
The app’s startup script refreshes the ARM64 adapter beside the owned Windows
DLL before starting SteamVR, including after an APK/runtime update.

Next checks: sustained gaming soak, real chaperone/floor data, controller poses,
Standable calibration and estimated tracker motion.
No calibration, consent or license dialogs are accepted automatically.
