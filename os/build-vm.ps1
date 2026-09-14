[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\stearlight-vm')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outRoot = Join-Path $repo 'out'
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$expectedPrefix = [IO.Path]::GetFullPath($outRoot) + [IO.Path]::DirectorySeparatorChar
$insideRepoOutput = $resolvedOutput.StartsWith(
    $expectedPrefix, [StringComparison]::OrdinalIgnoreCase)
$onBuildDrive = [IO.Path]::GetPathRoot($resolvedOutput) -in @('F:\', 'G:\')
if (-not $insideRepoOutput -and -not $onBuildDrive) {
    throw "Output directory must remain inside $outRoot or be on the dedicated F: or G: build drive."
}
if ([IO.Directory]::Exists($resolvedOutput)) {
    [IO.Directory]::Delete($resolvedOutput, $true)
}
[IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

$buildTag = "stearlight-vm-export:$([Guid]::NewGuid().ToString('N'))"
$containerName = "stearlight-vm-export-$([Guid]::NewGuid().ToString('N'))"
$containerCreated = $false

$qemuImg = $null
$qemuCommand = Get-Command qemu-img.exe -ErrorAction SilentlyContinue
if ($qemuCommand) {
    $qemuImg = $qemuCommand.Source
} else {
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-img.exe',
        'C:\Program Files\QEMU\qemu-img.exe'
    )) {
        if ([IO.File]::Exists($candidate)) {
            $qemuImg = $candidate
            break
        }
    }
}
if (-not $qemuImg) { throw 'qemu-img.exe is required to convert the raw VM image to VDI.' }

$eDrive = Get-PSDrive -Name E -PSProvider FileSystem -ErrorAction SilentlyContinue
$temporaryRoot = $resolvedOutput
if ($eDrive -and $eDrive.Free -gt 20GB) {
    # The raw disk expands beyond the final VDI's logical size on Windows.
    # Keep both transient artifacts off F: when the separate work volume has
    # enough room; only the final VDI is retained on the build drive.
    $temporaryRoot = 'E:\StearlightBuildTemp'
    [IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
}
$raw = Join-Path $temporaryRoot (
    ".stearlight-vm-raw-$([Guid]::NewGuid().ToString('N')).raw")
$image = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi'
$checksum = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi.sha256'
$conversionSucceeded = $false

try {
    # Load the scratch image into the daemon, then copy its raw artifact from a
    # short-lived, uniquely named container to the temporary work volume.
    & docker buildx build --platform linux/amd64 --target image `
        --tag $buildTag --load --progress plain `
        --file (Join-Path $PSScriptRoot 'Dockerfile.vm') $repo
    if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM image build failed.' }

    # The export stage is FROM scratch and therefore has no default command.
    # Supplying one lets Docker create the stopped container; docker cp reads
    # its filesystem without starting it.
    & docker create --name $containerName $buildTag /bin/true | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM export container could not be created.' }
    $containerCreated = $true

    Write-Host "Copying ${containerName}:/stearlight-vm.raw to temporary raw image $raw"
    & docker cp "${containerName}:/stearlight-vm.raw" $raw
    if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM raw image could not be copied from Docker.' }

    # Write the dynamic VDI directly to its final F:/G: path. A cross-volume
    # Move-Item would first allocate the VDI's full logical size on the target
    # drive, even when sparse blocks would fit there.
    Write-Host "Converting $raw directly to $image with $qemuImg"
    & $qemuImg convert -p -S 1M -f raw -O vdi $raw $image
    if ($LASTEXITCODE -ne 0) { throw 'Raw-to-VDI conversion failed.' }
    if (-not [IO.File]::Exists($image)) {
        throw "VM image was not created: $image"
    }

    $hash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        $checksum,
        "$hash  $([IO.Path]::GetFileName($image))$([Environment]::NewLine)")
    $conversionSucceeded = $true
} finally {
    if ([IO.File]::Exists($raw)) {
        [IO.File]::Delete($raw)
    }
    if (-not $conversionSucceeded -and [IO.File]::Exists($image)) {
        [IO.File]::Delete($image)
    }
    if ($containerCreated) {
        $previousErrorAction = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & docker rm $containerName *> $null } finally {
            $ErrorActionPreference = $previousErrorAction
        }
    }
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & docker image inspect $buildTag *> $null
        if ($LASTEXITCODE -eq 0) {
            & docker image rm $buildTag *> $null
        }
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
}
Write-Host "Built $image"
