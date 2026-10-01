# Experimental Windows Venus renderer

These source patches preserve the Windows GPU bridge work independently of
the downloaded DLL. They are diagnostic; the native Frame dashboard still
has no verified rendered image. The accepted GTK window code is unchanged.

Base: `cmspam/winq-emu-virglrenderer` commit
`e80354b8f5a0dedc62374b905a181874682659cc` (MIT).
Patches 0001–0003 are from Try Omarchy's WINQ-EMU r20c source package; their
original contents and hashes are recorded in `sources.lock.json`.
Patch 0004 adds the Stearlight fixes:

- Include the descriptor count in failed resource replies. Omitting it leaves
  QEMU waiting for bytes that never arrive.
- Preserve the renderer logger instead of installing a callback that discards
  its messages.
- Offer CPU-visible memory only for types that can import an actual Windows
  section. The tested RTX 3050 permits types 3 and 4, whereas its GPU-local BAR
  type 5 cannot import that memory.

## Build

Use an MSYS2 UCRT64 shell with Meson, Ninja, a C compiler, Vulkan headers,
Python YAML, and the renderer's existing development dependencies. Keep the
checkout, build, and temporary directory on F: or G:.

```sh
git clone https://github.com/cmspam/winq-emu-virglrenderer.git /f/Stearlight-Venus/source
git -C /f/Stearlight-Venus/source checkout --detach e80354b8f5a0dedc62374b905a181874682659cc
# Replace this path with the current worktree path; it is read-only input.
patch_dir=/c/Users/jfran/.t3/worktrees/H.265-SVRT/t3code-70883554/vm/host/windows-venus/patches
for patch in "$patch_dir"/*.patch; do
  git -C /f/Stearlight-Venus/source apply "$patch" || exit 1
done
bash /c/Users/jfran/.t3/worktrees/H.265-SVRT/t3code-70883554/vm/host/windows-venus/build.sh /f/Stearlight-Venus/source /f/Stearlight-Venus/build
```

The DLL is `build/src/libvirglrenderer-1.dll`. Stop the project QEMU before
copying it into a separate copy of the GTK bundle. Retain the original bundle
for comparison, and select the candidate with `test-arm64.ps1 -QemuDirectory`.
Set `VIRGL_LOG_FILE` to an explicit F: log before enabling `WINQ_DIAG=1`.

## October 1 result

The clean patched renderer compiles. Resource-failure handling and compatible
memory selection were exercised in the Windows ARM64 VM; QEMU remained
responsive after the tested failures. A temporary eight-byte change to the
recovery Gamescope upload allocation allowed ordinary CPU-visible coherent
memory in place of CPU-visible coherent GPU-local memory. This passed upload
allocation and reached Xwayland. The guarded adaptation is applied only to a
disposable guest copy when the tester selects a Windows GPU diagnostic mode.

Software Xwayland avoids the tested GL buffer-import failure. A clean session
with CPU WSI presentation, `VN_DEBUG=no_drm_syncobj`, and synchronous buffer
creation/allocation rendered a red test application through Gamescope. Earlier
captures were black. The working test removed `VN_PERF=no_multi_ring` as well;
the individual effect of each synchronization option has not been isolated.
Patch 0005 now transports actual Vulkan image allocations across renderer
contexts, with recorded allocation memory types and matching GPU/driver UUIDs.
Image export is opt-in with `STEARLIGHT_VULKAN_IMAGE_EXPORT=1`; ordinary buffer
export remains restricted. Both DMA_BUF and OPAQUE_FD image roundtrip tests
passed all 4096 pixels across independent guest processes.

The separate guest shim supplies opt-in timeline FD transport, preserving real
GPU timeline semaphores and mirroring only completed values. Its two-process
test passed pending GPU waits and completion in both directions, including
Vulkan 1.1 KHR entry points. It supports permanent imports into fresh timeline
semaphores; it does not provide generic binary-semaphore FD transport.

Mesa 26.1.6's CPU X11 path also needed actual upload completion reporting and
correct timeout handling. The pinned source patch is under `vm/os/driver/mesa`.
The isolated present-wait and X11 pixel-readback test passes. These results do
not verify native dashboard texture interoperability, dock/laser rendering,
curvature, stereo synchronization, or GL import of these Vulkan handles.

The OS tester's `-GamescopePreview` reproduces this focused rendering path with
Valve's loading animation. It retains the accepted GTK sizing and uses the
guarded upload adaptation only in that diagnostic mode.
The fresh automatic GTK capture passed; repeated host captures can still be
blank or displaced. Two direct 1920x1080 guest Gamescope captures confirm
animated content. Host presentation/capture is therefore still under investigation.

Builds, diagnostic logs, and black captures are saved under
`F:/Stearlight-GPU-Bridge-2026-09-30` and
`F:/Stearlight-Frame-Session-2026-09-30`.
