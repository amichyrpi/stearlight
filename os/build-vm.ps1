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
$onBuildDrive = [IO.Path]::GetPathRoot($resolvedOutput) -ieq 'F:\'
if (-not $insideRepoOutput -and -not $onBuildDrive) {
    throw "Output directory must remain inside $outRoot or be on the dedicated F: build drive."
}
if ([IO.Directory]::Exists($resolvedOutput)) {
    [IO.Directory]::Delete($resolvedOutput, $true)
}
[IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

docker buildx build --platform linux/amd64 --target image `
    --output "type=local,dest=$resolvedOutput" `
    --file (Join-Path $PSScriptRoot 'Dockerfile.vm') $repo
if ($LASTEXITCODE -ne 0) { throw 'Stearlight VM image build failed.' }

$image = Join-Path $resolvedOutput 'stearlight-os-vm-x86_64.vdi'
if (-not [IO.File]::Exists($image)) { throw "VM image was not created: $image" }
Write-Host "Built $image"
