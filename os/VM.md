# Stearlight VM test image

This target builds a real x86_64 GPT disk with an EFI System Partition, GRUB,
an Alpine ext4 root filesystem, Linux kernel/initramfs, OpenRC, Xorg, and the
same Stearlight receiver and UI assets used by the Raspberry Pi image.

VirtualBox or QEMU on an x86_64 host cannot execute the Pi's ARM64 guest. The VM
target therefore validates boot, service startup, video decoding, window
composition, and UI rendering. It does not validate Raspberry Pi firmware, VC4
KMS, or the ARM64 Steam runtime.

Build and run an automated screenshot test from PowerShell:

```powershell
.\os\build-vm.ps1
.\os\test-vm.ps1 -BootSeconds 45
```

Outputs are written to `out/stearlight-vm/` unless an explicit output directory
is supplied. With VirtualBox installed the test captures a framebuffer; without
it, `test-vm.ps1` starts QEMU with an SDL window and performs a serial-console
smoke test. Pass `-KeepRunning` to leave the QEMU window running.
