# Quest performance check — 2026-10-13

Quest 2, SteamVR 2.18.2, Celeste 1.4.0.0. The scene uses the 75% profile
and requests 72 Hz; local 2D game panels use 960x540 at a target of 60 Hz.

The unmodified FNA3D dynamic-buffer search reached 300 slots while 299 were
already free. It searched only after the current slot when the parent buffer
was bound. The repair starts that search at zero, preserving the busy-slot
check and NOOVERWRITE behavior. It also retains data when NONE selects a
different slot.

Twenty measurements over 9.6 minutes at Celeste's idle title screen:

| Measurement | Result |
| --- | --- |
| Total game KGSL allocation, minimum and maximum | 1,072,533,504 bytes |
| Largest dynamic buffer's slot count | 8, constant |
| FNA buffers / images / live regions | 23 / 919 / 942, constant |
| Game resident memory, first → last | 717,700 → 696,524 KiB |
| Game swap | 0 throughout |

Both eye captures show the title screen in passthrough. The user previously
confirmed stable picture and audio. They could not play during this check;
this is an idle-menu regression check, not a gameplay or flight benchmark.
Continuous 72 FPS and smoother dashboard movement remain unverified.

VTOL VR also launched locally after Steam updated its ARM64 runtime. OpenVR
reported valid headset and both controller poses, and the user confirmed its
room/menu was visible and movement felt smoother. The Linux compositor
reported 47.0–68.8 FPS in sampled ten-second intervals. This is the compositor's
output rate, not VTOL's gameplay rate. Flight performance remains unverified.
The game still places substantial pressure on the Quest's memory during asset
loading; it does not establish that VTOL can sustain 72 FPS.

Nine measurements over 4.1 minutes after loading kept VTOL's combined resident
and swapped memory at 2.16–2.18 GiB. GPU allocation settled at about 0.96 GiB.
The last samples still contained about 1.47 GiB of game swap, so memory pressure
remains a performance constraint even though this check did not show Celeste's
continuous buffer growth.

The final full APK was rebuilt, installed, and launched through its normal
Android startup. The dashboard returned, and Celeste reached its title screen
in both eyes with passthrough. The installed game library matches the bundled
repair's checksum; its GPU allocation again measured 1,072,533,504 bytes and
game swap remained zero.

Validation includes 23 Python tests and the concurrent 100,000-sample tracking
publication test. The FNA test compiles and runs the actual pinned upstream
SetBufferData function with controlled slots, checking completed-slot reuse,
busy-slot preservation, NOOVERWRITE, and copying prior data for NONE.
