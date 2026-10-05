param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [string]$ArchivePath = 'stearlight-frame-windows.zip'
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$archive = [IO.Path]::GetFullPath($ArchivePath)
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
if (Test-Path -LiteralPath (Join-Path $package 'data')) { throw 'Account data must not be included in a download' }
foreach ($relative in @('start-vm.cmd', 'start-vm.ps1', 'vmlinuz-lts', 'initramfs-lts',
    'stearlight-arm64.qcow2', 'qemu/qemu-system-aarch64.exe', 'qemu/qemu-img.exe', 'qemu/share')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative))) { throw "Incomplete download: $relative" }
}
foreach ($entry in Get-Content -LiteralPath (Join-Path $package 'SHA256SUMS')) {
    $hash, $file = $entry -split '\s+', 2
    if ((Get-FileHash -LiteralPath (Join-Path $package $file) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
        throw "Checksum mismatch: $file"
    }
}
& 7z a -tzip -mx=1 $archive (Join-Path $package '*')
if ($LASTEXITCODE) { throw 'Archive creation failed' }
(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' +
    [IO.Path]::GetFileName($archive) | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ascii
Write-Host "Packaged Windows VM: $archive"
