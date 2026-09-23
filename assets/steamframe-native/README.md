# Steam Frame native dashboard sources

This directory is a source snapshot from the authorized Linux SteamVR dump at
`F:\StearlightBuildArchive\steamvr-linux\steamvr-linux`.

The `webinterface` tree is Valve's shipped `vrwebui_dashboard` bundle: the
original HTML, JavaScript chunks, CSS, fonts, and dashboard artwork. It is
kept unmodified so the native SteamVR dashboard can be loaded without
recreating its controls in a second UI implementation.

`laser_pointer` contains the shipped SteamVR laser model and pointer textures.
The native `vrdashboard` compositor owns the live laser; the files remain in
the source tree for Valve's runtime and the deterministic visual test only.
The production SDL shell does not draw a competing cursor.

The bundle is installed at:

`/usr/share/svrt/assets/steamframe-native/webinterface/dashboard`

It is intentionally separate from the SDL page-surface bridge. The Valve
runtime is the only production dashboard implementation; the visual harness
is not a replacement for `vrserver`/`vrdashboard`.
