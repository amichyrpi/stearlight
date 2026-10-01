# H.265 SVRT

H.265 SVRT is the streaming software project; its source stays at the repository
root. The maintained Stearlight ARM64 Steam Frame test VM lives under
[`vm/`](vm/README.md).

The VM builds a separate Alpine ARM64 disk from Steam Frame recovery content.
Valve's native SteamVR compositor, dashboard, and VRHTML runtime provide the
dock and laser; the VM's fake OpenVR driver supplies test tracking poses.
See [vm/README.md](vm/README.md) for the F:/G: build and screenshot commands.
