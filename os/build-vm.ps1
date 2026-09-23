[CmdletBinding()]
param(
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputDirectory) {
    foreach ($driveName in @('F', 'G')) {
        $drive = Get-PSDrive -Name $driveName -PSProvider FileSystem `
            -ErrorAction SilentlyContinue
        if ($drive -and $drive.Free -gt 20GB) {
            $OutputDirectory = Join-Path "$($drive.Name):\" 'StearlightBuild\vm'
            break
        }
    }
}
if (-not $OutputDirectory) {
    throw 'No F: or G: build drive has at least 20 GB free; refusing to write build output on C:.'
}
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$outputDriveRoot = [IO.Path]::GetPathRoot($resolvedOutput)
if ($outputDriveRoot -notin @('F:\', 'G:\')) {
    throw "Output directory must be on F: or G:, not '$resolvedOutput'."
}
if ($resolvedOutput.TrimEnd('\') -eq $outputDriveRoot.TrimEnd('\')) {
    throw 'Refusing to delete an entire F: or G: volume as the output directory.'
}
[IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

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

$temporaryRoot = Join-Path $outputDriveRoot 'StearlightBuildTemp'
# Keep the staged VDI on the same approved volume as the final output. This
# lets the final replacement be a same-volume operation and preserves an
# existing image if Docker or qemu-img fails halfway through a rebuild.
[IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
$raw = Join-Path $temporaryRoot (
    ".stearlight-vm-raw-$([Guid]::NewGuid().ToString('N')).raw")
$exportDirectory = Join-Path $temporaryRoot (
    ".stearlight-vm-export-$([Guid]::NewGuid().ToString('N'))")
$image = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi'
$checksum = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi.sha256'
$stagedImage = Join-Path $temporaryRoot (
    ".stearlight-vm-vdi-$([Guid]::NewGuid().ToString('N')).vdi")
$backupImage = Join-Path $temporaryRoot (
    ".stearlight-vm-backup-$([Guid]::NewGuid().ToString('N')).vdi")
$conversionSucceeded = $false

try {
    # Export the FROM-scratch image directly to the approved temporary volume.
    # Loading the raw disk into Docker first makes Docker unpack the entire
    # multi-gigabyte image into its own store and is both slow and unnecessary.
    $steamVrDump = 'F:\StearlightBuildArchive\steamvr-linux\steamvr-linux'
    if (-not [IO.Directory]::Exists($steamVrDump)) {
        throw "SteamVR dump is required at '$steamVrDump'."
    }
    foreach ($requiredSteamVrFile in @(
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
        $requiredPath = Join-Path $steamVrDump $requiredSteamVrFile
        $exists = if ([IO.Path]::GetExtension($requiredSteamVrFile)) {
            [IO.File]::Exists($requiredPath)
        } else {
            [IO.Directory]::Exists($requiredPath)
        }
        if (-not $exists) {
            throw "SteamVR dump is incomplete; missing '$requiredPath'."
        }
    }
    if ([IO.Directory]::Exists($exportDirectory)) {
        [IO.Directory]::Delete($exportDirectory, $true)
    }
    [IO.Directory]::CreateDirectory($exportDirectory) | Out-Null
    $buildkitExportPath = $exportDirectory -replace '\\', '/'
    & docker buildx build --platform linux/amd64 --build-context `
        "steamvr=$steamVrDump" --target image `
        --output "type=local,dest=$buildkitExportPath" --progress plain `
        --file (Join-Path $PSScriptRoot 'Dockerfile.vm') $repo
    if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM image build failed.' }

    $raw = Join-Path $exportDirectory 'stearlight-vm.raw'
    if (-not [IO.File]::Exists($raw)) {
        throw "Stearlight VM raw image was not exported: $raw"
    }

    # Stage the dynamic VDI beside the raw export. Do not overwrite an older
    # known-good image until conversion and hashing have both succeeded.
    Write-Host "Converting $raw to staged image $stagedImage with $qemuImg"
    & $qemuImg convert -p -S 1M -f raw -O vdi $raw $stagedImage
    if ($LASTEXITCODE -ne 0) { throw 'Raw-to-VDI conversion failed.' }
    if (-not [IO.File]::Exists($stagedImage)) {
        throw "VM image was not created: $stagedImage"
    }

    $hash = (Get-FileHash -LiteralPath $stagedImage -Algorithm SHA256).Hash.ToLowerInvariant()
    if ([IO.File]::Exists($image)) {
        # Windows PowerShell's four-argument File.Replace overload rejects a
        # null backup path on some .NET Framework builds. An explicit backup
        # keeps the operation atomic and preserves the previous known-good
        # image until the replacement has succeeded.
        [IO.File]::Replace($stagedImage, $image, $backupImage, $true)
        [IO.File]::Delete($backupImage)
    } else {
        [IO.File]::Move($stagedImage, $image)
    }
    [IO.File]::WriteAllText(
        $checksum,
        "$hash  $([IO.Path]::GetFileName($image))$([Environment]::NewLine)")
    $conversionSucceeded = $true
} finally {
    if ([IO.Directory]::Exists($exportDirectory)) {
        [IO.Directory]::Delete($exportDirectory, $true)
    }
    if ([IO.File]::Exists($stagedImage)) {
        [IO.File]::Delete($stagedImage)
    }
    if ($conversionSucceeded -and [IO.File]::Exists($backupImage)) {
        [IO.File]::Delete($backupImage)
    }
}
Write-Host "Built $image"
