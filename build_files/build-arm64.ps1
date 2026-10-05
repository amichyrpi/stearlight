[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$RecoveryRawImage,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [string]$WslDistribution = 'docker-desktop',
    [string]$ZstdExecutable
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$diskSize = (Get-Content -LiteralPath (Join-Path $repo 'disk_config/disk.json') -Raw | ConvertFrom-Json).rootfs_size_gib
$output = [IO.Path]::GetFullPath($OutputDirectory)
$recovery = (Resolve-Path -LiteralPath $RecoveryRawImage).Path
foreach ($path in @($output, $recovery)) {
    if ($path -notmatch '^[FG]:\\' -or $path.Contains("'")) {
        throw "ARM64 VM inputs and outputs must use F: or G: without apostrophes: $path"
    }
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$runtime = Join-Path $output 'frame-runtime'
$runtimeMarker = Join-Path $runtime 'bin\linuxarm64\vrcompositor'
$steamArchive = Join-Path $output 'steam.tar.zst'
if (-not (Test-Path -LiteralPath $runtimeMarker)) {
    if (Test-Path -LiteralPath $runtime) {
        throw "Incomplete runtime directory; use a fresh output folder: $runtime"
    }
    & (Join-Path $PSScriptRoot 'extract-frame-runtime.ps1') `
        -RecoveryRawImage $recovery -DestinationDirectory $runtime `
        -WslDistribution $WslDistribution
    if ($LASTEXITCODE -ne 0) { throw 'Steam Frame runtime extraction failed.' }
}
if (-not (Test-Path -LiteralPath $steamArchive)) {
    & (Join-Path $PSScriptRoot 'extract-frame-runtime.ps1') `
        -RecoveryRawImage $recovery -DestinationDirectory $runtime `
        -WslDistribution $WslDistribution -SteamArchiveOnly
    if ($LASTEXITCODE -ne 0) { throw 'Steam Frame client archive extraction failed.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $runtime 'resources\stearlight-session\steamos-cursor.png')) -or
    -not (Test-Path -LiteralPath (Join-Path $runtime 'resources\stearlight-session\steam_icon.png')) -or
    -not (Test-Path -LiteralPath (Join-Path $runtime 'drivers\frame_hmd\resources\frame_hmd_additional.vrsettings')) -or
    -not (Test-Path -LiteralPath (Join-Path $runtime 'resources\stearlight-session\gamescope\bin\gamescope')) -or
    -not (Test-Path -LiteralPath (Join-Path $runtime 'resources\stearlight-session\gamescope\lib\ld-linux-aarch64.so.1'))) {
    & (Join-Path $PSScriptRoot 'extract-frame-runtime.ps1') `
        -RecoveryRawImage $recovery -DestinationDirectory $runtime `
        -WslDistribution $WslDistribution -SessionAssetsOnly
    if ($LASTEXITCODE -ne 0) { throw 'Steam Frame session asset extraction failed.' }
}

if (-not $ZstdExecutable) {
    $zstdCommand = Get-Command zstd.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -match '^[FG]:\\' } | Select-Object -First 1
    if ($zstdCommand) { $ZstdExecutable = $zstdCommand.Source }
}
if (-not $ZstdExecutable) {
    throw 'Pass -ZstdExecutable with a zstd.exe stored on F: or G:.'
}
$zstd = (Resolve-Path -LiteralPath $ZstdExecutable).Path
if ($zstd -notmatch '^[FG]:\\') {
    throw "The zstd executable must be stored on F: or G:: $zstd"
}
$steamTar = Join-Path $output 'steam-frame.tar'
if (-not (Test-Path -LiteralPath $steamTar) -or
    (Get-Item -LiteralPath $steamTar).Length -lt 1GB -or
    (Get-Item -LiteralPath $steamTar).LastWriteTimeUtc -lt (Get-Item -LiteralPath $steamArchive).LastWriteTimeUtc) {
    & $zstd -d --force --no-progress $steamArchive -o $steamTar
    if ($LASTEXITCODE -ne 0) { throw 'Steam Frame Steam client archive decompression failed.' }
}

# Extract the ARM64 Steam client and its UI data under WSL so its Linux
# symlinks and executable modes survive the Windows drive boundary.
$steamClient = Join-Path $output 'steam-client-extracted'
$steamClientMarker = Join-Path $steamClient 'RUNSTEAM.sh'
$steamClientTar = Join-Path $output 'steam-client.tar'
if (-not (Test-Path -LiteralPath $steamClientMarker)) {
    if (Test-Path -LiteralPath $steamClient) {
        throw "Incomplete Steam client directory; use a fresh output folder: $steamClient"
    }
    $drive = $output.Substring(0, 1).ToLowerInvariant()
    $relativeOutput = $output.Substring(3).Replace('\', '/')
    $mountRoot = "/tmp/stearlight-steam-client-$PID"
    $script = @"
set -eu
mkdir -p '$mountRoot'
mount -t drvfs $($drive.ToUpperInvariant()): '$mountRoot' -o metadata
trap 'umount "$mountRoot" 2>/dev/null || true; rmdir "$mountRoot" 2>/dev/null || true' EXIT
steam_tar='$mountRoot/$relativeOutput/steam-frame.tar'
stage='$mountRoot/$relativeOutput/steam-client-extracted'
client_tar='$mountRoot/$relativeOutput/steam-client.tar'
mkdir -p "$stage"
tar -x -o -f "$steam_tar" -C "$stage" ./steamrtarm64 ./linuxarm64 ./steam ./RUNSTEAM.sh ./steam.sh ./steam_msg.sh ./bin ./public ./steamui ./clientui ./resource ./tenfoot ./friends ./controller_base ./graphics ./legacycompat ./package ./fossilize_engine_filters.json ./ThirdPartyLegalNotices.html ./ThirdPartyLegalNotices-Chromium.html ./ThirdPartyLegalNotices.css ./ThirdPartyLegalNotices.doc ./steam_subscriber_agreement.txt
test -x "$stage/RUNSTEAM.sh"
test -x "$stage/steamrtarm64/steam"
test -f "$stage/linuxarm64/steamclient.so"
tar -C "$stage" -cf "$client_tar" .
"@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
    & wsl.exe --distribution $WslDistribution -- sh -c "echo $encoded | base64 -d | sh"
    if ($LASTEXITCODE -ne 0) { throw 'Steam client extraction/package step failed.' }
}
foreach ($marker in @(
    $steamClientMarker,
    (Join-Path $steamClient 'steamrtarm64\steam'),
    (Join-Path $steamClient 'linuxarm64\steamclient.so'),
    (Join-Path $steamClient 'steamui\movies\steamframe_startup_450x450.webm'),
    (Join-Path $steamClient 'steamui\images\steam_spinner.png'),
    (Join-Path $steamClient 'steamui\images\deck_boot_transition.png'),
    $steamClientTar
)) {
    if (-not (Test-Path -LiteralPath $marker)) { throw "Steam client staging is incomplete: $marker" }
}

# The recovery contains absolute ELF symlinks. Tar it under WSL so Docker
# receives Linux symlinks instead of following them on the Windows host.
$runtimeTar = Join-Path $output 'frame-runtime.tar'
$drive = $output.Substring(0, 1).ToLowerInvariant()
$mountRoot = "/tmp/stearlight-arm64-build-$PID"
$relativeOutput = $output.Substring(3).Replace('\', '/')
$script = @"
set -eu
mkdir -p '$mountRoot'
mount -t drvfs $($drive.ToUpperInvariant()): '$mountRoot' -o metadata
trap 'umount "$mountRoot" 2>/dev/null || true; rmdir "$mountRoot" 2>/dev/null || true' EXIT
tar -C '$mountRoot/$relativeOutput/frame-runtime' -cf '$mountRoot/$relativeOutput/frame-runtime.tar' .
"@
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
& wsl.exe --distribution $WslDistribution -- sh -c "echo $encoded | base64 -d | sh"
if ($LASTEXITCODE -ne 0) { throw 'Could not package the extracted runtime under WSL.' }

& docker import --platform linux/arm64 $runtimeTar stearlight-frame-runtime:latest | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Docker could not import the Frame runtime.' }
& docker import --platform linux/arm64 $steamClientTar stearlight-frame-steam-client:latest | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Docker could not import the Steam client runtime.' }
& docker buildx build --platform linux/arm64 `
    --build-context frame-runtime=docker-image://stearlight-frame-runtime:latest `
    --build-context steam-client=docker-image://stearlight-frame-steam-client:latest `
    --build-context "openvr-headers=$(Join-Path $repo 'packages\openvr\headers')" `
    -f (Join-Path $repo 'Containerfile') --load `
    -t stearlight-arm64-frame-vm $repo
if ($LASTEXITCODE -ne 0) { throw 'ARM64 VM image build failed.' }

$container = (& docker create --platform linux/arm64 --entrypoint /bin/sh stearlight-arm64-frame-vm).Trim()
if ($LASTEXITCODE -ne 0 -or -not $container) { throw 'Could not create the image export container.' }
try {
    & docker cp "${container}:/boot/vmlinuz-lts" (Join-Path $output 'vmlinuz-lts')
    if ($LASTEXITCODE -ne 0) { throw 'Could not export the ARM64 kernel.' }
    & docker cp "${container}:/boot/initramfs-lts" (Join-Path $output 'initramfs-lts')
    if ($LASTEXITCODE -ne 0) { throw 'Could not export the ARM64 initramfs.' }
    & docker export $container -o (Join-Path $output 'rootfs.tar')
    if ($LASTEXITCODE -ne 0) { throw 'Could not export the ARM64 root filesystem.' }
} finally {
    & docker rm $container | Out-Null
}

$volume = "stearlight-arm64-build-$PID"
& docker volume create $volume | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not create a temporary Linux build volume.' }
try {
    $dockerOutput = $output.Replace('\', '/')
    & docker run --rm --platform linux/arm64 `
        -v "${volume}:/work" -v "${dockerOutput}:/out" -e "STEARLIGHT_DISK_SIZE=${diskSize}G" alpine:edge sh -c `
        'apk add --no-cache e2fsprogs >/dev/null && mkdir -p /work/rootfs && tar -xf /out/rootfs.tar -C /work/rootfs && truncate -s $STEARLIGHT_DISK_SIZE /work/stearlight-arm64.ext4 && mke2fs -q -F -t ext4 -O ^has_journal,^orphan_file -E lazy_itable_init=0 -d /work/rootfs /work/stearlight-arm64.ext4 && e2fsck -fn /work/stearlight-arm64.ext4 && cp /work/stearlight-arm64.ext4 /out/stearlight-arm64.ext4'
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the ARM64 ext4 VM disk.' }
} finally {
    & docker volume rm $volume | Out-Null
}
Write-Host "ARM64 VM disk: $(Join-Path $output 'stearlight-arm64.ext4')"
