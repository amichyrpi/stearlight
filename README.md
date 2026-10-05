# Stearlight OS

Stearlight is an alpha **ARM64 Steam Frame test OS**. Valve's ARM64 Steam
client, SteamVR and Gamescope provide its native dashboard, separate left
dock, bottom dock, laser and sign-in. The maintained target is a Windows-hosted
QEMU VM with a small virtual headset/controller driver.

Download the Windows VM package from [GitHub Actions](https://github.com/amichyrpi/stearlight/actions/workflows/build.yml)
or a tagged [VM release](https://github.com/amichyrpi/stearlight/releases).
Extract the artifact ZIP, then extract `stearlight-frame-windows.zip` into a
writable folder and double-click `start-vm.cmd`. Docker, WSL and a separate
QEMU installation are not needed to run the download. See the
[launch instructions](post_process/README.md) for controls and requirements.

ARM64 CPU execution is emulated on x64. First startup can take several minutes;
sustained 30 FPS has not been achieved on the tested host.

## Repository layout

The layout follows [Armada](https://github.com/armada-os/armada)'s separation
of image assembly, packages, installed files and disk-image preparation.
Stearlight retains its Alpine/Frame runtime and existing VM build.

| Path | Purpose |
| --- | --- |
| `Containerfile` | Assemble the ARM64 OS root filesystem |
| `build_files/` | Fetch and extract recovery payloads, build the image, run checks |
| `packages/` | Native runtime adaptations, OpenVR SDK, Windows QEMU sources and patches |
| `system_files/` | Files installed under the guest's `/etc` and `/usr` |
| `disk_config/` | Root disk size; ext4 and qcow2 assembly is in the image builder |
| `post_process/` | Portable Windows launcher and ZIP/checksum assembly |
| `tests/` | Focused source, lifecycle, graphics and launcher checks |
| `.github/workflows/` | Image, Windows runtime and download builds |

## Development

```sh
git submodule update --init --recursive
bash build_files/check.sh
sudo bash build_files/build-image.sh dist
```

The image builder needs Linux with Docker/Buildx, Python 3, pax-utils,
btrfs-progs, zstd, e2fsprogs and qemu-utils. GitHub builds the image on an
ARM64 runner. `packages/sources.lock.json` pins Valve's public recovery and
QEMU sources; the recovery checksum is verified before extraction.

The optional `Justfile` provides `just check`, `just build`, `just packages`,
`just package` and `just run-vm`. The Windows runtime recipe requires MSYS2
UCRT64 and the dependencies in the GitHub workflow. The package and launch
recipes run on Windows.

The workflow produces **stearlight-frame-windows**. A `vm-*` tag also publishes
its ZIP and checksum as a GitHub release. Built files go in `dist/`; account
state belongs to the extracted package's `data/`, never the source tree.
See [tests/README.md](tests/README.md) for validation.
