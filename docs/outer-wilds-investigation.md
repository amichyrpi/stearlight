# Outer Wilds standalone investigation

**Status: Unsupported on Quest 2. Work stopped at the user's request.**
The experimental code is retained for contributors to continue. Keep the full
SteamVR interface; a separate lightweight game view was declined.

## Tested configuration

- Quest 2, Adreno 650, approximately 6 GB RAM, Android's 32-child-process limit unchanged.
- Steam AppID `753640`, build `20230391`, game `1.1.16.1372`, Unity `2019.4.39f1`.
- Valve Proton 11.0 (ARM64), ARM64EC FEX, DXVK 2.7.1, Mesa 26.1.6 with KGSL.
- Game window: 960 × 540, Low settings, a 30 FPS limit. Headset rendering settings are separate.

## Retained implementation

[`proton-game-launcher.py`](../packages/quest-runtime/linux/proton-game-launcher.py)
extends the existing Garden of Witches flat Unity path to Outer Wilds:

- A dedicated Gamescope OpenVR window, with Wine's SDL/HID gamepad support.
- Unity graphics jobs disabled and two job workers requested.
- DXVK instead of WineD3D, `MESA_VK_WSI_DEBUG=sw,noshm`, and
  `MESA_VK_ENABLE_SUBMIT_THREAD=true`. The last option was tested to avoid
  switching submit modes mid-session; it is not proof that the late hang is fixed.
- Two DXVK compiler threads, maximum frame latency of one, and a 30 FPS cap.
  The 512 MB DXGI memory values affect reporting, **not a hard allocation limit**.
  Explicit user DXVK settings are appended after these defaults.
- A checksum-pinned graphics asset adjustment: quarter-resolution texture mips,
  smaller shadow maps, and a 128 MB streaming-mip budget. The game's original
  asset has streaming mipmaps disabled, so that budget alone does not limit memory.
- One-time per-profile graphics defaults, backups, and a versioned marker.
  Gamma, field of view, aspect ratio and other required Unity JSON fields are
  supplied explicitly. Saves and subsequent settings changes are preserved.

The asset adapter recognizes the tested build and leaves unknown assets alone.
It changes `OuterWilds_Data/globalgamemanagers` at these offsets:

| Offset | Field | Original → experiment |
| --- | --- | --- |
| 248972 | Shadow resolution | 3 → 0 |
| 249016 | Texture quality | 0 → 2 |
| 249048 | Streaming mip budget, float | 512 → 128 MiB |

Original SHA-256:
`60af32f30f99e9e539a0bd19c1425f3ee3fb7c57172acbc1b556a6d0a81d6267`.
Quarter-mip SHA-256:
`778ac7dc5a1654b1f6708a57d582c533daf60357ea0e0888b1b4c4d209a98dbf`.

Graphics defaults apply to existing `SteamSaves/*/player.owsett` profiles.
A profile created during the first launch will need a later launch to receive
them. This is a remaining limitation for anyone continuing the implementation.

[`patch-mesa.py`](../build_files/quest/patch-mesa.py) retains four driver changes:

| Change | Evidence and limits |
| --- | --- |
| A6xx D32S8/discard depth workaround, upstream `a70d2af590db192f87b3af01f83a68b450edb4c3` | Removed the initial repeatable GPU hang near submission timestamp 9. A later GPU hang still occurred before the final KGSL changes. |
| Disable PresentWait2 capability for software X11 surfaces | The XPutImage path does not publish the presentation-completion IDs DXVK waits for. Without this change, presentation stalled after the first frames. Hardware X11 capability is retained. |
| Nonblocking KGSL timestamp probes, upstream `e984ef29` | A zero timeout must read the retired timestamp instead of issuing the kernel's indefinite-wait ioctl. Backported to 26.1's fd-based helper. |
| Correct timestamp/sync-file merging, upstream `1da50a1b` | Convert the aggregate timestamp before changing its state; do not treat an incoming FD as a timestamp. Backported without changing the older helper API. |

The existing shared-timeline, DMA allocation FD ownership and Zink image-import
fixes are retained. Upstream KGSL context is in
[Mesa MR 44838](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44838).

## Observed failures

WineD3D reached the menu with audio, but was slow and rendered its 3D scene
incorrectly. DXVK initially hung the GPU. After the depth and presentation
changes, the menu scene rendered and keyboard input could navigate it and
start loading a save.

One longer menu session later froze with audio continuing. The kernel recorded:

