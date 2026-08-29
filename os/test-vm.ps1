[CmdletBinding()]
param(
    [string]$ImagePath = (Join-Path $PSScriptRoot '..\out\stearlight-vm\stearlight-os-vm-x86_64.vdi'),
    [string]$Name = 'Stearlight-OS-Test',
    [int]$BootSeconds = 45,
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$image = (Resolve-Path -LiteralPath $ImagePath).Path
$output = Split-Path -Parent $image
$screenshot = Join-Path $output 'stearlight-vm.png'
$serialLog = Join-Path $output 'stearlight-vm-serial.log'

# QEMU is the supported fallback on machines without VirtualBox.  Keep all
# firmware, logs and temporary state beside the VM image so a test never uses
# the system drive for VM data.
if (-not [IO.File]::Exists($vbox)) {
    $qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
    $qemuCode = 'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
    $qemuVarsSource = 'C:\Program Files\qemu\share\edk2-i386-vars.fd'
    if (-not [IO.File]::Exists($qemu)) {
        throw 'Neither VirtualBox 7.x nor QEMU was found. Install QEMU to run the VM test.'
    }
    if (-not [IO.File]::Exists($qemuCode) -or -not [IO.File]::Exists($qemuVarsSource)) {
        throw 'QEMU EFI firmware files are missing.'
    }

    [IO.Directory]::CreateDirectory($output) | Out-Null
    $qemuCodeLocal = Join-Path $output 'edk2-code.fd'
    $qemuVars = Join-Path $output 'edk2-vars.fd'
    Copy-Item -LiteralPath $qemuCode -Destination $qemuCodeLocal -Force
    Copy-Item -LiteralPath $qemuVarsSource -Destination $qemuVars -Force
    Remove-Item -LiteralPath $serialLog -Force -ErrorAction SilentlyContinue

    $qemuArgs = @(
        '-machine', 'q35',
        '-m', '4096',
        '-smp', '2',
        '-accel', 'tcg,thread=multi',
        '-drive', "if=pflash,format=raw,readonly=on,file=$qemuCodeLocal",
        '-drive', "if=pflash,format=raw,file=$qemuVars",
        '-drive', "file=$image,format=vdi,if=virtio",
        '-display', 'sdl',
        '-serial', "file:$serialLog",
        '-monitor', 'none',
        '-no-reboot',
        '-snapshot'
    )
    $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru
    Write-Host "QEMU started (PID $($qemuProcess.Id)); waiting $BootSeconds seconds..."
    $deadline = [DateTime]::UtcNow.AddSeconds($BootSeconds)
    $serial = ''
    $booted = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($qemuProcess.HasExited) { break }
        if (Test-Path -LiteralPath $serialLog) {
            try { $serial = Get-Content -LiteralPath $serialLog -Raw -ErrorAction SilentlyContinue } catch { }
            if ($serial -match '(?i)(X\.Org X Server|SVRT UI|svrt-receiver)') {
                $booted = $true
                break
            }
        }
        Start-Sleep -Seconds 1
    }
    if (-not $booted) {
        $exitCode = if ($qemuProcess.HasExited) { $qemuProcess.ExitCode } else { 'timeout' }
        if (-not $qemuProcess.HasExited) {
            Stop-Process -Id $qemuProcess.Id -Force -ErrorAction SilentlyContinue
        }
        throw "VM did not reach the Stearlight session (code $exitCode). Serial log: $serialLog`n$serial"
    }
    Write-Host "QEMU serial smoke test passed. Serial log: $serialLog"

    if (-not $KeepRunning) {
        Stop-Process -Id $qemuProcess.Id -Force -ErrorAction SilentlyContinue
        Write-Host 'QEMU stopped.'
    } else {
        Write-Host "QEMU remains running (PID $($qemuProcess.Id))."
    }
    return
}

$vmRoot = Join-Path $output 'virtualbox'
$machineFolder = Join-Path $vmRoot $Name
$testDisk = Join-Path $machineFolder 'stearlight-test.vdi'

function Invoke-VBox {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $vbox @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage failed: $($Arguments -join ' ')"
    }
}

function Remove-TestMachineFolder {
    if (-not [IO.Directory]::Exists($machineFolder)) { return }

    $safePrefix = [IO.Path]::GetFullPath($vmRoot) + [IO.Path]::DirectorySeparatorChar
    $resolvedMachine = [IO.Path]::GetFullPath($machineFolder)
    if (-not $resolvedMachine.StartsWith($safePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a VirtualBox directory outside the VM output root.'
    }

    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            [IO.Directory]::Delete($resolvedMachine, $true)
            return
        } catch [IO.IOException] {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Assert-VisibleScreenshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::FromFile($Path)
    try {
        $visibleSamples = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 16) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 16) {
                $pixel = $bitmap.GetPixel($x, $y)
                if (($pixel.R + $pixel.G + $pixel.B) -gt 18) {
                    $visibleSamples++
                }
            }
        }
    } finally {
        $bitmap.Dispose()
    }

    if ($visibleSamples -lt 5) {
        throw "VM framebuffer remained black ($visibleSamples visible samples). See $serialLog"
    }
    Write-Host "Framebuffer check: $visibleSamples visible samples"
}

$listed = & $vbox list vms
if ($LASTEXITCODE -ne 0) { throw 'Unable to list VirtualBox machines.' }
if ($listed -match ('"' + [regex]::Escape($Name) + '"')) {
    $running = & $vbox list runningvms
    if ($LASTEXITCODE -ne 0) { throw 'Unable to list running VirtualBox machines.' }
    if ($running -match ('"' + [regex]::Escape($Name) + '"')) {
        Invoke-VBox controlvm $Name poweroff | Out-Null
    }
    Invoke-VBox unregistervm $Name | Out-Null
}
Remove-TestMachineFolder
[IO.Directory]::CreateDirectory($vmRoot) | Out-Null

Invoke-VBox createvm --name $Name --basefolder $vmRoot `
    --platform-architecture x86 --ostype Linux_64 --register | Out-Null
Copy-Item -LiteralPath $image -Destination $testDisk
Invoke-VBox modifyvm $Name --firmware efi --memory 4096 --cpus 2 `
    --graphicscontroller vmsvga --vram 128 --accelerate3d on `
    --audio-enabled off --nic1 nat --uart1 0x3F8 4 `
    --uart-mode1 file $serialLog
Invoke-VBox modifyvm $Name --nat-pf1 'stearlight-ssh,tcp,127.0.0.1,2222,,22'
Invoke-VBox storagectl $Name --name SATA --add sata --controller IntelAhci
Invoke-VBox storageattach $Name --storagectl SATA --port 0 --device 0 `
    --type hdd --medium $testDisk
Invoke-VBox setextradata $Name CustomVideoMode1 1920x1080x32
Invoke-VBox startvm $Name --type headless | Out-Null

Start-Sleep -Seconds $BootSeconds
Invoke-VBox controlvm $Name screenshotpng $screenshot
Assert-VisibleScreenshot -Path $screenshot
Write-Host "Screenshot: $screenshot"
Write-Host "Serial log: $serialLog"

if (-not $KeepRunning) {
    Invoke-VBox controlvm $Name poweroff | Out-Null
    Invoke-VBox unregistervm $Name | Out-Null
    Remove-TestMachineFolder
}
