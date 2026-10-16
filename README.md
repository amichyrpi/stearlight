<p align="center">
  <a href="https://github.com/amichyrpi/stearlight">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="assets/stearlight_small_white.svg">
      <img src="assets/stearlight_small_black.svg" alt="Stearlight">
    </picture>
  </a>
</p>

<h1 align="center">Stearlight OS</h1>

<p align="center"><strong>Steam Frame OS Quest application</strong></p>

<p align="center">
  Full Steam Frame OS port with Proton, FEX, streaming, and standalone capability all in one app.
</p>

<p align="center">
  <a href="https://github.com/amichyrpi/stearlight/releases/tag/continuous"><strong>Install Stearlight OS</strong></a>
  ·
  <a href="https://github.com/amichyrpi/stearlight#known-issues">Known issues</a>
</p>

<p align="center">
  <a href="https://github.com/amichyrpi/stearlight">
    <picture>
      <img src="assets/celeste-passthrough-2026-10-13.png" alt="Celeste standalone">
    </picture>
  </a>
</p>

<p align="center">
  <a href="https://github.com/amichyrpi/stearlight" style="display: inline-flex; align-items: center; gap: 40px;">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="assets/steamos_powered_white.png">
      <img src="assets/steamos_powered_black.png" alt="Steam OS" height="120">
    </picture>
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="assets/steamvr_white.png">
      <img src="assets/steamvr_black.png" alt="Steam VR" height="120">
    </picture>
  </a>
</p>

## About

Stearlight is a Linux application for the Quest platform
that brings the full Steam Frame OS to the Quest.

It includes:

- ARM64 SteamVR with FEX and Proton
- Tracking support and passthrough
- VR streaming for VR and non-VR games
- Bluetooth support for sign-in
- Standalone capability for VR and non-VR games

## Known-issues

- [ ] Blue dots appearing in the right eye when the headset is in use.

## Performance

For a better VR experience, you will need to disable battery-saving
mode on your Quest. That means a smoother experience but shorter playtime.

## Development

This project bundles a VM version of the OS and a Quest version of the OS.
The VM version is not maintained anymore, so the Quest
version is the only one maintained.

### Standalone gaming

Every game is different and therefore has different issues. In this
project, we have added patches for standalone gaming, but they are not perfect,
and some games still don't run. If you want to run a game and you have patched
the project for your game, your changes will be merged.

### Next update

- [ ] Hand traking
- [ ] Controller support via Bluetooth

## Build for VM

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

## Contribution

Before contributing to this project, please look at the [`Contribution guidelines`](CONTRIBUTING.md).

## License

Stearlight code is licensed under the **FSL-1.1-ALv2** license. Valve assets and intellectual property are owned by Valve themselves.
