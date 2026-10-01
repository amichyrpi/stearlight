# VM tests

Run the quick source and runtime-preparation checks from the repository root:

```sh
sh vm/os/tests/layout-test.sh
sh vm/os/tests/steam-firstboot-test.sh
sh vm/os/tests/steam-runtime-patch-test.sh
sh vm/os/tests/steamvr-native-test.sh
sh vm/os/tests/xr-bridge-env-test.sh
sh vm/os/tests/xr-lifecycle-test.sh
```

The full visual/runtime test is vm/test-vm.ps1. It builds the image if needed,
boots QEMU with Virtio-GPU/Venus, waits for the SteamVR compositor, dashboard,
and synthetic dashboard input, then saves a PPM framebuffer for inspection.
It requires Docker/Buildx, WSL2, QEMU, and an authorized x86_64 SteamVR source
tree.
