# Steam Frame native dashboard sources

This directory is a source snapshot from the authorized Linux SteamVR dump at
`F:\StearlightBuildArchive\steamvr-linux\steamvr-linux`.

The `webinterface` tree is Valve's shipped `vrwebui_dashboard` bundle: the
original HTML, JavaScript chunks, CSS, fonts, and dashboard artwork. It is
kept unmodified so the native SteamVR dashboard can be loaded without
recreating its controls in a second UI implementation.

`laser_pointer` contains the shipped SteamVR laser model and pointer textures.
The native `vrdashboard` compositor owns the live laser using files from its
own SteamVR installation. This checked-in copy is retained for inspection;
the production SDL shell does not draw a competing cursor.

The VM uses the original Valve bundle from the SteamVR installation at
`/opt/steamvr/resources/webinterface/dashboard`. This checked-in copy is kept
for source inspection and runtime integration work; it is not duplicated
into the appliance image.

There is no local mock HTML page or `VRHTML` shim in this tree: those files
could render a preview but could not reproduce the host behavior supplied by
`vrserver` and `vrdashboard`.
