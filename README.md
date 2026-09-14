<p align="center">
    <img src="/assets/stearlight_small_white.svg#gh-dark-mode-only">
    <img src="/assets/stearlight_small.svg#gh-light-mode-only">
</p>

[![Build](https://github.com/amichyrpi/stearlight/actions/workflows/build.yml/badge.svg)](https://github.com/amichyrpi/stearlight/actions/workflows/build.yml)

# Stearlight

This is a **work in progress**. SteamVR driver and Raspberry Pi 4 receiver. This project is in development and is not stable, consider it as **alpha**.

# Naming comvention

In this project, naming convention can be really confusing, But here some usefull informations to understand them:

- The `H.265` prefix is used to indicate that the project is using the H.265 video codec.

- The `SVRT` prefix is used to indicate that the project is using the SteamVR runtime.

- The `Stearlight` prefix is used to indicate that the project is using our SteamOS based distro.

## TODO

**This project is shifting from the `H.265-SVRT` runtime to the `SteamOS` distro, so the TODO list is not up to date.**

- [ ] Create the SVRT Utility App
  - [ ] Framerate and latency measurements
  - [x] Framerate changer with 60 Hz and 30 Hz presets
  - [ ] SteamVr driver installer with sync with latest version and automatic updates
  - [ ] Usage time measurements
  - [ ] Longest session time measurements
  - [ ] Start steamvr button that load the driver in steamvr otherwise it will not be loaded
  - [ ] Tracking support to pair a vive tracker to the headset
  - [ ] SVRT Firmware updater

## Usage/Installing

Builds and driver are available on the [Releases](https://github.com/amichyrpi/stearlight/releases) page.

### Stearlight OS appliance image

Stearlight OS is a custom Alpine aarch64 distro, it uses native Gamescope stack, isolated Steam glibc runtime, and 1440x1600 per eye shell.

We use a separated build and test environment for the aarch64 distro and the VM.

For any informations about the aarch64 distro and the VM, please refer to the [os/README.md](os/README.md) and [os/VM.md](os/VM.md) files.

### Compiling the legacy H.265 receiver (optional)

The native Stearlight OS path does not build a custom receiver or use IHSlib.
Build the Alpine image with `os/build.sh` as described in
[os/README.md](os/README.md). The commands below are retained only for
developers working on the old H.265 transport; they require an explicit
legacy opt-in and must not be used for Steam Frame/VRLink streaming.

Before compilling H.265 SVRT you need to be running a 64-bit Raspberry Pi OS image with KMS enabled and FFmpeg. If you don't have a 64-bit Raspberry Pi OS image, you can install it by using [Raspberry Pi Imager](https://www.raspberrypi.com/software/). You can enable KMS and install FFmpeg using the following commands:

- **KMS**
  ```sh
  config=/boot/firmware/config.txt
  test -f "$config" || config=/boot/config.txt
  grep -qxF 'dtoverlay=vc4-kms-v3d' "$config" || \
    echo 'dtoverlay=vc4-kms-v3d' | sudo tee -a "$config"
  sudo reboot
  ```

- **FFmpeg**
  ```sh
  sudo apt update
  sudo apt install -y ffmpeg
  ffmpeg -hide_banner -decoders 2>&1 | grep hevc_v4l2request
  ```

- **Disable Wi-Fi power saving**

  Wi-Fi power saving can introduce periodic latency spikes, bitrate stalls,
  and dropped stream frames. Disable it persistently for the active `wlan0`
  NetworkManager connection, then reboot to apply the radio setting:

  ```sh
  wifi_connection="$(nmcli -g GENERAL.CONNECTION device show wlan0)"
  sudo nmcli connection modify "$wifi_connection" 802-11-wireless.powersave 2
  sudo reboot
  ```

  After reconnecting, verify that the profile reports the raw value `2`:

  ```sh
  wifi_connection="$(nmcli -g GENERAL.CONNECTION device show wlan0)"
  nmcli -g 802-11-wireless.powersave connection show "$wifi_connection"
  ```

- **Lock the Pi 4 core clock for high-rate KMS output**

  Lock the core clock at its normal 500 MHz performance value to avoid a
  display clock transition while high-rate video is being scanned out:

  ```sh
  config=/boot/firmware/config.txt
  test -f "$config" || config=/boot/config.txt
  grep -qxF 'force_turbo=1' "$config" || echo 'force_turbo=1' | sudo tee -a "$config"
  grep -qxF 'core_freq_min=500' "$config" || echo 'core_freq_min=500' | sudo tee -a "$config"
  sudo reboot
  ```

  This increases idle power use and heat. Verify adequate cooling and power
  after reboot:

  ```sh
  vcgencmd measure_clock core
  vcgencmd measure_temp
  vcgencmd get_throttled
  ```

H.265 SVRT requires the following libraries to be installed:

- **Before dependencies installation**
  ```sh
  sudo apt update
  sudo apt full-upgrade -y
  ```

- **Dependencies installation**
  ```sh
  sudo apt install -y build-essential cmake git ninja-build pkg-config \
    libsdl2-dev libavformat-dev libavcodec-dev libavutil-dev libdrm-dev \
    libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libudev-dev libasound2-dev
  ```

The build process is otherwise normal for a CMake program:

```sh
git clone https://github.com/amichyrpi/stearlight.git
cd stearlight
mkdir build && cd build
cmake .. -G Ninja -DSVRT_BUILD_DRIVER=OFF \
  -DSVRT_BUILD_RECEIVER=ON \
  -DSVRT_BUILD_VENDORED_SDL=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
ctest --output-on-failure
```

Optionally, to install the program:

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin svrt-receiver
sudo usermod -aG input,video,render svrt-receiver
sudo cmake --install .
sudo /usr/local/libexec/svrt/install-steam-arm64-pi.sh
sudo systemctl daemon-reload
sudo systemctl enable --now svrt-receiver.service
```

The service runs as the `svrt-receiver` user with membership in the `input`,
`video`, and `render` groups. This grants controller and DRM access without
running the receiver as root. The Steam installer installs Valve's native
ARM64 beta client and runtime under `/var/lib/svrt-receiver`, then applies the
ARMv8.0 compatibility files required by Raspberry Pi 4 CPUs.

The graphical legacy receiver starts Valve's native Gamepad UI client. When
Steam Frame mode is enabled it passes the installed Valve client's
`-steamframe` option, and
the connection tile opens Valve's registered `steamlink://lookup/` handler. Pairing,
authorization, transport, and the Steam Frame controls remain inside the
client; the receiver never implements a second protocol. `SVRT_STEAM_FRAME=0`
and `SVRT_START_IN_STREAMING_MODE=1` remain legacy-receiver compatibility
controls only. Native Steam Frame mode never enters the legacy custom pairing
path.

### SteamVR driver setup

For the native Steam Frame/VRLink path, use Valve's built-in SteamVR `vrlink`
driver. The Steam client and SteamVR provide pairing, authorization, transport,
and the Steam Frame UI. Tracking and controller input are provided only when
the connected headset exposes the hardware contract Valve expects. Stearlight
does not register a guessed third-party driver and does not reproduce the Steam
Link protocol.

Run `scripts\install-driver.ps1` without switches to remove any old `svrt`
registration and verify that Valve's native VRLink driver is installed. Do not
register the legacy `build/svrt` DLL for this path. The legacy package remains
available only for the old custom H.265 receiver protocol.

An optional resource-only package is available only when the exact Android
product string sent by the headset is known:

```powershell
scripts\install-driver.ps1 -InstallVrlinkResources `
  -ProductName '<exact android/os/Build/PRODUCT value>'
```

The package adds panel metadata to Valve's driver; it contains no pairing,
streaming, tracking, or controller implementation. The utility and legacy
package are available from the [Releases](https://github.com/amichyrpi/stearlight/releases) page.

You can also build and install the driver manually by using the following commands:

- **Windows**
  ```powershell
  git clone https://github.com/amichyrpi/stearlight.git
  Set-Location stearlight
  cmake -S . -B build -A x64 `
    -DSVRT_BUILD_PI_LIBRARY=OFF `
    -DSVRT_BUILD_RECEIVER=OFF `
    -DSVRT_BUILD_TESTS=OFF
  cmake --build build --config Release --parallel
  $vrpathreg = Join-Path ${env:ProgramFiles(x86)} `
    'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
  # Native Steam Frame/VRLink: Valve's built-in driver owns the path.
  & $vrpathreg removedriverswithname svrt
  ```

If an exact headset product value is available, the optional resource package
can be generated with
`-DSVRT_BUILD_VRLINK_RESOURCES=ON -DSVRT_VRLINK_PRODUCT=<product>` and then
registered with `vrpathreg`. Do not invent a product alias: Steam Link selects
these settings by the exact value reported by the headset.

The legacy custom H.265 driver is still available as an explicit compatibility
path by registering `build\svrt` (or by running
`scripts\install-driver.ps1 -LegacyH265`).

### Native Steam Frame starting order

The Stearlight OS session starts Valve's Steam client in its native
`-steamframe` mode at Raspberry Pi boot. On the PC, leave Valve's built-in
`vrlink` driver installed; no Stearlight driver process or custom pairing daemon
is started for this path.

### Legacy H.265 transport (optional)

The following transport details apply only to the explicit legacy receiver
build (`SVRT_LEGACY_H265=1`, `SVRT_STEAM_FRAME=0`) and are not used by native
Steam Frame/VRLink streaming.

The legacy status endpoint does not claim valid tracking until a real sensor
driver is connected. `SVRT_ENABLE_SYNTHETIC_POSE=1` enables bounded test motion
for protocol bring-up only; it must not be used for a headset session.

The Raspberry Pi receiver is started at Raspberry Pi boot, and the legacy
SteamVR driver is started at SteamVR boot. The driver automatically detects
when the Raspberry Pi receiver becomes available.

### Stereo resolution and transport

The headset renders `1440x1600` per eye and packs both eyes side by side in one
`2880x1600` HEVC stream. The Raspberry Pi decodes that stream with the single
`rpivid` H.265 hardware decoder and presents its DRM PRIME frames directly with
KMS. The selectable refresh rates are 60 Hz and 30 Hz, with 60 Hz as the
default. Runtime discovery uses mDNS/DNS-SD (`_stearlight._tcp.local`) with a
UDP discovery fallback. TCP 9945 carries session/configuration and clock-sync
messages; video uses 1200-byte UDP datagrams on 9944 with 10+2 Reed-Solomon
FEC and bounded frame reassembly. Tracking returns over UDP 9947 at 250 Hz,
and audio uses UDP 9946. SSH is only a development/deployment tool.

To start the installed Raspberry Pi receiver manually:

```sh
sudo systemctl start svrt-receiver.service
```

To run it directly without the systemd service:

```sh
sudo systemctl stop svrt-receiver.service
cd ~/stearlight/build
sudo env SDL_VIDEODRIVER=kmsdrm ./pi-receiver/svrt-receiver 9944
```

To run at boot without a connected HDMI display:

```sh
sudo systemctl edit svrt-receiver.service
```

Enter the following override, save it, and restart the service:

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/svrt-receiver --headless 9944
```

```sh
sudo systemctl daemon-reload
sudo systemctl restart svrt-receiver.service
```

## Testing stream latency

Stop SteamVR before running the latency tester so it can use the Raspberry Pi video connection.

- **Windows**
  ```powershell
  py -m pip install av
  py scripts\stream-latency-test.py ROOT.local --frames 30
  ```

- **Linux**
  ```sh
  python3 -m pip install --user av
  python3 scripts/stream-latency-test.py ROOT.local --frames 30
  ```

## License

This project is licensed under the Functional Source License, Version 1.1,
with Apache License 2.0 as the Future License (`FSL-1.1-ALv2`). See the
[LICENSE](LICENSE) file for details.

This project uses parts of the [Vanilla](https://github.com/vanilla-wiiu/vanilla) code, to handle the DRM scanout and the KMS overlay plane. The Vanilla code is licensed under the GPL-2.0 License. See the [LICENSE](https://github.com/vanilla-wiiu/vanilla/blob/master/LICENSE) file for details.
