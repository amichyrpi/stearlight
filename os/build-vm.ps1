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

docker buildx build --platform linux/amd64 --target image `
    --output "type=local,dest=$resolvedOutput" `
    --file (Join-Path $PSScriptRoot 'Dockerfile.vm') $repo
if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM image build failed.' }

$raw = Join-Path $resolvedOutput 'stearlight-vm.raw'
$image = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi'
$checksum = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi.sha256'
if (-not [IO.File]::Exists($raw)) { throw "Raw VM image was not created: $raw" }

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

Write-Host "Converting $raw to $image with $qemuImg"
& $qemuImg convert -p -f raw -O vdi $raw $image
if ($LASTEXITCODE -ne 0) { throw 'Raw-to-VDI conversion failed.' }
if (-not [IO.File]::Exists($image)) { throw "VM image was not created: $image" }

$hash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(
    $checksum,
    "$hash  $([IO.Path]::GetFileName($image))$([Environment]::NewLine)")
Remove-Item -LiteralPath $raw -Force
Write-Host "Built $image"
