# Stearlight OS VM

## Start

Download `stearlight-frame-windows.zip` from the
[Continuous build](https://github.com/amichyrpi/stearlight/releases/tag/continuous),
extract all files into a writable folder and double-click `start-vm.cmd`.
If using a GitHub Actions artifact, extract its outer ZIP first.

Use Windows 10/11 x64, a Vulkan-capable GPU with its vendor driver, at least
8 GB RAM and about 12 GB free disk space. QEMU is bundled; Docker, WSL and
administrator access are not needed to run the VM.

This is an alpha ARM64 VM. CPU execution is emulated on x64; first startup
can take 10-15 minutes on the tested host. Sustained 30 FPS has not been
achieved. Wait through the stereo boot/loading animations for native Steam
sign-in. Use **More Options > Mobile App - Scan QR code**, or password sign-in.

## Controls

Click inside the window to focus its mouse and keyboard.

- Move the mouse to aim the native laser.
- Left click: Trigger; Enter: Select.
- Right click or Escape: Back; arrow keys: navigation.
- F1: toggle the SteamVR dashboard.

The vertical dock beside the main panel is separate from the Steam button
on the bottom dock. It can hide when the laser leaves the frame; aim back
at the panel to show it.

Internet uses Ethernet, represented by the globe icon. QR sign-in needs
no Bluetooth. To use a separately shared USB/IP Bluetooth adapter:
`powershell -File .\start-vm.ps1 -BluetoothBusId YOUR_BUS_ID`.
The guest uses that adapter while the VM is running.

## Saved data and logs

Account state is in `data/user.qcow2`. Closing the VM retains it. Keep the
whole extracted folder together and run one instance at a time. Extract a
new build into a new folder; keep the old folder to preserve its account disk.
To reset the current build to fresh sign-in, close it and move `data/` aside.

Logs are in `data/serial.log`, `data/qemu.log` and `data/renderer.log`.
If startup fails, close and restart the VM. Optional `-DebugConsolePort`
and `-QmpPort` bind debugging to localhost.
