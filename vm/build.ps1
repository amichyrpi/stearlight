[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [string]$SteamVRDirectory = $env:STEARLIGHT_STEAMVR_SOURCE,
    [string]$RecoveryImageUrl = $env:STEARLIGHT_FRAME_RECOVERY_URL,
    [string]$WslDistribution = $env:STEARLIGHT_WSL_DISTRIBUTION,
    [switch]$KeepRecoveryRaw,
    [switch]$NoCache
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$vmRoot = (Resolve-Path $PSScriptRoot).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot 'build'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

function Test-PathInside([string]$Path, [string]$Root) {
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    return $resolvedPath.StartsWith(
        $resolvedRoot, [StringComparison]::OrdinalIgnoreCase)
}

function Get-RequiredCommand([string]$Name, [string]$Purpose) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$Name is required $Purpose and was not found in PATH."
    }
    return $command.Source
}

if (-not $SteamVRDirectory) {
    throw @'
An x86_64 SteamVR runtime is required for this x86_64 VM. Supply
-SteamVRDirectory <path> or set STEARLIGHT_STEAMVR_SOURCE. The Steam Frame
recovery download below supplies the authentic UI resources, but its ARM64
executables cannot run inside this x86_64 guest.
'@
}
$steamVrRoot = (Resolve-Path -LiteralPath $SteamVRDirectory).Path
if (-not [IO.File]::Exists((Join-Path $steamVrRoot 'bin\vrenv.sh'))) {
    # SteamCMD archives often contain the runtime under one extra directory.
    $nestedSteamVrRoot = Join-Path $steamVrRoot 'steamvr-linux'
    if ([IO.File]::Exists((Join-Path $nestedSteamVrRoot 'bin\vrenv.sh'))) {
        $steamVrRoot = (Resolve-Path -LiteralPath $nestedSteamVrRoot).Path
    }
}
foreach ($requiredPath in @(
    'bin\vrenv.sh',
    'bin\linux64\vrstartup',
    'bin\linux64\vrserver',
    'bin\linux64\vrdashboard',
    'bin\linux64\vrcompositor',
    'drivers\vrlink\driver.vrdrivermanifest',
    'drivers\null\driver.vrdrivermanifest',
    'drivers\null\bin\linux64\driver_null.so',
    'content',
    'tools\tools.vrmanifest',
    'steamxr_linux64.json')) {
    $path = Join-Path $steamVrRoot $requiredPath
    $exists = if ([IO.Path]::GetExtension($requiredPath)) {
        [IO.File]::Exists($path)
    } else {
        [IO.Directory]::Exists($path)
    }
    if (-not $exists) {
        throw "The x86_64 SteamVR source is missing '$requiredPath': $steamVrRoot"
    }
}

$docker = Get-RequiredCommand 'docker.exe' 'to fetch/extract recovery content and build the VM'
$qemuImg = Get-RequiredCommand 'qemu-img.exe' 'to convert the VM disk for QEMU'
$curl = Get-RequiredCommand 'curl.exe' 'to download the official recovery image'
& $docker buildx version | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Docker Buildx is required; enable it in Docker Desktop and retry.'
}
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL is required to mount the recovery image read-only and extract its Btrfs root.'
}

$cacheRoot = Join-Path $outputRoot 'cache'
$recoveryCache = Join-Path $cacheRoot 'recovery'
[IO.Directory]::CreateDirectory($recoveryCache) | Out-Null

