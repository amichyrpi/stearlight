param([string]$Adb='adb',[switch]$SkipRestart,[switch]$TimelineProbe)
$ErrorActionPreference='Stop'
$package='net.stearlight.quest'
function Run-Adb([string[]]$Arguments) {
    $result=& $Adb @Arguments
    if($LASTEXITCODE){throw "ADB failed: $($Arguments[0])"}
    return $result
}
# Developer validation path: run-as inherits adbd's syscall policy, allowing
# Linux robust futexes which Android's ordinary application policy blocks.
Run-Adb @('shell','run-as',$package,'mkdir','-p','files/state') | Out-Null
Run-Adb @('shell','run-as',$package,'touch','files/state/external-runtime') | Out-Null
if(!$SkipRestart){
 Run-Adb @('shell','run-as',$package,'rm','-f','files/state/external-runtime.ready') | Out-Null
 Run-Adb @('shell','am','force-stop',$package) | Out-Null
 # run-as processes started by adbd survive Activity force-stop. Remove only
 # this package's Linux runtime processes before starting another session.
 $appUid=(Run-Adb @('shell','run-as',$package,'id','-u')).Trim()
 foreach($line in (Run-Adb @('shell','ps','-A','-o','UID,PID,NAME'))){
  # CEF children can rename themselves to "exe" or thread names. This UID
  # belongs only to our package; force-stop has already closed its Activity.
  if($line -match '^\s*(\d+)\s+(\d+)\s+(\S+)\s*$' -and $Matches[1] -eq $appUid){
   $linuxPid=$Matches[2]
   # A tracer's death can remove its children before we reach their ps rows.
   Run-Adb @('shell',"run-as $package /system/bin/sh -c 'kill -KILL $linuxPid 2>/dev/null || true'") | Out-Null
  }
 }
 Run-Adb @('shell','am','start','-a','android.intent.action.MAIN','-c','com.oculus.intent.category.VR','-n',"$package/.QuestActivity") | Out-Null
}
$ready=$false
for($attempt=0;$attempt -lt 120;$attempt++) {
    & $Adb shell run-as $package test -f files/state/external-runtime.ready 2>$null
    if(!$LASTEXITCODE){$ready=$true;break}
    Start-Sleep -Milliseconds 500
}
if(!$ready){throw 'The app did not finish installing its Linux payload. Inspect StearlightQuest logcat.'}
$apk=(Run-Adb @('shell','pm','path',$package)).Trim()
if($apk -notmatch '^package:(/data/app/[A-Za-z0-9_~=/.-]+/base\.apk)$'){throw 'Unexpected APK installation path'}
$native=$Matches[1].Substring(0,$Matches[1].LastIndexOf('/'))+'/lib/arm64'
$files="/data/user/0/$package/files"
$command='/bin/bash /opt/quest/start-session.sh'
if($TimelineProbe){$command='/usr/bin/env --unset=LD_LIBRARY_PATH MESA_VK_ENABLE_SUBMIT_THREAD=1 /opt/quest/gpu-probe --timeline'}
# A forcibly closed host launcher cannot run its finally block. Its ADB-side
# helper survives and keeps spawning run-as processes. Replace only helpers
# for this package before starting a new one, including on debug restarts.
foreach($line in (Run-Adb @('shell','ps','-A','-o','PID,ARGS'))){
 if($line -match '^\s*(\d+)\s+sh\s+/data/local/tmp/stearlight-quest-net\.sh\s+net\.stearlight\.quest\s*$'){
  $staleMonitorPid=$Matches[1]
  Run-Adb @('shell',"case `$(cat /proc/$staleMonitorPid/cmdline 2>/dev/null) in *stearlight-quest-net.sh*net.stearlight.quest*) kill -TERM $staleMonitorPid 2>/dev/null || true;; esac") | Out-Null
 }
}
Run-Adb @('push',(Join-Path $PSScriptRoot 'socket-table-monitor.sh'),'/data/local/tmp/stearlight-quest-net.sh') | Out-Null
$monitorLog=Join-Path ([IO.Path]::GetTempPath()) "stearlight-quest-net-$PID.log"
$monitorError="$monitorLog.error"
$adbExecutable=(Get-Command $Adb -ErrorAction Stop).Source
$monitor=Start-Process -FilePath $adbExecutable -ArgumentList @('shell',"sh /data/local/tmp/stearlight-quest-net.sh $package") -WindowStyle Hidden -PassThru -RedirectStandardOutput $monitorLog -RedirectStandardError $monitorError
$remoteMonitorPid=$null
try {
 $networkReady=$false
 for($attempt=0;$attempt -lt 60;$attempt++){
  $monitorOutput=Get-Content -LiteralPath $monitorLog -ErrorAction SilentlyContinue
  foreach($line in $monitorOutput){if($line -match '^STEARLIGHT_NET_MONITOR_PID=(\d+)$'){$remoteMonitorPid=$Matches[1]}}
  if($monitorOutput -contains 'STEARLIGHT_NET_MONITOR_READY'){$networkReady=$true;break}
  if($monitor.HasExited){throw 'ADB socket-table monitor exited before it was ready'}
  Start-Sleep -Milliseconds 250
 }
 if(!$networkReady){throw 'ADB socket-table monitor did not become ready'}
# adbd permits PRoot's seccomp syscall acceleration. The ordinary Android app
# launcher has a different syscall policy and cannot use this setting.
$launch="unset PROOT_NO_SECCOMP; export LD_LIBRARY_PATH=$native PROOT_LOADER=$native/libproot_loader.so PROOT_TMP_DIR=$files/tmp GLIBC_TUNABLES=glibc.pthread.rseq=0; exec $native/libproot_exec.so --kill-on-exit --sysvipc --link2symlink -r $files/rootfs -b /dev -b /proc -b /sys -b ${files}/tmp:/tmp -b ${files}/shm:/dev/shm -b ${files}/state:/run/stearlight/quest -b ${files}/state/proc-net:/proc/net -w /home/stearlight $command > $files/logs/linux.log 2>&1"
Write-Host 'Starting the developer Linux runtime. Logs: adb shell run-as net.stearlight.quest tail -n 40 files/logs/linux.log'
& $Adb shell "run-as $package /system/bin/sh -c '$launch'"
if($LASTEXITCODE){throw "Linux runtime exited with code $LASTEXITCODE"}
} finally {
 if($remoteMonitorPid){
  # Verify the command before killing a pid which Android may have reused.
  Run-Adb @('shell',"case `$(cat /proc/$remoteMonitorPid/cmdline 2>/dev/null) in *stearlight-quest-net.sh*) kill -TERM $remoteMonitorPid 2>/dev/null || true;; esac") | Out-Null
 }
 if(!$monitor.HasExited){Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue}
}
