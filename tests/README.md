# Validation

From the repository root:

```sh
bash build_files/check.sh
```

With the bundled Windows runtime:

```powershell
.\tests\launcher-test.ps1 -QemuDirectory .\dist\qemu
```

These check recovery preparation, Containerfile inputs, runtime lifecycle,
launcher paths and the portable account disk. GitHub also builds the ARM64
image, checks its filesystem, builds QEMU and validates the ZIP layout.

For visual verification, boot a freshly extracted package on a Windows host
with a Vulkan driver. Capture native sign-in, the separate vertical dock,
bottom dock and laser in both eyes. Check laser movement and dock hover.
The developer capture helper is `tests/test-arm64.ps1`.
