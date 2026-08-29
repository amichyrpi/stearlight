# Stearlight OS (Raspberry Pi 4)

This is the Fedora bootc ARM64 target for Stearlight. The active source is
fully contained in this `os/` tree; it has no Armada git submodule.

```text
os/
  Containerfile              Fedora bootc ARM64 image
  build.sh                   image + raw/QCOW2 build entry point
  run-vm.sh                  QEMU VM runner
  boot/config.txt            Pi 4 V3D/KMS and splash configuration
  system_files/              required session, Decky sync, FEX profiles
  scripts/                   Steam, FEX, Proton, and session helpers
  decky/                     vendored Armada Decky plugins
  build_files/               Steam bootstrap helper
```

The vendored Armada components provide the Steam session, Gamescope
environment, Decky plugins, and FEX contract. Unused Qualcomm, ABL, Waydroid,
desktop, and handheld-only payloads are omitted. The Pi profile supplies its
own kernel, firmware, and V3D configuration.

The image has no desktop-mode target, no password getty, no enabled SSH
service, and no copied Wi-Fi secret. Steam's first-run UI owns language,
account, and NetworkManager setup.

## Build

The build requires Docker Buildx with ARM64 emulation. The image build is:

```sh
bash os/build.sh
```

It produces raw and QCOW2 images and compresses the raw image as
`out/stearlight-armada/stearlight-os-{UTC date}rp4.img.gz`.

Run the disposable VM with:

```sh
bash os/run-vm.sh
```

The VM validates the bootc image and Gamescope/Steam session. It cannot
validate Pi firmware, V3D, headset display timing, or hardware tracking.

## Device scope

This is a Pi 4 port. The stereo contract is 1440×1600 per eye (2880×1600
side-by-side output). A real 6DoF OpenXR compositor still requires the
headset-specific tracking and display bridge to be connected to this session.
