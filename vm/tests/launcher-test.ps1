param([Parameter(Mandatory = $true)][string]$QemuDirectory)
$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $QemuDirectory).Path
$qemuImg = Join-Path $runtime 'qemu-img.exe'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('Frame VM test ' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $package = Join-Path $temporary 'first folder'
    New-Item -ItemType Directory -Path $package,(Join-Path $package 'data') | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $package 'qemu') -Target $runtime | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $package 'vmlinuz-lts'), [byte[]]@(0))
    [IO.File]::WriteAllBytes((Join-Path $package 'initramfs-lts'), [byte[]]@(0))
    & $qemuImg create -f qcow2 (Join-Path $package 'stearlight-arm64.qcow2') 64M
    if ($LASTEXITCODE) { throw 'Test base disk creation failed' }
    $launch = & (Join-Path $PSScriptRoot '../start-vm.ps1') -VmDirectory $package -DryRun
    if ($launch.Arguments -notcontains 'tcg,thread=multi') { throw 'ARM emulation missing' }
    if ($launch.Arguments -contains '-snapshot') { throw 'Account disk would be discarded' }
    if (($launch.Arguments | Where-Object { $_ -like '*stearlight.windows-gpu=1*' }).Count -ne 1) { throw 'GPU boot option missing' }
    if (Test-Path -LiteralPath $launch.Overlay) { throw 'Dry run created account state' }
    Push-Location (Join-Path $package 'data')
    try {
        & $qemuImg create -f qcow2 -F qcow2 -b '../stearlight-arm64.qcow2' user.qcow2
        if ($LASTEXITCODE) { throw 'Relative overlay creation failed' }
    } finally { Pop-Location }
    $moved = Join-Path $temporary 'moved folder'
    Move-Item -LiteralPath $package -Destination $moved
    $info = & $qemuImg info --output=json (Join-Path $moved 'data/user.qcow2') | ConvertFrom-Json
    if ($LASTEXITCODE -or $info.'backing-filename' -ne '../stearlight-arm64.qcow2') { throw 'Overlay lost its relative backing image' }
    & $qemuImg check (Join-Path $moved 'data/user.qcow2')
    if ($LASTEXITCODE) { throw 'Moved account disk is invalid' }
    Write-Host 'Launcher path, dry-run and portable account disk checks passed.'
} finally {
    # Remove the junction before recursive cleanup, retaining the real runtime.
    foreach ($folder in @('first folder', 'moved folder')) {
        $junction = Join-Path $temporary "$folder/qemu"
        if (Test-Path -LiteralPath $junction) { [IO.Directory]::Delete($junction) }
    }
    if (-not [IO.Path]::GetFullPath($temporary).StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test cleanup escaped its directory'
    }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
