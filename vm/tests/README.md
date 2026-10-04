# VM validation

The GitHub workflow checks sources/preparation, builds a fresh ARM64 disk,
checks ext4/qcow2 integrity and builds the Windows host. `launcher-test.ps1`
checks paths with spaces, a relative writable overlay moved with its base,
and launcher arguments.

```sh
sh vm/os/tests/layout-test.sh
sh vm/os/tests/steamvr-native-test.sh
sh vm/os/tests/frame-browser-test.sh
sh vm/os/tests/xr-bridge-env-test.sh
sh vm/os/tests/xr-lifecycle-test.sh
```

```powershell
.\vm\tests\launcher-test.ps1 -QemuDirectory .\vm\dist\qemu
```

Visual validation needs a Windows host with a Vulkan driver. Boot a **fresh**
package to login, capture both eyes and move the laser. Check the separate
vertical dock beside the panel and the bottom dock. The bottom Steam button
opens a different main menu. Confirm Gamescope, vrserver, vrcompositor,
vrdashboard and Steam webhelper remain running. Process names alone do not
establish that the UI rendered.

The legacy x86 VM and its checks have been removed.
