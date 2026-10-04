# Windows Venus renderer

Build Windows GTK QEMU and its Venus renderer with
`bash vm/host/build-runtime.sh BUILD_DIR OUTPUT_DIR` in MSYS2 UCRT64.
The GitHub workflow lists the dependencies.

The renderer is pinned to
[cmspam/winq-emu-virglrenderer](https://github.com/cmspam/winq-emu-virglrenderer)
commit `e80354b8f5a0dedc62374b905a181874682659cc` (MIT).
`sources.lock.json` records the eight required patches and hashes, which
the complete host builder verifies before applying patches.

These fix Windows handle transport, host-visible memory selection,
Vulkan image/buffer sharing, replies and idle ring wakeups. They are required
for the tested SteamVR/Gamescope path. The GTK WGL guard lives in
`../qemu/patches/`. Failed direct-presentation experiment patches were removed.

SteamVR and Gamescope use GPU Vulkan rendering. Completed stereo pixels still
cross a CPU/X11 presentation path, and ARM64 CPU execution is emulated on x64.
Native login, Home, bottom dock, left Steam menu and laser have rendered on
RTX 3050; full GPU scanout and sustained 30 FPS have not been established.

`build.sh` builds only an already patched renderer tree. Use the complete
host builder for the portable package and corresponding source.
See [VM instructions](../../README.md) for launch and limitations.