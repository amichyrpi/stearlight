# Experimental Windows Venus renderer

These source patches preserve the Windows GPU bridge work independently of
the downloaded DLL. They are diagnostic. Valve's native stereo dashboard,
dock and laser now render in the guest and the accepted GTK host window in a
standalone test. The complete Steam/Gamescope session remains unfinished.
The GTK window code is unchanged.

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
Image export is opt-in with `STEARLIGHT_VULKAN_IMAGE_EXPORT=1`. Both DMA_BUF and OPAQUE_FD image roundtrip tests
passed all 4096 pixels across independent guest processes.

The separate guest shim supplies opt-in timeline FD transport, preserving real
GPU timeline semaphores and mirroring only completed values. Its two-process
test passed pending GPU waits and completion in both directions, including
Vulkan 1.1 KHR entry points. It supports permanent imports into fresh timeline
semaphores; it does not provide generic binary-semaphore FD transport.

Mesa 26.1.6's CPU X11 path also needed actual upload completion reporting and
correct timeout handling. The pinned source patch is under `vm/os/driver/mesa`.
The isolated present-wait and X11 pixel-readback test passes. A clean native
boot also renders Valve's stereo background at 2880x1600 and displays it in
the 720x400 GTK host window. The real PulseAudio endpoint fixes a native
audio startup assertion; GNU's library cache fixes dependency routing.
Steam's UI remains blocked by a shared robust mutex-list fault in
`chromehtml.so` and by repeated shared-memory IPC allocation. A 3 GiB
mount and the recovery's libc did not fix it. Some native boots also time
out waiting for the compositor's OpenVR API.
The October 2 native VRHTML trace exposed a GPU-local/CPU-visible allocation
check that no VM memory type satisfied. Its guarded guest upload adaptation
selects coherent CPU-visible memory. Native textures then allocate and share
successfully, and the real curved dashboard, dock and laser render in both
eyes with the desktop dashboard. The Frame HMD profile was not loaded in
that capture because its driver resource path was wrong. After correcting
the path, the Frame facet renderer exposed additional transport failures.
The improved Frame UI, stereo synchronization and GL import remain unverified.

Patch 0007 shares coherent host-visible Vulkan buffers through real Windows
sections and host-pointer imports, including allocations without a dedicated
buffer hint. Patch 0008 preserves optimal tiling for opaque external images;
the Windows shim previously forced every external image to linear tiling.
Eight two-process buffer tests pass, covering DMA_BUF/OPAQUE_FD, dedicated
and non-dedicated allocation, and cached/uncached coherent memory. GPU writes
are verified in both directions. Three image tests pass all 4096 pixels:
DMA_BUF linear, OPAQUE_FD linear and OPAQUE_FD optimal. These fixes remove the
Frame renderer's tested shared-buffer and icon-image allocation failures.
Native presentation and the shared robust mutex-list failure still block
the full Frame session.

After these fixes, the Frame facet renderer still aborts in its own GPU timing
path. A diagnostic allowed one second for native presentation waits instead
of 15 ms. Four real waits completed successfully, yet the renderer still
reported a missing command buffer, `bad event=1`, and a GPU-work watchdog
abort. The diagnostic was reverted. Successful transport or presentation
tests therefore do not establish Frame renderer compatibility. Full-session
tests with the recovery's original Steam client also failed to produce its UI.

An early native timing trace observed `Begin`, `End`, `Pre` and `Post` events
with null command buffers before any timing-buffer assignment. Removing the
synthetic Vulkan display did not activate a working fallback: the native WSI
window failed and the reported refresh rate became zero. The native GPU
still exposes distinct graphics and compute queue families, so the shim's
single-family compute alias is inactive on the tested RTX 3050.
Steam's SDL X11 and video-library probes pass. A fresh session with the
updated client and all four CPUs available to its web helpers still filled
shared memory to 98% and exited with `std::bad_alloc`, without creating a
Steam browser page. These diagnostics do not establish a working Frame UI.

October 3 paired IPC traces found that the parent's 500 ms connection reply
wait expired while the helper created its two 25 MiB buffers. A guarded guest
adaptation extends that wait to 30 seconds for this recovery build. A fresh
native Windows flat Frame frontend then rendered with shared memory around
100 MiB. Full VR frontend startup also stopped exhausting shared memory,
but the modern Frame dock, laser and curved panel still have no successful
full-session capture. See `vm/README.md` for the current VM launch settings.

Patch 0006 adds a bounded Windows ring wait. A live trace found an idle renderer
ring sleeping with queued commands; explicitly notifying it resumed Gamescope
initialization. The patch rechecks the real queue every 5 ms while idle and
retains actual command execution and completion reporting.

The OS tester's `-GamescopePreview` reproduces this focused rendering path with
Valve's loading animation. It retains the accepted GTK sizing and uses the
guarded upload adaptation only in that diagnostic mode.
The fresh automatic GTK capture passed; repeated host captures can still be
blank or displaced. Two direct 1920x1080 guest Gamescope captures confirm
animated content. Host presentation/capture is therefore still under investigation.

Builds, diagnostic logs, and black captures are saved under
`F:/Stearlight-GPU-Bridge-2026-09-30` and
`F:/Stearlight-Frame-Session-2026-09-30`.
