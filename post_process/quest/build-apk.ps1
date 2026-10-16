param(
    [Parameter(Mandatory)][string]$Payload,
    [Parameter(Mandatory)][string]$Output,
    [string]$Gradle='gradle'
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$payloadPath=(Resolve-Path -LiteralPath $Payload).Path
$outputPath=[System.IO.Path]::GetFullPath($Output)
foreach($file in @('assets/rootfs.list','assets/rootfs.sha256','assets/quest-overlay.bin','assets/boot/clips.json','jniLibs/arm64-v8a/libproot_exec.so')){
    if(!(Test-Path -LiteralPath (Join-Path $payloadPath $file))){throw "Incomplete Quest payload: $file"}
}
$parts=@(Get-Content -LiteralPath (Join-Path $payloadPath 'assets/rootfs.list'))
$prootBytes=[IO.File]::ReadAllBytes((Join-Path $payloadPath 'jniLibs/arm64-v8a/libproot_exec.so'))
if([Text.Encoding]::ASCII.GetString($prootBytes).IndexOf('PROOT_QUEST_ROBUST',[StringComparison]::Ordinal) -lt 0){throw 'This payload lacks the standalone PRoot adapter. Rebuild it with prepare-payload.py and --android-ndk.'}
if(!$parts.Count){throw 'Quest rootfs list is empty'}
for($index=0;$index -lt $parts.Count;$index++){
    $expected='rootfs/part-{0:D3}.bin' -f $index
    if($parts[$index] -ne $expected){throw "Invalid Quest rootfs part: $($parts[$index])"}
    $part=Join-Path $payloadPath "assets/$expected"
    if(!(Test-Path -LiteralPath $part) -or (Get-Item -LiteralPath $part).Length -eq 0){throw "Missing or empty Quest rootfs part: $expected"}
}
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
& $Gradle -p (Join-Path $repo 'packages/quest-runtime') `
    "-PquestPayload=$($payloadPath.Replace('\','/'))" `
    "-PquestBuildDir=$($outputPath.Replace('\','/'))" `
    --project-cache-dir (Join-Path $outputPath 'gradle-cache') `
    --no-daemon --console=plain :app:assembleDebug
if($LASTEXITCODE){throw "Quest APK build failed ($LASTEXITCODE)"}
$apk=Join-Path $outputPath 'outputs/apk/debug/app-debug.apk'
if(!(Test-Path -LiteralPath $apk)){throw 'Gradle did not produce the Quest APK'}
$digest=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$apk.sha256" -Value "$digest  app-debug.apk" -Encoding ascii
foreach($name in @('run-linux-debug.ps1','socket-table-monitor.sh','shell.ps1','shell.sh','usb-network.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path (Split-Path $apk) $name) -Force
}
Copy-Item -LiteralPath (Join-Path $repo 'packages/quest-runtime/README.md') -Destination (Join-Path (Split-Path $apk) 'README.md') -Force
Write-Output "Quest APK: $apk"
