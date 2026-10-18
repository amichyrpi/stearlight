#!/bin/sh
set -eu
root=${STEARLIGHT_STEAM_ROOT:-/opt/steam}
quest=${STEARLIGHT_QUEST_ROOT:-/opt/quest}
launcher="$root/steamrtarm64/steamwebhelper.sh"
temporary=
trap '[ -z "$temporary" ] || rm -f "$temporary"' EXIT
# An update replaces Valve's launcher. Restore the platform adapter before
# the new client opens its browser, keeping Valve's current UI and binaries.
if ! cmp -s "$quest/steamwebhelper-launcher.sh" "$launcher";then
 temporary="$launcher.quest-next"
 cp "$quest/steamwebhelper-launcher.sh" "$temporary"
 chmod 755 "$temporary"
 mv "$temporary" "$launcher"
 temporary=
fi
if grep -Fq 'export LD_LIBRARY_PATH="${STEAMROOT}/${STEAM_RT_ARM64}"' "$root/RUNSTEAM.sh";then
 sed -i 's|export LD_LIBRARY_PATH="${STEAMROOT}/${STEAM_RT_ARM64}"|export LD_LIBRARY_PATH="${STEAMROOT}/${STEAM_RT_ARM64}/libs:${STEAMROOT}/${STEAM_RT_ARM64}"|' "$root/RUNSTEAM.sh"
fi
if ! grep -Fq '# STEARLIGHT_STEAM_CHANNEL' "$root/RUNSTEAM.sh";then
 temporary="$root/RUNSTEAM.sh.quest-next"
 awk 'BEGIN {done=0} /^STEAM_COMMAND=\($/ && !done {
  print "# STEARLIGHT_STEAM_CHANNEL";
  print ". /opt/quest/steam-channel-args.sh"; done=1
 } {print} END {if(!done)exit 1}' "$root/RUNSTEAM.sh" >"$temporary"
 chmod --reference="$root/RUNSTEAM.sh" "$temporary"
 mv "$temporary" "$root/RUNSTEAM.sh"
 temporary=
fi
/bin/sh "$quest/prepare-store-browser.sh"
/bin/sh "$quest/prepare-cef.sh" || true
/usr/bin/python3 "$quest/prepare-steam-input.py"
/bin/sh "$quest/frame-steam-ipc" || true
/bin/sh "$quest/prepare-streaming-client.sh"
/bin/sh "$quest/prepare-game-runtime.sh"
/usr/bin/python3 "$quest/steam_channels.py"
/usr/bin/python3 "$quest/prepare-desktop-ui.py" "$root"