```text
GPU hang detected
OuterWilds.exe[29857]: gpu fault ctx 44 ... ts 64787 status 00E59005
```

The native submission stack was waiting in `kgsl_syncobj_wait`, through
`vk_common_WaitForFences` and the software-copy wait in
`wsi_common_queue_present`. Successful present calls earlier in the session
did not establish sustained stability. The final KGSL changes were compiled
and exercised, but a sufficiently long menu run has not proven this hang fixed.

Save loading reached 98%, then caused severe memory pressure. One load produced
a kernel OOM kill of `steamwebhelper`, followed by the full app closing. A later
PC-only monitor terminated just the game after sustained critical pressure,
before Android killed Stearlight again. Representative later samples:

| Game RSS | Game swap | System MemAvailable |
| --- | --- | --- |
| 847 MiB | 1153 MiB | 0 MiB |
| 461 MiB | 1594 MiB | 0 MiB |
| 574 MiB | 1655 MiB | 0 MiB |

At a captured loading point, unnamed mappings accounted for roughly 2 GB of
PSS plus swap. FEX's named JIT mapping was roughly 65 MB, so its code cache
was not the main memory consumer in that capture. These numbers do not include
all graphics allocations or establish an exact minimum RAM requirement.

Additional checks:

- An eighth-mip trial also failed to complete loading. It changed texture quality
  to 3 in the asset and graphics settings. Asset SHA-256:
  `784d5186a9b755ce17941d0726c6096e368e5c652f9d789388bdd57e4a82fab8`.
  This trial is documented rather than made the default.
- Critical Chromium memory-pressure notifications plus garbage collection
  reduced summed process RSS by about 270 MB in one measurement, but did not
  resolve loading. Summed RSS includes shared pages; this is not a measured
  amount of physical RAM reclaimed.
- A clean headset reboot and the updated driver still did not complete the load.
- The game's build lists graphics API `[2]` and its built-in game shaders only
  compiler platform `[4]`: Direct3D 11. A native Vulkan/OpenGL engine switch
  cannot be assumed to work with the shipped shader data.
- `sharedassets1.assets` contains about 898 MB of serialized Mesh objects.
  Roughly 728 MB belongs to readable meshes; their CPU vertex/index data totals
  about 285 MiB, with only about 27 MiB in names not containing `Collider`.
  The largest objects are collision geometry. No geometry or collision data
  was removed or rewritten.

Full gameplay, acceptable gameplay performance, and sustained stability were
**never verified**. Keep the compatibility rating Unsupported.

## Continue and validate

Use the normal [Quest build instructions](../README.md#build-the-quest-apk).
The driver must be rebuilt from the restored Mesa patcher; copying only the
Python launcher is insufficient. The prior driver build used X11 support
from the Zink/Turnip build, not a Turnip-only build with no X11 platform.

Run the normal checks from a Linux environment:

```sh
bash build_files/quest/check.sh
```

The launcher suite includes save/settings preservation, JSON default migration,
renderer selection and Garden/VTOL regression coverage. The following checks
compile the actual patched KGSL helper functions against mocked kernel/FD
operations; provide your extracted and patched Mesa source directory:

```sh
python3 build_files/quest/patch-mesa.py /path/to/mesa-26.1.6
python3 packages/quest-runtime/tests/mesa-kgsl-poll.test.py /path/to/mesa-26.1.6
python3 packages/quest-runtime/tests/mesa-kgsl-merge.test.py /path/to/mesa-26.1.6
```

The checks cover zero/expired polls, finite/infinite waits, interruptions,
timestamp wraparound, cross-queue merging, both timestamp/FD orders and FD
ownership. Compilation and these checks passed during the investigation;
they do not replace headset testing. The full normal Quest check suite also passed.

For another hardware attempt, record both kernel GPU faults and per-process
RSS/swap alongside system MemAvailable. A screenshot of a menu or an increasing
present counter is insufficient: confirm changing frames, input, save loading
and continued gameplay. Do not repeat a load that exhausts memory without a
material change. Keep licensed game assets and account logs out of the repo.

## Repository versus installed headset

The experimental source and tests are retained here. The user's headset was
returned to the previous working runtime and original game configuration when
testing stopped; saves were preserved. No lightweight game mode was implemented.

An earlier full test APK was built and signed locally but was not installed
because Android rejected the update for insufficient storage. That APK predates
the final KGSL changes and is not a release candidate. A future build must use
the current sources. No changes were pushed or released during this work.
