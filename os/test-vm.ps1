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
$qemuScreenshot = Join-Path $output 'stearlight-vm.ppm'
$serialLog = Join-Path $output 'stearlight-vm-serial.log'
$expectedWidth = 2880
$expectedHeight = 1600
$expectedRefresh = 60

function Read-SerialLog {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    try {
        return Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
    } catch {
        return ''
    }
}

function Wait-SerialReady {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 45
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $serial = ''
    while ([DateTime]::UtcNow -lt $deadline) {
        $serial = Read-SerialLog -Path $Path
        if ($serial -match '(?i)active mode is|could not open user') {
            throw "Stearlight session reported a display/runtime error. Serial log: $Path`n$serial"
        }
        if ($serial -match "SVRT UI READY $expectedWidth`x$expectedHeight @ $expectedRefresh`Hz") {
            return $serial
        }
        if ($Process -and $Process.HasExited) { break }
        Start-Sleep -Seconds 1
    }
    $exitCode = if ($Process -and $Process.HasExited) {
        $Process.ExitCode
    } else { 'timeout' }
    throw "VM did not report SVRT UI READY ${expectedWidth}x${expectedHeight}@${expectedRefresh}Hz (code $exitCode). Serial log: $Path`n$serial"
}

function Invoke-QemuMonitor {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $buffer = New-Object byte[] 4096
        try {
            while ($stream.DataAvailable) {
                $read = $stream.Read($buffer, 0, $buffer.Length)
                if ($read -le 0) { break }
            }
        } catch [System.IO.IOException] { }
        $bytes = [Text.Encoding]::ASCII.GetBytes("$Command`n")
        $stream.Write($bytes, 0, $bytes.Length)
        Start-Sleep -Milliseconds 400
        $response = New-Object System.Text.StringBuilder
        try {
            while ($stream.DataAvailable) {
                $read = $stream.Read($buffer, 0, $buffer.Length)
                if ($read -le 0) { break }
                [void]$response.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $read))
            }
        } catch [System.IO.IOException] { }
        return $response.ToString()
    } finally {
        if ($client) { $client.Dispose() }
    }
}

function Read-PpmToken {
    param([Parameter(Mandatory = $true)][IO.Stream]$Stream)

    while ($true) {
        $byte = $Stream.ReadByte()
        if ($byte -lt 0) { return $null }
        if ([char]::IsWhiteSpace([char]$byte)) { continue }
        if ($byte -eq [byte][char]'#') {
            do { $byte = $Stream.ReadByte() } while ($byte -ge 0 -and $byte -ne 10)
            continue
        }
        $bytes = New-Object 'System.Collections.Generic.List[byte]'
        while ($byte -ge 0 -and -not [char]::IsWhiteSpace([char]$byte)) {
            [void]$bytes.Add([byte]$byte)
            $byte = $Stream.ReadByte()
        }
        return [Text.Encoding]::ASCII.GetString($bytes.ToArray())
    }
}