if (-not $RecoveryImageUrl) {
    $indexUrl = 'https://steamdeck-images.steamos.cloud/recovery/'
    Write-Host "Checking Valve's SteamOS recovery index for the latest Steam Frame image..."
    $index = Invoke-WebRequest -Uri $indexUrl -UseBasicParsing
    $imageNames = [regex]::Matches(
        $index.Content,
        'href="(?<name>steamframe-oobe-repair-[^"]+\.img\.bz2)"',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase) |
        ForEach-Object { $_.Groups['name'].Value } |
        Sort-Object -Descending
    if (-not $imageNames) {
        throw "No Steam Frame recovery image was listed at $indexUrl. Set STEARLIGHT_FRAME_RECOVERY_URL to an official .img.bz2 URL to pin one."
    }
    $imageName = $imageNames[0]
    $RecoveryImageUrl = [Uri]::new([Uri]$indexUrl, $imageName).AbsoluteUri
} else {
    $imageName = [IO.Path]::GetFileName(([Uri]$RecoveryImageUrl).AbsolutePath)
    if ($imageName -notmatch '\.img\.bz2$') {
        throw "Steam Frame recovery URL must refer to a compressed .img.bz2 image: $RecoveryImageUrl"
    }
    $recoveryUri = [Uri]$RecoveryImageUrl
    if ($recoveryUri.Scheme -ne 'https' -or
        $recoveryUri.DnsSafeHost -ne 'steamdeck-images.steamos.cloud') {
        throw "Steam Frame recovery images must be downloaded from Valve's steamdeck-images.steamos.cloud host over HTTPS."
    }
}

