#!/bin/sh
set -eu

steam_root=${STEARLIGHT_STEAM_ROOT:-/opt/steam}
library=$steam_root/steamui/library.js
bridge=${STEARLIGHT_QUEST_ROOT:-/opt/quest}/store-browser-bridge.js
# Use this installation's own library, keeping it paired with Valve's chunks.
# Reapplying an APK overlay must not restore an older Steam UI file.
temporary=
trap '[ -z "$temporary" ] || rm -f "$temporary"' EXIT
if ! head -n 1 "$library" | grep -qx '// STEARLIGHT_QUEST_STORE_ORIGIN_BRIDGE'; then
    temporary="$library.quest-store"
    cat "$bridge" > "$temporary"
    printf '\n' >> "$temporary"
    cat "$library" >> "$temporary"
    chmod --reference="$library" "$temporary"
    mv "$temporary" "$library"
fi
