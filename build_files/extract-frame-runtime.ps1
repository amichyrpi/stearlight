[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RecoveryRawImage,

    [Parameter(Mandatory = $true)]
    [string]$DestinationDirectory,

    [string]$WslDistribution = 'docker-desktop',

    [switch]$SteamArchiveOnly,
    [switch]$SessionAssetsOnly
)

$ErrorActionPreference = 'Stop'
if ($SteamArchiveOnly -and $SessionAssetsOnly) {
    throw '-SteamArchiveOnly and -SessionAssetsOnly cannot be combined.'
}

function Get-WslMountedPath([string]$Path, [string]$MountRoot) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $driveRoot = [IO.Path]::GetPathRoot($fullPath)
    if ($driveRoot -notmatch '^([A-Za-z]):\\$') {
        throw "Path must be on a mounted Windows drive: '$fullPath'."
    }

    $drive = $Matches[1].ToLowerInvariant()
    $relative = $fullPath.Substring($driveRoot.Length).Replace('\', '/')
    return [pscustomobject]@{
        Drive = $drive
        Path = "$MountRoot/$drive/$relative"
    }
}

function ConvertTo-ShellLiteral([string]$Value) {
    if ($Value.Contains("'")) {
        throw "Paths containing an apostrophe are not supported by the WSL extractor: '$Value'."
    }
    return "'$Value'"
}

$rawPath = (Resolve-Path -LiteralPath $RecoveryRawImage).Path
if ([IO.Directory]::Exists($rawPath)) {
    throw "Recovery input must be the decompressed raw disk image, not a directory: '$rawPath'."
}
$rawInfo = Get-Item -LiteralPath $rawPath

$destinationPath = [IO.Path]::GetFullPath($DestinationDirectory)
if ([IO.File]::Exists($destinationPath)) {
    throw "Extraction destination is a file: '$destinationPath'."
}
if (-not $SteamArchiveOnly -and -not $SessionAssetsOnly -and [IO.Directory]::Exists($destinationPath) -and
    (Get-ChildItem -LiteralPath $destinationPath -Force | Select-Object -First 1)) {
    throw "Extraction destination must be empty: '$destinationPath'."
}
[IO.Directory]::CreateDirectory($destinationPath) | Out-Null
$steamArchivePath = Join-Path (Split-Path $destinationPath -Parent) 'steam.tar.zst'
if ([IO.Directory]::Exists($steamArchivePath)) {
    throw "Steam archive destination is a directory: '$steamArchivePath'."
}

# Resolve the Btrfs rootfs-A partition from GPT rather than relying on the
# current recovery image's fixed partition offset.
$stream = [IO.File]::OpenRead($rawPath)
try {
    if ($stream.Length -lt 1024) {
        throw "Recovery image is too small to contain a GPT disk: '$rawPath'."
    }

    $reader = [IO.BinaryReader]::new($stream)
    $stream.Position = 512
    $header = $reader.ReadBytes(92)
    if ([Text.Encoding]::ASCII.GetString($header, 0, 8) -ne 'EFI PART') {
        throw "Input is not an uncompressed GPT recovery disk. Decompress the .img.bz2 first: '$rawPath'."
    }

    $entryLba = [BitConverter]::ToUInt64($header, 72)
    $entryCount = [BitConverter]::ToUInt32($header, 80)
    $entrySize = [BitConverter]::ToUInt32($header, 84)
    if ($entryCount -lt 1 -or $entryCount -gt 4096 -or $entrySize -lt 128) {
        throw "Invalid GPT partition table in '$rawPath'."
    }

    $stream.Position = [long]$entryLba * 512
    $rootStartLba = $null
    $rootEndLba = $null
    for ($index = 0; $index -lt $entryCount; $index++) {
        $entry = $reader.ReadBytes([int]$entrySize)
        if ($entry.Length -ne $entrySize) { break }

        $typeGuid = [Guid]::new([byte[]]$entry[0..15])
        if ($typeGuid -eq [Guid]::Empty) { continue }

        $nameLength = [Math]::Min(72, $entry.Length - 56)
        $name = [Text.Encoding]::Unicode.GetString($entry, 56, $nameLength).Trim([char]0)
        if ($name -ieq 'rootfs-A') {
            $rootStartLba = [BitConverter]::ToUInt64($entry, 32)
            $rootEndLba = [BitConverter]::ToUInt64($entry, 40)
            break
        }
    }

    if ($null -eq $rootStartLba -or $null -eq $rootEndLba -or
        $rootEndLba -lt $rootStartLba) {
        throw "Could not find the Steam Frame rootfs-A GPT partition in '$rawPath'."
    }

    $rootOffset = [long]$rootStartLba * 512
    $rootLength = [long]($rootEndLba - $rootStartLba + 1) * 512
    if ($rootOffset + $rootLength -gt $stream.Length) {
        throw "The rootfs-A partition extends beyond the raw image: '$rawPath'."
    }
} finally {
    $stream.Dispose()
}

