#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
for file in Containerfile Justfile build_files/build-image.sh build_files/check.sh build_files/fetch-recovery.py packages/sources.lock.json packages/stearlight-runtime/dashboard_ready.c packages/windows-runtime/build-runtime.sh disk_config/disk.json post_process/start-vm.ps1 post_process/start-vm.cmd post_process/package-windows.ps1 system_files/etc/local.d/arm64-vm.start tests/launcher-test.ps1; do
  test -s "$repo/$file" || { echo "Missing OS input: $file" >&2; exit 1; }
done
for legacy in vm assets cmake common docker gui lib pipe protocol scripts steamvr-driver tools utility CMakeLists.txt; do
  test ! -e "$repo/$legacy" || { echo "Legacy project path remains: $legacy" >&2; exit 1; }
done
if grep -nE 'Stearlight-.*20[0-9][0-9]|[FG]:[/\\]|Users[/\\]jfran' "$repo/post_process/start-vm.ps1" "$repo/post_process/start-vm.cmd" "$repo/build_files/build-image.sh" "$repo/packages/windows-runtime/build-runtime.sh" "$repo/.github/workflows/build.yml"; then
  echo 'Portable OS build contains developer host configuration' >&2
  exit 1
fi
python3 - "$repo" <<'PY'
import json, pathlib, shlex, sys
root = pathlib.Path(sys.argv[1])
disk = json.loads((root / 'disk_config/disk.json').read_text())
assert isinstance(disk['rootfs_size_gib'], int) and disk['rootfs_size_gib'] >= 4
for line in (root / 'Containerfile').read_text().splitlines():
    if not line.startswith('COPY ') or '--from=' in line:
        continue
    for source in shlex.split(line)[1:-1]:
        assert (root / source).exists(), f'Missing container input: {source}'
PY
echo 'Stearlight OS layout and container inputs passed.'
