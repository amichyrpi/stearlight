#!/usr/bin/env bash
set -euo pipefail
# Ubuntu: docker/buildx, python3, pax-utils, btrfs-progs, zstd, e2fsprogs,
# qemu-utils. Run as root to mount the read-only recovery partition.
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:?Usage: build-image.sh OUTPUT_DIRECTORY [RECOVERY_RAW_IMAGE]}
mkdir -p "$output"
output=$(realpath "$output")
work=$(mktemp -d)
loop=
container=
cleanup() {
  if mountpoint -q "$work/frame"; then umount "$work/frame"; fi
  if [ -n "$loop" ]; then losetup -d "$loop"; fi
  if [ -n "$container" ]; then docker rm "$container" >/dev/null; fi
  rm -rf "$work"
}
trap cleanup EXIT
raw=${2:-$work/recovery.raw}
if [ "$#" -lt 2 ]; then
  python3 "$repo/vm/fetch-recovery.py" "$repo/vm/sources.lock.json" "$raw"
fi
read -r offset length < <(python3 "$repo/vm/fetch-recovery.py" partition "$raw")
mkdir "$work/frame" "$work/runtime" "$work/steam"
loop=$(losetup --find --show --read-only --offset "$offset" --sizelimit "$length" "$raw")
mount -t btrfs -o ro,subvolid=5 "$loop" "$work/frame"
frame="$work/frame"
runtime="$work/runtime"
for component in bin/linuxarm64 bin/vrwebhelper/linuxarm64 bin/vrenv.sh resources content \
    drivers/frame_controller tools steamxr_linuxarm64.json \
    third_party_legal_notices.txt third_party_legal_notices_web.txt \
    drivers/frame_hmd/driver.vrdrivermanifest \
    drivers/frame_hmd/resources/frame_hmd_additional.vrsettings drivers/frame_hmd/resources/input; do
  test -e "$frame/opt/steamvr/$component"
  mkdir -p "$runtime/$(dirname "$component")"
  cp -a "$frame/opt/steamvr/$component" "$runtime/$(dirname "$component")/"
done
if [ -f "$frame/opt/steamvr/.disable-steamvr-health-check" ]; then
  cp -a "$frame/opt/steamvr/.disable-steamvr-health-check" "$runtime/"
fi
session="$runtime/resources/stearlight-session"
mkdir -p "$session"
cp -a "$frame/usr/share/steamos/steamos-cursor.png" "$frame/usr/share/steamos/steam_icon.png" "$session/"
source "$repo/vm/os/extract-gamescope-runtime.sh"
extract_frame_gamescope "$frame" "$session/gamescope"
tar --zstd -xf "$frame/usr/lib/steam/steam.tar.zst" -C "$work/steam" \
  ./steamrtarm64 ./linuxarm64 ./steam ./RUNSTEAM.sh ./steam.sh ./steam_msg.sh \
  ./bin ./public ./steamui ./clientui ./resource ./tenfoot ./friends ./controller_base \
  ./graphics ./legacycompat ./package ./fossilize_engine_filters.json \
  ./ThirdPartyLegalNotices.html ./ThirdPartyLegalNotices-Chromium.html \
  ./ThirdPartyLegalNotices.css ./ThirdPartyLegalNotices.doc ./steam_subscriber_agreement.txt
umount "$frame"
losetup -d "$loop"
loop=
if [ "$#" -lt 2 ]; then rm "$raw"; fi
docker buildx build --platform linux/arm64 \
  --build-context "frame-runtime=$runtime" --build-context "steam-client=$work/steam" \
  --build-context "openvr-headers=$repo/openvr/headers" \
  -f "$repo/vm/Dockerfile.arm64" --load -t stearlight-arm64-frame-vm:build "$repo"
container=$(docker create --platform linux/arm64 --entrypoint /bin/sh stearlight-arm64-frame-vm:build)
mkdir "$work/rootfs"
docker export "$container" | tar -xf - -C "$work/rootfs"
docker rm "$container" >/dev/null
container=
cp "$work/rootfs/boot/vmlinuz-lts" "$work/rootfs/boot/initramfs-lts" "$output/"
# Preserve device nodes, ownership and executable bits. mkfs populates a
# sparse 8 GiB disk without a second writable mount or an 8 GiB download.
truncate -s 8G "$work/rootfs.ext4"
mke2fs -q -F -t ext4 -O ^has_journal,^orphan_file -E lazy_itable_init=0 \
  -d "$work/rootfs" "$work/rootfs.ext4"
e2fsck -fn "$work/rootfs.ext4"
qemu-img convert -f raw -O qcow2 -c "$work/rootfs.ext4" "$output/stearlight-arm64.qcow2"
qemu-img check "$output/stearlight-arm64.qcow2"
cp "$repo/vm/start-vm.ps1" "$repo/vm/start-vm.cmd" "$repo/vm/README.md" "$repo/vm/sources.lock.json" "$output/"
mkdir -p "$output/licenses"
cp "$work/steam/steam_subscriber_agreement.txt" "$work/steam/ThirdPartyLegalNotices"* "$output/licenses/"
cp "$runtime/third_party_legal_notices"* "$output/licenses/"
(
  cd "$output"
  sha256sum stearlight-arm64.qcow2 vmlinuz-lts initramfs-lts > SHA256SUMS
)
echo "Built fresh ARM64 Frame VM: $output/stearlight-arm64.qcow2"
