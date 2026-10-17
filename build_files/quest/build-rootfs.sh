#!/usr/bin/env bash
# Assemble APK inputs from the public recovery. No VM disk or host runtime.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
output=${1:?Usage: build-rootfs.sh OUTPUT_DIRECTORY [RECOVERY_RAW_IMAGE]}
mkdir -p "$output"
output=$(realpath "$output")
test ! -e "$output/frame-rootfs.tar.zst"
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
  python3 "$repo/build_files/fetch-recovery.py" "$repo/packages/sources.lock.json" "$raw"
fi
read -r offset length < <(python3 "$repo/build_files/fetch-recovery.py" partition "$raw")
mkdir "$work/frame" "$work/runtime" "$work/steam" "$work/root"
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
source "$repo/build_files/extract-gamescope-runtime.sh"
extract_frame_gamescope "$frame" "$session/gamescope"
tar --zstd -xf "$frame/usr/lib/steam/steam.tar.zst" -C "$work/steam" \
  ./steamrtarm64 ./linuxarm64 ./steam ./RUNSTEAM.sh ./steam.sh ./steam_msg.sh \
  ./bin ./public ./steamui ./clientui ./resource ./tenfoot ./friends ./controller_base \
  ./graphics ./legacycompat ./package ./fossilize_engine_filters.json \
  ./ThirdPartyLegalNotices.html ./ThirdPartyLegalNotices-Chromium.html \
  ./ThirdPartyLegalNotices.css ./ThirdPartyLegalNotices.doc ./steam_subscriber_agreement.txt
docker buildx build --platform linux/arm64 \
  --build-context "frame-runtime=$runtime" --build-context "steam-client=$work/steam" \
  --build-context "openvr-headers=$repo/packages/openvr/headers" \
  -f "$repo/Containerfile" --load -t stearlight-quest-rootfs:build "$repo"
container=$(docker create --platform linux/arm64 --entrypoint /bin/sh stearlight-quest-rootfs:build)
docker export "$container" | tar -xf - -C "$work/root"
docker rm "$container" >/dev/null
container=
python3 "$repo/build_files/stage-frame-desktop.py" --root "$frame" --output "$work/root/opt/stearlight-desktop"
python3 "$repo/build_files/quest/stage-chromium.py" --frame "$frame" \
  --desktop "$work/root/opt/stearlight-desktop" --output "$work/root/opt/stearlight-browser" --downloads "$work/downloads"
# These are APK build inputs, not a bootable VM image. Preserve Unix modes and
# aliases in a tar so the artifact transfer cannot drop executable bits.
tar --zstd -cf "$output/frame-rootfs.tar.zst" -C "$work/root" \
  --exclude='./dev/*' --exclude='./proc/*' --exclude='./sys/*' --exclude='./boot' \
  --exclude='./lib/modules' --exclude='./lib/firmware' .
chmod 644 "$output/frame-rootfs.tar.zst"
(cd "$output" && sha256sum frame-rootfs.tar.zst > SHA256SUMS)
echo "Quest build rootfs: $output/frame-rootfs.tar.zst"
