#!/bin/sh
set -eu

binary=${STEARLIGHT_STEAM_ROOT:-/opt/steam}/steamrtarm64/libcef.so
current=$(sha256sum "$binary" | cut -d ' ' -f 1)
case "$current" in
    923dffdb20868c4253f276257856c9e84a3ca8095d839505a77f9e9d320022b7) exit 0 ;;
    891d85f97231fcafa5763b7140039c5d3cbbb99f2a42ede08de5991abcbc3cdb) ;;
    *) echo 'Quest udev guard does not match this CEF build; leaving it unchanged.' >&2; exit 1 ;;
esac

# Android denies udev's netlink monitor. This verified CEF calls
# UdevWatcher::EnumerateExistingDevices with a null watcher and crashes.
# Preserve the function's non-null path and return through its epilogue on null.
# Its x28 save/restore is redundant: x28 is never modified in this function.
# Move the existing vector initializer into that save slot, add cbz x0, and
# remove the redundant restore. PAC and all modified callee-saved registers stay.
temporary="$binary.quest-udev"
trap 'rm -f "$temporary"' EXIT
cp -p "$binary" "$temporary"
printf %s QOUFTw== | base64 -d |
    dd of="$temporary" bs=1 seek=126008568 conv=notrunc 2>/dev/null
printf %s wAwAtA== | base64 -d |
    dd of="$temporary" bs=1 seek=126008584 conv=notrunc 2>/dev/null
printf %s HyAD1Q== | base64 -d |
    dd of="$temporary" bs=1 seek=126008996 conv=notrunc 2>/dev/null
if [ "$(sha256sum "$temporary" | cut -d ' ' -f 1)" != \
     923dffdb20868c4253f276257856c9e84a3ca8095d839505a77f9e9d320022b7 ]; then
    echo 'Quest CEF udev guard failed validation.' >&2
    exit 1
fi
mv "$temporary" "$binary"
echo 'STEARLIGHT QUEST CEF UDEV NULL GUARD READY'
