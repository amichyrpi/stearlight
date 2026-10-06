# Stearlight OS

An alpha ARM64 Steam Frame test OS. Valve's Steam client, SteamVR and
Gamescope provide the native dashboard, separate left dock, bottom dock,
laser and sign-in. The maintained target is a Windows QEMU VM.

## Download and run

1. Download `stearlight-frame-windows.zip` from the
   [Continuous build](https://github.com/amichyrpi/stearlight/releases/tag/continuous).
2. Extract the entire ZIP into a writable folder.
3. Double-click `start-vm.cmd` and wait for the native sign-in screen.

The download includes QEMU. Running it needs no Docker or WSL. ARM64 CPU
execution is emulated on x64; first startup can take 10-15 minutes on the
tested host. See [VM instructions](post_process/README.md) for requirements,
controls and account storage.

## Source layout

The layout follows [Armada](https://github.com/armada-os/armada).

| Path | Purpose |
| --- | --- |
| `Containerfile` | ARM64 OS assembly |
| `build_files/` | Recovery extraction, image builder and checks |
| `packages/` | Runtime sources, OpenVR SDK and Windows QEMU patches |
| `system_files/` | Guest `/etc` and `/usr` files |
| `disk_config/` | Root disk configuration |
| `post_process/` | Windows packaging and launcher |
| `tests/` | Focused source and runtime checks |

## Build

On Linux, install Docker/Buildx, Python 3, pax-utils, btrfs-progs, zstd,
e2fsprogs and qemu-utils:

```sh
git submodule update --init --recursive
bash build_files/check.sh
sudo bash build_files/build-image.sh dist
```

`just --list` shows check, build, Windows runtime, package and launch recipes.
The Windows runtime needs MSYS2 UCRT64; its dependencies are in the workflow.
Windows packaging requires `7z` on `PATH`. Source payloads are pinned in
`packages/sources.lock.json`; outputs go in `dist/`.

Successful main builds update the **Continuous build** release. Maintainers
can also publish through the workflow's manual option. A `vm-*` tag produces
a versioned release. See [validation instructions](tests/README.md).