$archive = Join-Path $recoveryCache $imageName
$downloadMarker = "$archive.complete"
if (-not [IO.File]::Exists($downloadMarker)) {
    Write-Host "Downloading Valve's Steam Frame recovery image to $archive"
    & $curl --fail --location --retry 5 --retry-all-errors `
        --continue-at - --output $archive $RecoveryImageUrl
    if ($LASTEXITCODE -ne 0) {
        throw "Recovery download failed with curl exit code $LASTEXITCODE. Rerun to resume the partial download."
    }
    if (-not [IO.File]::Exists($archive) -or
        (Get-Item -LiteralPath $archive).Length -lt 1GB) {
        throw "Downloaded recovery archive is unexpectedly small: $archive"
    }
    [IO.File]::WriteAllText($downloadMarker, $RecoveryImageUrl)
} elseif (-not [IO.File]::Exists($archive)) {
    throw "Recovery cache marker exists without its archive: $archive"
}

$rawImage = Join-Path $recoveryCache 'steamframe-recovery.raw'
$runtimeContext = Join-Path $outputRoot ".frame-runtime-$([Guid]::NewGuid().ToString('N'))"
$frameUiContext = Join-Path $outputRoot ".frame-ui-$([Guid]::NewGuid().ToString('N'))"
$exportDirectory = Join-Path $outputRoot ".build-export-$([Guid]::NewGuid().ToString('N'))"
$stagedRaw = Join-Path $exportDirectory 'stearlight-vm.raw'
$compressedRaw = Join-Path $exportDirectory 'stearlight-vm.raw.zst'
$image = Join-Path $outputRoot 'stearlight-vm-x86_64.vdi'
$checksum = "$image.sha256"
$stagedImage = Join-Path $outputRoot ".stearlight-vm-$([Guid]::NewGuid().ToString('N')).vdi"
$backupImage = Join-Path $outputRoot ".stearlight-backup-$([Guid]::NewGuid().ToString('N')).vdi"
$extractionSucceeded = $false
$buildSucceeded = $false
$createdRawImage = $false

try {
    if (-not [IO.File]::Exists($rawImage)) {
        Write-Host 'Decompressing the recovery disk using the Docker Alpine image...'
        $archiveNameInContainer = $imageName.Replace("'", "'\''")
        $decompress = "apk add --no-cache bzip2 >/dev/null && bunzip2 -kc '/cache/$archiveNameInContainer' > /cache/steamframe-recovery.raw.partial && mv /cache/steamframe-recovery.raw.partial /cache/steamframe-recovery.raw"
        & $docker run --rm `
            --mount "type=bind,source=$recoveryCache,target=/cache" `
            alpine:3.22 sh -ec $decompress
        if ($LASTEXITCODE -ne 0) {
            throw "Steam Frame recovery decompression failed with exit code $LASTEXITCODE."
        }
        $createdRawImage = $true
    }
    if ((Get-Item -LiteralPath $rawImage).Length -le
        (Get-Item -LiteralPath $archive).Length) {
        throw "Decompressed recovery image is incomplete: $rawImage"
    }

    [IO.Directory]::CreateDirectory($runtimeContext) | Out-Null
    & (Join-Path $PSScriptRoot 'extract-frame-ui.ps1') `
        -RecoveryRawImage $rawImage `
        -DestinationDirectory $runtimeContext `
        -WslDistribution $WslDistribution

    # The VM stays x86_64: only Valve's architecture-neutral Frame UI data is
    # combined with a matching-architecture SteamVR executable payload.
    [IO.Directory]::CreateDirectory($frameUiContext) | Out-Null
    foreach ($component in @('resources', 'content')) {
        $source = Join-Path $runtimeContext $component
        if (-not [IO.Directory]::Exists($source)) {
            throw "Frame recovery extraction did not produce '$component'."
        }
        Copy-Item -LiteralPath $source -Destination $frameUiContext -Recurse
    }
    $extractionSucceeded = $true

    [IO.Directory]::CreateDirectory($exportDirectory) | Out-Null
    $buildArgs = @(
        'buildx', 'build', '--platform', 'linux/amd64',
        '--build-context', "steamvr=$steamVrRoot",
        '--build-context', "frame-ui=$frameUiContext",
        '--target', 'image',
        '--output', "type=local,dest=$exportDirectory",
        '--progress', 'plain')
    if ($NoCache) { $buildArgs += '--no-cache' }
    $buildArgs += @('--file', (Join-Path $vmRoot 'Dockerfile'), $vmRoot)
    & $docker @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "stearlight VM image build failed with exit code $LASTEXITCODE."
    }
    if (-not [IO.File]::Exists($compressedRaw)) {
        throw "Buildx did not export the compressed raw disk: $compressedRaw"
    }

    $decompressRaw = "apk add --no-cache zstd >/dev/null && zstd -d -f /work/stearlight-vm.raw.zst -o /work/stearlight-vm.raw"
    & $docker run --rm `
        --mount "type=bind,source=$exportDirectory,target=/work" `
        alpine:3.22 sh -ec $decompressRaw
    if ($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($stagedRaw)) {
        throw 'The compressed VM disk could not be expanded for QEMU conversion.'
    }

    Write-Host "Converting the raw VM disk to $image"
    & $qemuImg convert -p -S 1M -f raw -O vdi $stagedRaw $stagedImage
    if ($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($stagedImage)) {
        throw 'QEMU raw-to-VDI conversion failed.'
    }
    $hash = (Get-FileHash -LiteralPath $stagedImage -Algorithm SHA256).Hash.ToLowerInvariant()
    if ([IO.File]::Exists($image)) {
        [IO.File]::Replace($stagedImage, $image, $backupImage, $true)
        [IO.File]::Delete($backupImage)
    } else {
        [IO.File]::Move($stagedImage, $image)
    }
    [IO.File]::WriteAllText(
        $checksum,
        "$hash  $([IO.Path]::GetFileName($image))$([Environment]::NewLine)")
    $buildSucceeded = $true
} finally {
    foreach ($temporaryPath in @($runtimeContext, $frameUiContext, $exportDirectory)) {
        if ((Test-PathInside $temporaryPath $outputRoot) -and
            [IO.Directory]::Exists($temporaryPath)) {
            [IO.Directory]::Delete($temporaryPath, $true)
        }
    }
    foreach ($temporaryPath in @($stagedImage, $backupImage)) {
        if ((Test-PathInside $temporaryPath $outputRoot) -and
            [IO.File]::Exists($temporaryPath)) {
            [IO.File]::Delete($temporaryPath)
        }
    }
    if ($buildSucceeded -and $extractionSucceeded -and $createdRawImage -and
        -not $KeepRecoveryRaw -and
        [IO.File]::Exists($rawImage)) {
        [IO.File]::Delete($rawImage)
    }
}

Write-Host "Built $image"
Write-Host "SHA-256: $checksum"
