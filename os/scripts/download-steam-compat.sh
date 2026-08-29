#!/usr/bin/env bash
set -euo pipefail

destination=${1:?usage: download-steam-compat.sh OUTPUT_DIRECTORY}
fex_branch=${STEARLIGHT_FEX_BRANCH:-beta}
proton_branch=${STEARLIGHT_PROTON_BRANCH:-public}

: "${STEAM_USERNAME:?Set STEAM_USERNAME for the Steam depot subscription.}"
: "${STEAM_PASSWORD:?Set STEAM_PASSWORD. GitHub Actions should use an encrypted secret.}"

mkdir -p "$destination"
destination=$(CDPATH= cd -- "$destination" && pwd)
rm -rf "$destination/fex" "$destination/proton11" \
       "$destination/appmanifest_3127680.acf" \
       "$destination/appmanifest_4628740.acf"

docker run --rm \
  -e STEAM_USERNAME -e STEAM_PASSWORD \
  -e FEX_BRANCH="$fex_branch" -e PROTON_BRANCH="$proton_branch" \
  -v "$destination:/tools" steamcmd/steamcmd:latest bash -euc '
    steamcmd \
      +@ShutdownOnFailedCommand 1 +@NoPromptForPassword 1 \
      +force_install_dir /tools/fex \
      +login "$STEAM_USERNAME" "$STEAM_PASSWORD" \
      +app_update 3127680 -beta "$FEX_BRANCH" validate \
      +force_install_dir /tools/proton11 \
      +app_update 4628740 -beta "$PROTON_BRANCH" validate \
      +quit

    for app_id in 3127680 4628740; do
      manifest=$(find /home/steam /root /tools \
        -name "appmanifest_${app_id}.acf" -print -quit 2>/dev/null || true)
      test -n "$manifest"
      cp "$manifest" "/tools/appmanifest_${app_id}.acf"
    done
  '

test -s "$destination/appmanifest_3127680.acf"
test -s "$destination/appmanifest_4628740.acf"
test -n "$(find "$destination/fex" -mindepth 1 -print -quit)"
test -n "$(find "$destination/proton11" -mindepth 1 -print -quit)"
