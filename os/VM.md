# Stearlight VM test image

This target builds a real x86_64 GPT disk with an EFI System Partition, GRUB,
an Alpine ext4 root filesystem, Linux kernel/initramfs, OpenRC, Xorg, and the
same Stearlight receiver and UI assets used by the Raspberry Pi image. The VM
advertises and selects a 2880x1600 display at 60 Hz; the side-by-side shell is
therefore 1440x1600 per eye.

VirtualBox or QEMU on an x86_64 host cannot execute the Pi's ARM64 guest. The VM
target therefore validates boot, service startup, video decoding, window
composition, and UI rendering. The VM runs the receiver-only shell because an
x86_64 guest cannot execute the ARM64 Steam runtime. It does not validate
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