$wslMountRoot = "/tmp/stearlight-frame-extract-$PID"
$rawMount = Get-WslMountedPath $rawPath $wslMountRoot
$destinationMount = Get-WslMountedPath $destinationPath $wslMountRoot
$steamArchiveMount = Get-WslMountedPath $steamArchivePath $wslMountRoot
$driveLetters = @($rawMount.Drive, $destinationMount.Drive, $steamArchiveMount.Drive) | Select-Object -Unique
$mountLines = ($driveLetters | ForEach-Object {
    "mkdir -p $wslMountRoot/$_; mount -t drvfs $($_.ToUpperInvariant()): $wslMountRoot/$_ -o metadata"
}) -join "`n"

$wslScript = @'
set -eu
__MOUNT_DRIVES__
modprobe btrfs
frame_root=/tmp/stearlight-frame-recovery-root
mkdir -p "$frame_root"
loop_device=
cleanup() {
  umount "$frame_root" 2>/dev/null || true
  if [ -n "$loop_device" ]; then
    losetup -d "$loop_device" 2>/dev/null || true
  fi
__UNMOUNT_DRIVES__
  rmdir __MOUNT_ROOT__ 2>/dev/null || true
}
trap cleanup EXIT INT TERM
loop_device=$(losetup --find --show --read-only --offset __ROOT_OFFSET__ --sizelimit __ROOT_LENGTH__ __RAW_PATH__)
mount -t btrfs -o ro,subvolid=5 "$loop_device" "$frame_root"
destination=__DESTINATION__
mkdir -p "$destination"
__GAMESCOPE_EXTRACTOR__
if [ '__STEAM_ARCHIVE_ONLY__' != '1' ] && [ '__SESSION_ASSETS_ONLY__' != '1' ]; then
components='bin/linuxarm64 bin/vrwebhelper/linuxarm64 bin/vrenv.sh resources content drivers/frame_controller tools steamxr_linuxarm64.json third_party_legal_notices.txt third_party_legal_notices_web.txt'
for component in $components; do
  source_path="$frame_root/opt/steamvr/$component"
  if [ ! -e "$source_path" ]; then
    printf 'Steam Frame recovery is missing required runtime path: %s\n' "$component" >&2
    exit 1
  fi
  parent=${component%/*}
  if [ "$parent" = "$component" ]; then parent=.; fi
  mkdir -p "$destination/$parent"
  cp -a "$source_path" "$destination/$parent/"
done
if [ -f "$frame_root/opt/steamvr/.disable-steamvr-health-check" ]; then
  cp -a "$frame_root/opt/steamvr/.disable-steamvr-health-check" "$destination/"
fi
for binary in vrstartup vrserver vrcompositor vrdashboard vrpathreg; do
  if [ ! -x "$destination/bin/linuxarm64/$binary" ]; then
    printf 'Extracted SteamVR binary is missing or not executable: %s\n' "$binary" >&2
    exit 1
  fi
done
fi
if [ '__STEAM_ARCHIVE_ONLY__' != '1' ]; then
  # The physical CV driver is hardware-specific. Preserve the resource-only
  # Frame HMD profile so the VM driver can load Valve's compositor/UI defaults.
  for component in drivers/frame_hmd/driver.vrdrivermanifest drivers/frame_hmd/resources/frame_hmd_additional.vrsettings drivers/frame_hmd/resources/input; do
    test -e "$frame_root/opt/steamvr/$component"
    mkdir -p "$destination/${component%/*}"
    cp -a "$frame_root/opt/steamvr/$component" "$destination/${component%/*}/"
  done
  mkdir -p "$destination/resources/stearlight-session"
  for asset in steamos-cursor.png steam_icon.png; do
    test -s "$frame_root/usr/share/steamos/$asset"
    cp -a "$frame_root/usr/share/steamos/$asset" "$destination/resources/stearlight-session/"
  done
  extract_frame_gamescope "$frame_root" "$destination/resources/stearlight-session/gamescope"
fi
if [ '__SESSION_ASSETS_ONLY__' = '1' ]; then exit 0; fi
steam_archive_source="$frame_root/usr/lib/steam/steam.tar.zst"
if [ ! -s "$steam_archive_source" ]; then
  printf 'Steam Frame recovery is missing Steam client archive: %s\n' "$steam_archive_source" >&2
  exit 1
fi
steam_archive=__STEAM_ARCHIVE__
mkdir -p "$(dirname "$steam_archive")"
cp -f "$steam_archive_source" "$steam_archive"
'@

$unmountLines = ($driveLetters | ForEach-Object {
    "  umount $wslMountRoot/$_ 2>/dev/null || true; rmdir $wslMountRoot/$_ 2>/dev/null || true"
}) -join "`n"
$wslScript = $wslScript.Replace('__MOUNT_ROOT__', (ConvertTo-ShellLiteral $wslMountRoot))
$wslScript = $wslScript.Replace('__MOUNT_DRIVES__', $mountLines)
$wslScript = $wslScript.Replace('__UNMOUNT_DRIVES__', $unmountLines)
$wslScript = $wslScript.Replace('__ROOT_OFFSET__', [string]$rootOffset)
$wslScript = $wslScript.Replace('__ROOT_LENGTH__', [string]$rootLength)
$wslScript = $wslScript.Replace('__RAW_PATH__', (ConvertTo-ShellLiteral $rawMount.Path))
$wslScript = $wslScript.Replace('__DESTINATION__', (ConvertTo-ShellLiteral $destinationMount.Path))
$gamescopeExtractor = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'extract-gamescope-runtime.sh') -Raw
$wslScript = $wslScript.Replace('__GAMESCOPE_EXTRACTOR__', $gamescopeExtractor.Replace("`r", ''))
$wslScript = $wslScript.Replace('__STEAM_ARCHIVE__', (ConvertTo-ShellLiteral $steamArchiveMount.Path))
$wslScript = $wslScript.Replace('__STEAM_ARCHIVE_ONLY__', $(if ($SteamArchiveOnly) { '1' } else { '0' }))
$wslScript = $wslScript.Replace('__SESSION_ASSETS_ONLY__', $(if ($SessionAssetsOnly) { '1' } else { '0' }))

# Pass the script as base64 because Windows PowerShell's native argument
# quoting strips embedded double quotes from multi-line WSL command strings.
$encodedScript = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($wslScript))
$wslCommand = "echo $encodedScript | base64 -d | sh"
& wsl.exe --distribution $WslDistribution -- sh -c $wslCommand
if ($LASTEXITCODE -ne 0) {
    throw "Read-only Steam Frame runtime extraction failed with exit code $LASTEXITCODE."
}

if ($SessionAssetsOnly) {
    foreach ($asset in @(
        'resources\stearlight-session\steamos-cursor.png',
        'resources\stearlight-session\steam_icon.png',
        'drivers\frame_hmd\driver.vrdrivermanifest',
        'drivers\frame_hmd\resources\frame_hmd_additional.vrsettings'
    )) {
        if (-not [IO.File]::Exists((Join-Path $destinationPath $asset))) {
            throw "Extracted Steam Frame session asset is missing: $asset"
        }
    }
    Write-Host "Extracted Valve's Steam Frame session assets to '$destinationPath'."
    return
}
if (-not $SteamArchiveOnly) {
    foreach ($requiredRelativePath in @(
        'bin\linuxarm64\vrstartup',
        'bin\linuxarm64\vrserver',
        'bin\linuxarm64\vrcompositor',
        'bin\linuxarm64\vrdashboard',
        'bin\vrwebhelper\linuxarm64\libcef.so',
        'resources\webinterface\dashboard\systemui.html',
        'drivers\frame_controller\driver.vrdrivermanifest',
        'drivers\frame_hmd\resources\frame_hmd_additional.vrsettings',
        'tools\vrlink\bin\linuxarm64\vrlink'
    )) {
        if (-not [IO.File]::Exists((Join-Path $destinationPath $requiredRelativePath))) {
            throw "Extracted Steam Frame runtime is incomplete: '$requiredRelativePath'."
        }
    }
    Write-Host "Extracted Valve's ARM64 SteamVR runtime from '$rawPath' to '$destinationPath'."
}
if (-not (Test-Path -LiteralPath $steamArchivePath) -or
    (Get-Item -LiteralPath $steamArchivePath).Length -lt 1MB) {
    throw "Extracted Steam client archive is missing or too small: '$steamArchivePath'."
}
Write-Host "Extracted Valve's ARM64 Steam client archive from '$rawPath' to '$steamArchivePath'."
