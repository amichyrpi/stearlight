#!/usr/bin/env bash
# Prepare trusted build inputs for an unprivileged payload build.
set -euo pipefail
[ "$#" -eq 2 ] || { echo 'Usage: extract-rootfs.sh ARCHIVE NEW_DIRECTORY' >&2; exit 2; }
archive=$(realpath "$1")
root=$2
mkdir -p "$root"
root=$(realpath "$root")
if [ -n "$(find "$root" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo 'Rootfs destination must be empty.' >&2
  exit 1
fi
tar --zstd --no-same-owner --delay-directory-restore -xf "$archive" -C "$root"
# BusyBox's bbsuid can be execute-only. The next build stage must read/copy
# it and patch runtime files without root. Preserve all existing execute bits.
# Recursive chmod does not follow the rootfs's symlinks into the host.
chmod -R u+rwX "$root"
