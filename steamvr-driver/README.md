# SteamVR packages

The native Steam Frame path uses Valve's built-in
`SteamVR/drivers/vrlink` driver. Valve's Steam client and SteamVR own discovery,
authorization, transport, tracking, controller input, and the Steam Frame UI.
The default Stearlight OS path therefore has no external SteamVR driver to
install and never uses IHSlib for this flow.

`svrt` is the legacy custom H.265 transport driver. It is retained only for the
old receiver protocol and must not be registered for the native Steam Frame
path.

The optional `svrt-vrlink` package is resource-only. It may be generated only
when the exact Android `os/Build/PRODUCT` value reported by the headset is
known:

```powershell
.\scripts\install-driver.ps1 -InstallVrlinkResources `
  -ProductName '<exact android/os/Build/PRODUCT value>'
```

It contains no Valve binaries and no pairing, streaming, tracking, or
controller implementation. Do not invent `vrlink_*` aliases; Steam Link
selects settings by the exact product string. If an old `svrt` registration
exists, remove it before testing the native path.
