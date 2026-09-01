# Stearlight VM test image

This target builds a real x86_64 GPT disk with an EFI System Partition,
systemd-boot, an Alpine ext4 root filesystem, Linux kernel/initramfs, OpenRC, Xorg, the
standalone Stearlight shell, and Valve's x86_64 Steam bootstrap. The VM
advertises and selects a 2880x1600 display at 60 Hz; the side-by-side shell is
therefore 1440x1600 per eye.

The EFI partition carries the kernel and initramfs so this VM path does not
depend on the GRUB 2.14 EFI `LoadFile2` initrd handoff, which is unreliable with
the OVMF firmware used by QEMU. The Pi image keeps its native firmware/kernel
path.

VirtualBox or QEMU on an x86_64 host cannot execute the Pi's ARM64 Steam
runtime. The VM uses the native x86_64 Steam bootstrap instead, while keeping
the same standalone shell and first-run Steam path. It does not validate
Raspberry Pi firmware, VC4 KMS, or the ARM64 Steam runtime.

Build and run an automated screenshot test from PowerShell:

```powershell
.\os\build-vm.ps1
.\os\test-vm.ps1 -BootSeconds 45
```

Outputs are written to `out/stearlight-vm/` unless an explicit output directory
is supplied. With VirtualBox installed the test captures a framebuffer; without
it, `test-vm.ps1` starts QEMU with WHPX (falling back to multi-threaded TCG),
GTK/OpenGL presentation, and a `zoom-to-fit` window. It corrects the outer
window height for the 2880x1600 aspect ratio (the guest remains 2880x1600 while
the host window fits a standard monitor), waits for the exact display marker,
and captures QEMU's P6 framebuffer dump (`stearlight-vm.ppm`).
Use `-MeasureFps` to sample the boot movie while diagnosing host rendering
performance.
The test rejects the wrong dimensions, a black framebuffer, or a display/runtime
error.
Pass `-KeepRunning` to leave the QEMU window running.
