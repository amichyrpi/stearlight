# VM validation

The GitHub workflow checks sources/preparation, builds a fresh ARM64 disk,
checks ext4/qcow2 integrity and builds the Windows host. `launcher-test.ps1`
checks paths with spaces, a relative writable overlay moved with its base,
and launcher arguments.

```sh
bash build_files/check.sh
# Or: just check
```

```powershell
.\tests\launcher-test.ps1 -QemuDirectory .\dist\qemu
```

Visual validation needs a Windows host with a Vulkan driver. Boot a **fresh**
package to login, capture both eyes and move the laser. Check the separate
vertical dock beside the panel and the bottom dock. The bottom Steam button
opens a different main menu. Confirm Gamescope, vrserver, vrcompositor,
vrdashboard and Steam webhelper remain running. Process names alone do not
establish that the UI rendered.

`layout-test.sh` checks the OS directories and every local Containerfile
input, including paths changed during repository reorganization. The legacy
streaming project is archived outside this repository.
