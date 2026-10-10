#!/bin/sh
set -eu

steam_root=${STEARLIGHT_STEAM_ROOT:-/opt/steam}
library=$steam_root/steamui/library.js
bridge=${STEARLIGHT_QUEST_ROOT:-/opt/quest}/store-browser-bridge.js
# Use this installation's own library, keeping it paired with Valve's chunks.
# Reapplying an APK overlay must not restore an older Steam UI file.
temporary=
trap '[ -z "$temporary" ] || rm -f "$temporary"' EXIT
if ! grep -Fq '// STEARLIGHT_QUEST_STORE_ORIGIN_BRIDGE' "$library"; then
    temporary="$library.quest-store"
    cat "$bridge" > "$temporary"
    printf '\n' >> "$temporary"
    cat "$library" >> "$temporary"
    chmod --reference="$library" "$temporary"
    mv "$temporary" "$library"
fi
# Refresh our old prefix while retaining this installation's Valve UI.
if grep -Fxq '// STEARLIGHT_QUEST_UPDATE_CAPABILITIES' "$library" && ! grep -Fxq '// STEARLIGHT_QUEST_UPDATE_CAPABILITIES_V3' "$library";then
    temporary="$library.quest-update-remove"
    awk '/^\/\/ STEARLIGHT_QUEST_UPDATE_CAPABILITIES$/ {skip=1;next} skip && /^\}\)\(\);$/ {skip=0;next} !skip {print}' "$library" > "$temporary"
    chmod --reference="$library" "$temporary"
    mv "$temporary" "$library"
    temporary=
fi
if ! grep -Fxq '// STEARLIGHT_QUEST_UPDATE_CAPABILITIES_V3' "$library";then
    temporary="$library.quest-update"
    cat "${STEARLIGHT_QUEST_ROOT:-/opt/quest}/steam-update-bridge.js" > "$temporary"
    printf '\n' >> "$temporary"
    cat "$library" >> "$temporary"
    chmod --reference="$library" "$temporary"
    mv "$temporary" "$library"
fi
