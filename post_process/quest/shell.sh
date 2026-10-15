#!/bin/sh
set -eu
adb=${ADB:-adb}
package=net.stearlight.quest
installed=$("$adb" shell pm path "$package" | tr -d '\r')
case "$installed" in package:/data/app/*) ;; *) echo 'Install Stearlight OS first.' >&2; exit 1 ;; esac
apk=${installed#package:}
native=${apk%/*}/lib/arm64
case "$native" in *\'*|*\"*) echo 'Unexpected Android package path.' >&2; exit 1 ;; esac
files=/data/user/0/$package/files
entry='/bin/bash /opt/quest/desktop-shell.sh'
if [ "$#" -gt 0 ];then
 encoded=$(printf '%s' "$1" | base64 | tr -d '\n')
 entry="/usr/bin/python3 /opt/quest/adb-command.py $encoded"
fi
launch="export LD_LIBRARY_PATH=$native PROOT_LOADER=$native/libproot_loader.so PROOT_TMP_DIR=$files/tmp PROOT_QUEST_ROBUST=1 GLIBC_TUNABLES=glibc.pthread.rseq=0; exec $native/libproot_exec.so --kill-on-exit --sysvipc --link2symlink -r $files/rootfs -b /dev -b /proc -b /sys -b $files/tmp:/tmp -b $files/shm:/dev/shm -b $files/state:/run/stearlight/quest -w /home/stearlight $entry"
if [ "$#" -gt 0 ];then
 exec "$adb" shell "run-as $package /system/bin/sh -c '$launch'"
fi
exec "$adb" shell -t "run-as $package /system/bin/sh -c '$launch'"