function Assert-VisibleScreenshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "VM framebuffer screenshot was not created: $Path"
    }
    $stream = [IO.File]::OpenRead($Path)
    $header = New-Object byte[] 2
    $headerRead = $stream.Read($header, 0, 2)
    $isPpm = $headerRead -eq 2 -and $header[0] -eq [byte][char]'P' -and
             $header[1] -eq [byte][char]'6'
    if (-not $isPpm) {
        $stream.Dispose()
        # VirtualBox's screenshotpng command writes PNG while QEMU's HMP
        # screendump writes P6 PPM. Keep the same dimension/visibility checks
        # for both backends.
        Add-Type -AssemblyName System.Drawing
        $bitmap = [Drawing.Bitmap]::FromFile($Path)
        try {
            if ($bitmap.Width -ne $expectedWidth -or
                $bitmap.Height -ne $expectedHeight) {
                throw "VM framebuffer is $($bitmap.Width)x$($bitmap.Height); expected ${expectedWidth}x${expectedHeight}."
            }
            $visibleSamples = 0
            for ($y = 0; $y -lt $bitmap.Height; $y += 32) {
                for ($x = 0; $x -lt $bitmap.Width; $x += 32) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    if (($pixel.R + $pixel.G + $pixel.B) -gt 18) {
                        $visibleSamples++
                    }
                }
            }
        } finally {
            $bitmap.Dispose()
        }
    } else {
        [void]$stream.Seek(0, [IO.SeekOrigin]::Begin)
        try {
            $magic = Read-PpmToken -Stream $stream
            if ($magic -ne 'P6') {
                throw "VM framebuffer is not a binary PPM (magic '$magic'): $Path"
            }
            $width = 0
            $height = 0
            $maxValue = 0
            if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
                -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
                -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue)) {
                throw "VM framebuffer has an invalid PPM header: $Path"
            }
            if ($width -ne $expectedWidth -or $height -ne $expectedHeight -or
                $maxValue -ne 255) {
                throw "VM framebuffer is ${width}x${height} (max $maxValue); expected ${expectedWidth}x${expectedHeight} (max 255)."
            }
            $pixelBytes = [int64]$width * [int64]$height * 3
            if ($stream.Length -lt $pixelBytes) {
                throw "VM framebuffer is truncated ($($stream.Length) bytes; expected at least $pixelBytes)."
            }
            # Deriving the offset from the file length handles variable header
            # spacing without treating a valid first pixel as whitespace.
            $pixelOffset = $stream.Length - $pixelBytes
            $pixel = New-Object byte[] 3
            $visibleSamples = 0
            for ($y = 0; $y -lt $height; $y += 32) {
                for ($x = 0; $x -lt $width; $x += 32) {
                    $position = $pixelOffset + (([int64]$y * $width + $x) * 3)
                    [void]$stream.Seek($position, [IO.SeekOrigin]::Begin)
                    if ($stream.Read($pixel, 0, 3) -eq 3 -and
                        ([int]$pixel[0] + [int]$pixel[1] + [int]$pixel[2]) -gt 18) {
                        $visibleSamples++
                    }
                }
            }
        } finally {
            $stream.Dispose()
        }
    }

    if ($visibleSamples -lt 5) {
        throw "VM framebuffer remained black ($visibleSamples visible samples). See $serialLog"
    }
    Write-Host "Framebuffer check: $visibleSamples visible samples ($expectedWidth`x$expectedHeight)"
}

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

    $portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $portProbe.Start()
    $monitorPort = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
    $portProbe.Stop()

    $qemuArgs = @(
        '-machine', 'q35',
        '-m', '4096',
        '-smp', '2',
        '-accel', 'tcg,thread=multi',
        '-drive', "if=pflash,format=raw,readonly=on,file=$qemuCodeLocal",
        '-drive', "if=pflash,format=raw,file=$qemuVars",
        '-drive', "file=$image,format=vdi,if=virtio",
        '-vga', 'none',
        '-device', "VGA,edid=on,xres=$expectedWidth,yres=$expectedHeight,refresh_rate=$expectedRefresh,vgamem_mb=64",
        '-display', 'sdl,gl=off',
        '-serial', "file:$serialLog",
        '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off",
        '-no-reboot',
        '-snapshot'
    )
    $qemuProcess = $null
    $testSucceeded = $false
    try {
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs `
            -WorkingDirectory $output -PassThru
        Write-Host "QEMU started (PID $($qemuProcess.Id)); waiting for the ${expectedWidth}x${expectedHeight}@${expectedRefresh}Hz UI..."
        [void](Wait-SerialReady -Path $serialLog -Process $qemuProcess `
            -TimeoutSeconds $BootSeconds)
        Write-Host "QEMU receiver/UI readiness passed. Serial log: $serialLog"

        # The first boot frame is decoded lazily.  Allow the 4.6 s boot movie
        # (and TCG's initial FFmpeg setup) to reach a visible frame before
        # taking the framebuffer sample.
        Start-Sleep -Seconds 8
        $dumped = $false
        $dumpResponse = ''
        Remove-Item -LiteralPath $qemuScreenshot -Force -ErrorAction SilentlyContinue
        for ($attempt = 0; $attempt -lt 10 -and -not $dumped; $attempt++) {
            try {
                $dumpResponse = Invoke-QemuMonitor -Port $monitorPort `
                    -Command 'screendump stearlight-vm.ppm'
            } catch {
                $dumpResponse = $_.Exception.Message
            }
            $dumped = Test-Path -LiteralPath $qemuScreenshot
            if (-not $dumped) { Start-Sleep -Milliseconds 500 }
        }
        if (-not $dumped) {
            throw "QEMU screendump failed: $dumpResponse"
        }
        Assert-VisibleScreenshot -Path $qemuScreenshot
        Write-Host "QEMU framebuffer screenshot: $qemuScreenshot"
        $testSucceeded = $true
    } finally {
        if ($qemuProcess -and (-not $KeepRunning -or -not $testSucceeded)) {
            try {
                if (-not $qemuProcess.HasExited) { $qemuProcess.Kill() }
                [void]$qemuProcess.WaitForExit(5000)
            } catch { }
            Write-Host 'QEMU stopped.'
        } elseif ($qemuProcess -and $KeepRunning -and $testSucceeded) {
            Write-Host "QEMU remains running (PID $($qemuProcess.Id))."
        }
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
Invoke-VBox setextradata $Name CustomVideoMode1 2880x1600x32
Invoke-VBox startvm $Name --type headless | Out-Null

[void](Wait-SerialReady -Path $serialLog -TimeoutSeconds $BootSeconds)
Invoke-VBox controlvm $Name screenshotpng $screenshot
Assert-VisibleScreenshot -Path $screenshot
Write-Host "Screenshot: $screenshot"
Write-Host "Serial log: $serialLog"

if (-not $KeepRunning) {
    Invoke-VBox controlvm $Name poweroff | Out-Null
    Invoke-VBox unregistervm $Name | Out-Null
    Remove-TestMachineFolder
}
