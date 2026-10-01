# Third-party components

The Stearlight VM build does not check SteamVR binaries or Steam Frame recovery files into
this repository. The build consumes an authorized x86_64 SteamVR tree supplied
by the developer and downloads Steam Frame UI resources from Valve's recovery
index. Valve's terms apply to those binaries and resources.

The fake tracking driver is compiled against Valve's public OpenVR driver API;
the Dockerfile downloads the OpenVR headers from ValveSoftware/openvr at tag
`v2.15.6`. The VM installs Gamescope from Alpine's package repositories.
Refer to each upstream project for its license and notices.
