#!/usr/bin/env bash
set -euo pipefail
# MSYS2 UCRT64 builds the Windows host; the guest remains ARM64.
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=${1:?Usage: build-runtime.sh BUILD_DIRECTORY OUTPUT_DIRECTORY}
output=${2:?Usage: build-runtime.sh BUILD_DIRECTORY OUTPUT_DIRECTORY}
mkdir -p "$work" "$output"
work=$(realpath "$work")
output=$(realpath "$output")
read -r qemu_repo qemu_commit < <(python3 -c 'import json,sys; s=json.load(open(sys.argv[1]))["qemu"]; print(s["repository"],s["commit"])' "$repo/vm/sources.lock.json" | tr -d '\r')
read -r renderer_repo renderer_commit < <(python3 -c 'import json,sys; s=json.load(open(sys.argv[1])); print(s["repository"],s["commit"])' "$repo/vm/host/windows-venus/sources.lock.json" | tr -d '\r')
checkout() {
  local url=$1 commit=$2 directory=$3
  git init "$directory"
  git -C "$directory" fetch --depth=1 "$url" "$commit"
  git -C "$directory" checkout --detach FETCH_HEAD
  test "$(git -C "$directory" rev-parse HEAD)" = "$commit"
}
checkout "$renderer_repo" "$renderer_commit" "$work/renderer"
python3 - "$repo/vm/host/windows-venus" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
for patch in json.loads((root / 'sources.lock.json').read_text())['patches']:
    assert hashlib.sha256((root / patch['file']).read_bytes()).hexdigest() == patch['sha256'], patch['file']
PY
for patch in "$repo"/vm/host/windows-venus/patches/*.patch; do
  git -C "$work/renderer" apply "$patch"
done
meson setup "$work/renderer-build" "$work/renderer" --buildtype=release \
  --prefix=/ucrt64 -Dvenus=true -Dvideo=true -Dtests=false
meson compile -C "$work/renderer-build" -j "${BUILD_JOBS:-4}"
meson install -C "$work/renderer-build"
checkout "$qemu_repo" "$qemu_commit" "$work/qemu"
# configure obtains its pinned Meson wrap dependencies. The VM boots a
# supplied kernel and needs none of QEMU's unrelated ROM build submodules.
for patch in "$repo"/vm/host/qemu/patches/*.patch; do git -C "$work/qemu" apply "$patch"; done
mkdir "$work/qemu-build"
(
  cd "$work/qemu-build"
  "$work/qemu/configure" --target-list=aarch64-softmmu --prefix=/ucrt64 \
    --enable-gtk --enable-opengl --enable-virglrenderer --enable-slirp \
    --disable-docs --disable-plugins --disable-werror --disable-guest-agent
  meson configure -Dbuildtype=release -Doptimization=3 -Ddebug=false -Db_pie=false -Dqom_cast_debug=false
  ninja -j "${BUILD_JOBS:-4}" qemu-system-aarch64.exe qemu-img.exe
)
cp "$work/qemu-build/qemu-system-aarch64.exe" "$work/qemu-build/qemu-img.exe" "$output/"
cp /ucrt64/bin/libvirglrenderer-1.dll /ucrt64/bin/vulkan-1.dll "$output/"
# ldd resolves the full DLL closure against the build shell's runtime.
for binary in "$output"/*.exe "$output"/*.dll; do
  while read -r dependency; do cp "$dependency" "$output/"; done < <(
    ldd "$binary" | awk '$2 == "=>" && $3 ~ /^\/ucrt64\/bin\// {print $3}')
done
if ldd "$output/qemu-system-aarch64.exe" "$output/qemu-img.exe" | grep -q 'not found'; then
  echo 'Unresolved Windows runtime DLL' >&2; exit 1
fi
mkdir -p "$output/share" "$output/licenses" "$output/sources"
cp -a "$work/qemu/pc-bios/." "$output/share/"
for resource in glib-2.0 gtk-3.0 icons themes; do
  cp -a "/ucrt64/share/$resource" "$output/share/"
done
cp -a /ucrt64/share/licenses/. "$output/licenses/"
cp "$work/qemu/COPYING" "$output/licenses/QEMU-COPYING"
cp "$work/renderer/COPYING" "$output/licenses/virglrenderer-COPYING"
pacman -Q > "$output/sources/msys2-packages.txt"
tar --exclude=.git -C "$work" -cf - qemu renderer | zstd -T2 -6 -o "$output/sources/windows-host.tar.zst"
cp "$repo/vm/host/build-runtime.sh" "$repo/vm/sources.lock.json" "$output/sources/"
cp -a "$repo/vm/host/windows-venus" "$repo/vm/host/qemu" "$output/sources/"
"$output/qemu-system-aarch64.exe" --version
"$output/qemu-system-aarch64.exe" -display help | grep -x gtk
"$output/qemu-system-aarch64.exe" -device virtio-gpu-gl-pci,help | grep venus
echo "Built portable Windows QEMU runtime: $output"
