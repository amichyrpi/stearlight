#!/bin/sh
set -eu

binary=${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}/bin/linuxarm64/vrclient.so
current=$(sha256sum "$binary" | cut -d ' ' -f 1)
case "$current" in
    4eed284d0aa7322d56fc0cd8ed0530d8b6e7310934490b74d0529ba7043945b3) exit 0 ;;
    fe967dcc0d97dab984bd9acc1dc522fcecbcf9beb794fdd7ec156334ec31e313) ;;
    *) echo 'Quest texture cleanup guard does not match this SteamVR build; leaving it unchanged.' >&2; exit 1 ;;
esac

# This build dereferences empty entries while releasing shared GL/Vulkan
# resources after browser texture replacement. Branch to a null check before
# reading the GL semaphore field. Non-null entries execute the original load
# and resume the original cleanup; null entries advance the existing iterator.
# The four-instruction stub uses verified zero padding after the RX segment.
# Extend its file/memory bounds by 18 bytes, within the existing alignment gap.
# File size, data segments, stack, registers and synchronization stay intact.
temporary="$binary.quest-cleanup"
trap 'rm -f "$temporary"' EXIT
cp -p "$binary" "$temporary"
printf %s rIYRFA== | base64 -d |
    dd of="$temporary" bs=1 seek=1680040 conv=notrunc 2>/dev/null
printf %s QgAAtXR57hdADEC5UnnuFw== | base64 -d |
    dd of="$temporary" bs=1 seek=6274392 conv=notrunc 2>/dev/null
printf %s aL1fAAAAAABovV8AAAAAAA== | base64 -d |
    dd of="$temporary" bs=1 seek=96 conv=notrunc 2>/dev/null
if [ "$(sha256sum "$temporary" | cut -d ' ' -f 1)" != \
     4eed284d0aa7322d56fc0cd8ed0530d8b6e7310934490b74d0529ba7043945b3 ]; then
    echo 'Quest texture cleanup guard failed validation.' >&2
    exit 1
fi
mv "$temporary" "$binary"
echo 'STEARLIGHT QUEST BROWSER TEXTURE CLEANUP GUARD READY'
