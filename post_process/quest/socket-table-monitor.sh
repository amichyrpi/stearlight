#!/system/bin/sh
# ADB developer path only. Android hides /proc/net from application uids.
# Publish actual kernel socket rows owned by this package for Steam's lsof
# peer verification. No pid, uid, inode, address, or port is invented.
set -eu
package=$1
case "$package" in net.stearlight.quest) ;; *) exit 2 ;; esac
task_uid=$(run-as "$package" id -u)
task_monitor=$$
case "$task_uid" in ''|*[!0-9]*) exit 2 ;; esac
run-as "$package" mkdir -p files/state/proc-net
echo "STEARLIGHT_NET_MONITOR_PID=$$"
trap 'for table in tcp tcp6 udp udp6; do run-as "$package" rm -f "files/state/proc-net/$table.$task_monitor.tmp"; done' EXIT
trap 'exit 0' HUP INT TERM
ready=0
while true; do
 for table in tcp tcp6 udp udp6; do
  awk -v task_uid="$task_uid" 'NR == 1 || $8 == task_uid' "/proc/net/$table" |
   run-as "$package" /system/bin/sh -c "umask 077; cat > files/state/proc-net/$table.$task_monitor.tmp; mv files/state/proc-net/$table.$task_monitor.tmp files/state/proc-net/$table"
 done
 if [ "$ready" = 0 ]; then echo STEARLIGHT_NET_MONITOR_READY; ready=1; fi
 sleep .05
done
