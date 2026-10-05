# Third-party components

The Stearlight OS build fetches Valve's public ARM64 Steam Frame recovery,
pinned and checksummed in `packages/sources.lock.json`. Steam, SteamVR,
Gamescope and Frame resources are extracted from that read-only recovery.
Valve's terms and the bundled legal notices apply to those payloads. Recovery
images, runtime binaries and personal Steam account data are not source files
in this repository.

`packages/openvr` is Valve's OpenVR SDK submodule at commit
`0924064316de3effbcd1acf1e309182a2deb1c05` (`v2.15.6`). Its license is in
`packages/openvr/LICENSE`; the virtual headset/controller driver compiles
against its public driver headers.

The root `Containerfile` assembles Alpine Linux packages, Debian GNU libc
libraries, Mesa Venus and Bluetooth firmware with their upstream licenses.
The native Gamescope payload comes from the Frame recovery.

`packages/windows-runtime` builds pinned QEMU (GPL-2.0), virglrenderer (MIT)
and their MSYS2 runtime dependencies. Renderer patches and their hashes are
recorded in its `windows-venus/sources.lock.json`. The download includes host
source archives, package versions and dependency license files.

Stearlight's license remains in `LICENSE.md`. The downloadable image includes
that license, this notice, OpenVR's license, Steam/SteamVR notices and the
Steam Subscriber Agreement under `licenses/`.
