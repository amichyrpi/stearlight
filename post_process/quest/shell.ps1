[CmdletBinding()]
param([string]$Adb = 'adb', [string]$Command)
$ErrorActionPreference = 'Stop'
$package = 'net.stearlight.quest'
$installed = (& $Adb shell pm path $package).Trim()
if ($LASTEXITCODE -or -not $installed.StartsWith('package:/data/app/')) {
    throw 'Install Stearlight OS on the Quest first.'
}
$apk = $installed.Substring(8)
$native = $apk.Substring(0, $apk.LastIndexOf('/')) + '/lib/arm64'
$files = "/data/user/0/$package/files"
if ($native -notmatch '^/data/app/[A-Za-z0-9_./=+~-]+$') { throw 'Unexpected Android package path.' }
$entry = '/bin/bash /opt/quest/desktop-shell.sh'
if ($Command) {
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Command))
    $entry = "/usr/bin/python3 /opt/quest/adb-command.py $encoded"
}
$launch = "export LD_LIBRARY_PATH=$native PROOT_LOADER=$native/libproot_loader.so PROOT_TMP_DIR=$files/tmp PROOT_QUEST_ROBUST=1 GLIBC_TUNABLES=glibc.pthread.rseq=0; exec $native/libproot_exec.so --kill-on-exit --sysvipc --link2symlink -r $files/rootfs -b /dev -b /proc -b /sys -b ${files}/tmp:/tmp -b ${files}/shm:/dev/shm -b ${files}/state:/run/stearlight/quest -w /home/stearlight $entry"
if ($Command) {
    & $Adb shell "run-as $package /system/bin/sh -c '$launch'"
} else {
    & $Adb shell -t "run-as $package /system/bin/sh -c '$launch'"
}
exit $LASTEXITCODE
