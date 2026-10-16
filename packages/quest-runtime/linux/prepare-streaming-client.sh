#!/bin/sh
set -eu
launcher=/opt/quest/streaming-client-launcher
[ -f "$launcher" ] || exit 0
tools=${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}/tools/vrlink/bin/linuxarm64
# Repair images staged by older updaters, which restored executable modes
# only below bin/ and left the VR launch scripts and query tool without
# executable modes. Valve's files here are executable entry points.
for entry in "$tools"/*; do
    [ ! -f "$entry" ] || [ -L "$entry" ] || chmod 755 "$entry"
done
for player in /opt/steam/steamrtarm64/streaming_client \
    "${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}/tools/vrlink/bin/linuxarm64/vrlink";do
[ -f "$player" ] || continue
if cmp -s "$player" "$launcher";then continue;fi
# Refresh the original after Valve updates the runtime, without accumulating
# patched copies. Verify ELF before replacing the launch entry.
[ "$(od -An -tx1 -N4 "$player" | tr -d ' \n')" = 7f454c46 ] || exit 1
# Updating our launcher must preserve the actual Valve executable.
if [ -f "$player.real" ] && grep -aq 'streaming launcher path' "$player";then
    [ "$(od -An -tx1 -N4 "$player.real" | tr -d ' \n')" = 7f454c46 ] || exit 1
else
    mv "$player" "$player.real"
fi
cp "$launcher" "$player.new"
chmod 755 "$player.new" "$player.real"
mv "$player.new" "$player"
done
