[CmdletBinding()]
param(
    [string]$VmDirectory = $PSScriptRoot,
    [ValidateRange(2048, 8192)][int]$MemoryMegabytes = 4096,
    [ValidateRange(1, 12)][int]$CpuCount = 6,
    [ValidatePattern('^\d+-\d+(\.\d+)*$')][string]$BluetoothBusId,
    [ValidateRange(0, 65535)][int]$DebugConsolePort = 0,
    [ValidateRange(0, 65535)][int]$QmpPort = 0,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$vmRoot = (Resolve-Path -LiteralPath $VmDirectory).Path
$qemuDir = Join-Path $vmRoot 'qemu'
$qemu = Join-Path $qemuDir 'qemu-system-aarch64.exe'
$qemuImg = Join-Path $qemuDir 'qemu-img.exe'
foreach ($relative in @('qemu/qemu-system-aarch64.exe', 'qemu/qemu-img.exe',
    'qemu/share', 'vmlinuz-lts', 'initramfs-lts', 'stearlight-arm64.qcow2')) {
    if (-not (Test-Path -LiteralPath (Join-Path $vmRoot $relative))) {
        throw "Incomplete VM download: missing $relative. Extract the entire archive before starting."
    }
}
if ($vmRoot.Contains(',')) { throw 'Extract the VM into a folder whose path has no commas.' }
$data = Join-Path $vmRoot 'data'
$overlay = Join-Path $data 'user.qcow2'
$qemuArgs = @(
    '-L', (Join-Path $qemuDir 'share'),
    '-name', 'Stearlight Steam Frame',
    '-machine', 'virt', '-accel', 'tcg,thread=multi',
    '-cpu', 'max,sve=off,sme=off', '-smp', [string]$CpuCount, '-m', [string]$MemoryMegabytes,
    '-display', 'gtk,gl=on,zoom-to-fit=on,show-menubar=off,window-close=on,refresh-rate=60000',
    '-device', 'virtio-gpu-gl-pci,blob=true,hostmem=1G,venus=true,xres=1440,yres=800',
    '-device', 'qemu-xhci', '-device', 'usb-kbd', '-device', 'usb-tablet',
    '-netdev', 'user,id=net0', '-device', 'virtio-net-device,netdev=net0',
    '-monitor', 'none', '-serial', ('file:' + (Join-Path $data 'serial.log').Replace('\', '/')),
    '-kernel', (Join-Path $vmRoot 'vmlinuz-lts'), '-initrd', (Join-Path $vmRoot 'initramfs-lts'),
    '-drive', ('file=' + $overlay.Replace('\', '/') + ',format=qcow2,if=virtio')
)
$boot = 'console=ttyAMA0,115200 root=/dev/vda rootfstype=ext4 rw quiet loglevel=0 logo.nologo vt.global_cursor_default=0 rd.udev.log_priority=0 stearlight.windows-gpu=1 stearlight.low-resolution=1'
$boot += " stearlight.boot-epoch=$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
if ($BluetoothBusId) { $boot += " stearlight.bluetooth-busid=$BluetoothBusId" }
if ($DebugConsolePort) {
    $boot += ' stearlight.debug=1'
    $qemuArgs += @('-device', 'virtio-serial-device', '-chardev',
        "socket,id=debug,host=127.0.0.1,port=$DebugConsolePort,server=on,wait=off",
        '-device', 'virtconsole,chardev=debug')
}
if ($QmpPort) { $qemuArgs += @('-qmp', "tcp:127.0.0.1:$QmpPort,server=on,wait=off") }
$qemuArgs += @('-append', $boot)
if ($DryRun) {
    [pscustomobject]@{ Executable = $qemu; Arguments = $qemuArgs; Overlay = $overlay }
    return
}

New-Item -ItemType Directory -Force -Path $data | Out-Null
# A file lock prevents two launchers from sharing the same writable account disk.
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $data 'vm.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch { throw 'This VM is already running, or the extracted folder is not writable.' }
$savedEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR', 'STEARLIGHT_VULKAN_IMAGE_EXPORT',
    'VIRGL_LOG_FILE', 'PATH', 'GSETTINGS_SCHEMA_DIR', 'GDK_PIXBUF_MODULE_FILE')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    Push-Location $data
    try {
        if (-not (Test-Path -LiteralPath $overlay)) {
            & $qemuImg create -f qcow2 -F qcow2 -b '../stearlight-arm64.qcow2' 'user.qcow2'
            if ($LASTEXITCODE -ne 0) { throw 'Could not create the writable VM disk.' }
        }
    } finally { Pop-Location }
    $temporary = Join-Path $data 'temp'
    New-Item -ItemType Directory -Force -Path $temporary | Out-Null
    $env:TEMP = $temporary; $env:TMP = $temporary; $env:TMPDIR = $temporary
    $env:STEARLIGHT_VULKAN_IMAGE_EXPORT = '1'
    $env:VIRGL_LOG_FILE = (Join-Path $data 'renderer.log').Replace('\', '/')
    $env:PATH = "$qemuDir;$env:PATH"
    $env:GSETTINGS_SCHEMA_DIR = Join-Path $qemuDir 'share/glib-2.0/schemas'
    $loaderCache = Join-Path $qemuDir 'lib/gdk-pixbuf-2.0/2.10.0/loaders.cache'
    if (Test-Path -LiteralPath $loaderCache) { $env:GDK_PIXBUF_MODULE_FILE = $loaderCache }
    # Start-Process joins arguments on Windows PowerShell 5.1. Quote each
    # argument so paths with spaces and the kernel command line stay intact.
    $quotedArguments = $qemuArgs | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
    Write-Host 'Starting Steam Frame. ARM emulation can take several minutes to reach sign-in.'
    Write-Host "Account/settings: $data"
    $process = Start-Process -FilePath $qemu -ArgumentList $quotedArguments -PassThru `
        -WorkingDirectory $vmRoot -WindowStyle Normal -RedirectStandardError (Join-Path $data 'qemu.log')
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "QEMU exited with code $($process.ExitCode). See $(Join-Path $data 'qemu.log')."
    }
} finally {
    foreach ($entry in $savedEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    if ($lock) { $lock.Dispose() }
}
