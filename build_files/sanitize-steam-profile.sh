#!/usr/bin/env bash
set -euo pipefail

# Remove account/session state from a build-stage Steam tree.  The client
# payload, branch metadata and SteamAppData.vdf are intentionally retained;
# Steam will create fresh account data and present its own first-run setup on
# the target device.
steam_root=${1:?usage: sanitize-steam-profile.sh /path/to/Steam}
test -d "$steam_root"

rm -f "$steam_root/config/loginusers.vdf" \
      "$steam_root/config/localconfig.vdf" \
      "$steam_root/config/sharedconfig.vdf" \
      "$steam_root/config/registeruserconfig.vdf" \
      "$steam_root/config/configsetttings.vdf"
rm -rf "$steam_root/userdata"
find "$steam_root" -maxdepth 1 -type f -name 'ssfn*' -delete 2>/dev/null || true
