#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
for file in build_files/*.sh packages/windows-runtime/*.sh packages/windows-runtime/windows-venus/*.sh tests/*.sh; do bash -n "$file"; done
while IFS= read -r file; do
  if head -n 1 "$file" | grep -q '^#!.*sh'; then bash -n "$file"; fi
done < <(find system_files/usr system_files/etc/init.d system_files/etc/local.d -type f)
python3 tests/recovery-test.py
sh tests/layout-test.sh
sh tests/steamvr-native-test.sh
sh tests/frame-browser-test.sh
sh tests/xr-bridge-env-test.sh
sh tests/xr-lifecycle-test.sh
