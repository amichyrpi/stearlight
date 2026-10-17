#!/bin/bash
set -eu
directory=/opt/quest/desktop-cli
[ -d "$directory" ] || exit 0
state=/home/stearlight/.local/state/stearlight-desktop
mkdir -p "$state"
# Install the signed, pinned packages once; leave an existing toolset alone.
if [ ! -f "$state/cli-v1" ];then
 if ! command -v git >/dev/null || ! command -v curl >/dev/null || ! command -v nano >/dev/null;then
  /sbin/apk --no-network --no-scripts add --usermode "$directory/"*.apk
 fi
 touch "$state/cli-v1"
fi
