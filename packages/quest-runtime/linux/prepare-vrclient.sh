#!/bin/sh
set -eu

binary=${STEARLIGHT_STEAMVR_ROOT:-/opt/steamvr}/bin/linuxarm64/vrclient.so
current=$(sha256sum "$binary" | cut -d ' ' -f 1)
case "$current" in
    6742092521ddcfd14cf32d72dbdc8a1ee8732bd1dbdaa9af84b810b92ab750a7) exit 0 ;;
    19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0)
        temporary="$binary.quest-cleanup"
        trap 'rm -f "$temporary"' EXIT
        cp -p "$binary" "$temporary"
        # Frame OS 0.4.5 has two copies of this cleanup loop. Both must
        # skip empty shared-resource entries before reading their fields.
        printf %s x6IRFA== | base64 -d | dd of="$temporary" bs=1 seek=1695360 conv=notrunc 2>/dev/null
        printf %s QgAAtVld7hdADEC5N13uFw== | base64 -d | dd of="$temporary" bs=1 seek=6318492 conv=notrunc 2>/dev/null
        printf %s a6IRFA== | base64 -d | dd of="$temporary" bs=1 seek=1695744 conv=notrunc 2>/dev/null
        printf %s QgAAtbVd7hdADEC5k13uFw== | base64 -d | dd of="$temporary" bs=1 seek=6318508 conv=notrunc 2>/dev/null
        printf %s vGlgAAAAAAC8aWAAAAAAAA== | base64 -d | dd of="$temporary" bs=1 seek=96 conv=notrunc 2>/dev/null
        [ "$(sha256sum "$temporary" | cut -d ' ' -f 1)" = 6742092521ddcfd14cf32d72dbdc8a1ee8732bd1dbdaa9af84b810b92ab750a7 ] || exit 1
        mv "$temporary" "$binary"
        echo 'STEARLIGHT QUEST BROWSER TEXTURE CLEANUP GUARD READY'
        exit 0 ;;
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
