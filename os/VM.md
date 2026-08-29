# Stearlight VirtualBox test image

This target builds a real x86_64 GPT disk with an EFI System Partition, GRUB,
an Alpine ext4 root filesystem, Linux kernel/initramfs, OpenRC, Xorg, and the
same Stearlight receiver and UI assets used by the Raspberry Pi image.

VirtualBox on an x86_64 host cannot execute the Pi's ARM64 guest. The VM target
therefore validates boot, service startup, video decoding, window composition,
and UI rendering. It does not validate Raspberry Pi firmware, VC4 KMS, or the
ARM64 Steam runtime.

Build and run an automated screenshot test from PowerShell:

```powershell
.\os\build-vm.ps1
.\os\test-vm.ps1 -BootSeconds 45
```

Outputs are written to `out/stearlight-vm/`. The test creates a VM named
`Stearlight-OS-Test`, boots it headlessly, captures `stearlight-vm.png` and the
serial console log, verifies that the framebuffer is not black, then powers it
off. The runner tests a disposable VDI copy so the built image and its SHA-256
manifest remain unchanged. Pass `-KeepRunning` to inspect it in VirtualBox after
the screenshot.
