[CmdletBinding()]
param(
    [string]$ImagePath = (Join-Path $PSScriptRoot '..\out\stearlight-vm\stearlight-os-vm-x86_64.vdi'),
    [string]$Name = 'Stearlight-OS-Test',
    [int]$BootSeconds = 45,
    [int]$CaptureDelaySeconds = 8,
    [int]$MemoryMB = 3072,
    [switch]$MeasureFps,
    [switch]$SecondaryMonitor,
    [switch]$KeepRunning,
    [switch]$PersistDisk
)

$ErrorActionPreference = 'Stop'
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$image = (Resolve-Path -LiteralPath $ImagePath).Path
$output = Split-Path -Parent $image
$screenshot = Join-Path $output 'stearlight-vm.png'
$qemuScreenshot = Join-Path $output 'stearlight-vm.ppm'
$fpsScreenshot = Join-Path $output 'stearlight-vm-fps.ppm'
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
        # VM diagnostic mode uses an X11 scanout when gamescope cannot use a
        # software Vulkan presentation device.  It still guarantees the same
        # guest framebuffer dimensions and refresh rate, so accept its marker
        # as a display-ready result as well.
        $displayReady = $serial -match "(?i)(SVRT UI READY|STEARLIGHT GAMESCOPE READY|STEARLIGHT VM DISPLAY READY) $expectedWidth`x$expectedHeight @ $expectedRefresh`Hz"
        $steamSessionReady = $serial -match "(?i)STEARLIGHT STEAM SESSION STARTING"
        if ($displayReady -and $steamSessionReady) {
            return $serial
        }
        if ($Process -and $Process.HasExited) { break }
        Start-Sleep -Seconds 1
    }
    $exitCode = if ($Process -and $Process.HasExited) {
        $Process.ExitCode
    } else { 'timeout' }
    throw "VM did not report a ready gamescope/Steam session at ${expectedWidth}x${expectedHeight}@${expectedRefresh}Hz (code $exitCode). Serial log: $Path`n$serial"
}

function Invoke-QemuMonitor {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$DelayMilliseconds = 400
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
        if ($DelayMilliseconds -gt 0) {
            Start-Sleep -Milliseconds $DelayMilliseconds
        }
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

function Read-PpmFingerprint {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                                   [IO.FileAccess]::Read,
                                   [IO.FileShare]::ReadWrite)
    } catch {
        return $null
    }
    try {
        if ((Read-PpmToken -Stream $stream) -ne 'P6') { return $null }
        $width = 0; $height = 0; $maxValue = 0
        if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue) -or
            $width -ne $expectedWidth -or $height -ne $expectedHeight -or
            $maxValue -ne 255) { return $null }
        $pixelBytes = [int64]$width * [int64]$height * 3
        if ($stream.Length -lt $pixelBytes) { return $null }
        $pixelOffset = $stream.Length - $pixelBytes
        $sample = New-Object 'System.Collections.Generic.List[byte]'
        $pixel = New-Object byte[] 3
        # Sample a regular lattice across both eyes.  It is enough to detect
        # successive animation frames without hashing the complete 14 MB dump.
        for ($y = 160; $y -lt $height; $y += 160) {
            for ($x = 80; $x -lt $width; $x += 160) {
                [void]$stream.Seek($pixelOffset + (([int64]$y * $width + $x) * 3),
                                   [IO.SeekOrigin]::Begin)
                if ($stream.Read($pixel, 0, 3) -eq 3) {
                    [void]$sample.AddRange($pixel)
                }
            }
        }
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            return [BitConverter]::ToString($sha.ComputeHash($sample.ToArray())).Replace('-', '')
        }
        finally { $sha.Dispose() }
    } finally {
        $stream.Dispose()
    }
}

function Wait-QemuUiInitialized {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 45
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Read-SerialLog -Path $Path) -match '(?i)(SVRT UI INITIALIZED|STEARLIGHT GAMESCOPE READY|STEARLIGHT VM DISPLAY READY)') { return $true }
        if ($Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Measure-QemuBootFps {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Wait-QemuUiInitialized -Path $Path -Process $Process)) {
        throw 'QEMU UI did not initialize before the FPS measurement.'
    }
    Remove-Item -LiteralPath $fpsScreenshot -Force -ErrorAction SilentlyContinue
    $sampleCount = 0
    $changedCount = 0
    $lastFingerprint = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(4)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited) {
        try {
            $response = Invoke-QemuMonitor -Port $Port `
                -Command "screendump $fpsScreenshot" -DelayMilliseconds 25
            if (-not (Test-Path -LiteralPath $fpsScreenshot) -and $sampleCount -eq 0) {
                Write-Host "QEMU FPS screendump response: $response"
            }
        } catch { if ($sampleCount -eq 0) { Write-Host "QEMU FPS screendump error: $($_.Exception.Message)" } }
        $fingerprint = Read-PpmFingerprint -Path $fpsScreenshot
        if ($fingerprint) {
            $sampleCount++
            if ($lastFingerprint -and $fingerprint -ne $lastFingerprint)
                { $changedCount++ }
            $lastFingerprint = $fingerprint
        }
        Start-Sleep -Milliseconds 75
    }
    Write-Host "Boot animation frame samples: $sampleCount, changes: $changedCount"
    if ($sampleCount -lt 15 -or $changedCount -lt 12) {
        throw 'QEMU boot animation did not produce enough distinct frames.'
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
    $diskFormat = switch ([IO.Path]::GetExtension($image).ToLowerInvariant()) {
        '.raw' { 'raw'; break }
        '.img' { 'raw'; break }
        '.qcow2' { 'qcow2'; break }
        default { 'vdi' }
    }

    # QEMU's GTK backend keeps the non-client (title-bar/border) height from
    # its default 4:3 window when zoom-to-fit is enabled.  That leaves a large
    # empty strip above/below the 16:9 stereo framebuffer.  Resize the outer
    # window from its current width while preserving the guest aspect ratio;
    # the guest framebuffer itself remains 2880x1600.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class StearlightQemuWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out Rect rect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetClientRect(IntPtr hWnd, out Rect rect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int x,
        int y,
        int cx,
        int cy,
        uint flags);
}
'@

    function Set-QemuWindowAspect {
        param(
            [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
            [Parameter(Mandatory = $true)][int]$FramebufferWidth,
            [Parameter(Mandatory = $true)][int]$FramebufferHeight,
            [int]$TimeoutMilliseconds = 10000
        )

        $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            try { $Process.Refresh() } catch { return $false }
            $rawHandle = $Process.MainWindowHandle
            if ($rawHandle -and $rawHandle -ne 0) {
                $handle = [IntPtr]$rawHandle
                $outer = New-Object StearlightQemuWindow+Rect
                $client = New-Object StearlightQemuWindow+Rect
                if ([StearlightQemuWindow]::GetWindowRect($handle, [ref]$outer) -and
                    [StearlightQemuWindow]::GetClientRect($handle, [ref]$client)) {
                    $outerWidth = $outer.Right - $outer.Left
                    $outerHeight = $outer.Bottom - $outer.Top
                    $clientWidth = $client.Right - $client.Left
                    $clientHeight = $client.Bottom - $client.Top
                    if ($outerWidth -gt 0 -and $clientWidth -gt 0 -and
                        $clientHeight -gt 0) {
                        $nonClientHeight = $outerHeight - $clientHeight
                        $targetClientHeight = [Math]::Max(1, [int][Math]::Round(
                            $clientWidth * $FramebufferHeight / [double]$FramebufferWidth))
                        $targetOuterHeight = $targetClientHeight + $nonClientHeight
                        if ([Math]::Abs($targetOuterHeight - $outerHeight) -gt 1) {
                            $flags = [uint32]0x0004 -bor [uint32]0x0010 # SWP_NOZORDER | SWP_NOACTIVATE
                            if (-not [StearlightQemuWindow]::SetWindowPos(
                                    $handle, [IntPtr]::Zero, $outer.Left, $outer.Top,
                                    $outerWidth, $targetOuterHeight, $flags)) {
                                Write-Warning 'Could not resize the QEMU GTK window.'
                                return $false
                            }
                        }
                        Write-Host "QEMU window fit: $outerWidth`x$targetOuterHeight outer ($clientWidth`x$targetClientHeight guest area)"
                        return $true
                    }
                }
            }
            Start-Sleep -Milliseconds 200
        }
        Write-Warning 'QEMU window was not available for aspect-ratio fitting.'
        return $false
    }

    function Move-QemuWindowToSecondaryMonitor {
        param(
            [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process
        )

        if (-not $SecondaryMonitor) { return $false }
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $screens = [System.Windows.Forms.Screen]::AllScreens
            if ($screens.Count -lt 2) {
                Write-Warning 'A secondary monitor was requested, but only one display is available.'
                return $false
            }
            $Process.Refresh()
            $rawHandle = $Process.MainWindowHandle
            if (-not $rawHandle -or $rawHandle -eq 0) {
                Write-Warning 'QEMU window handle was not available for secondary-monitor placement.'
                return $false
            }
            $work = $screens[1].WorkingArea
            $handle = [IntPtr]$rawHandle
            $outer = New-Object StearlightQemuWindow+Rect
            if (-not [StearlightQemuWindow]::GetWindowRect($handle, [ref]$outer)) {
                return $false
            }
            $width = [Math]::Max(320, $outer.Right - $outer.Left)
            $height = [Math]::Max(240, $outer.Bottom - $outer.Top)
            $x = $work.Left + 20
            $y = $work.Top + 20
            $flags = [uint32]0x0004 -bor [uint32]0x0010 # SWP_NOZORDER | SWP_NOACTIVATE
            if (-not [StearlightQemuWindow]::SetWindowPos(
                    $handle, [IntPtr]::Zero, $x, $y, $width, $height, $flags)) {
                Write-Warning 'Could not move the QEMU GTK window to the secondary monitor.'
                return $false
            }
            Write-Host "QEMU window placed on secondary monitor at ${x},${y}."
            return $true
        } catch {
            Write-Warning "Secondary-monitor placement unavailable: $($_.Exception.Message)"
            return $false
        }
    }

    $portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $portProbe.Start()
    $monitorPort = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
    $portProbe.Stop()

    $qemuArgs = @(
        '-machine', 'q35',
        # Steam's 32-bit bootstrap, 64-bit WebHelper and lavapipe each keep
        # their own mapped runtime/LLVM images.  Four GiB exhausts the guest
        # before the Gamepad UI can create its Vulkan surface; keep the VM
        # generous while the host window remains scaled to the monitor.
        '-m', "$MemoryMB",
        '-smp', '4',
        # Prefer Windows Hypervisor Platform when it is enabled.  The second
        # accelerator keeps the test portable on machines where Hyper-V/WHPX
        # is unavailable, while multi-threaded TCG remains the fallback.
        '-accel', 'whpx',
        '-accel', 'tcg,thread=multi',
        '-cpu', 'max',
        '-drive', "if=pflash,format=raw,readonly=on,file=$qemuCodeLocal",
        '-drive', "if=pflash,format=raw,file=$qemuVars",
        # systemd-boot is read through the firmware SATA controller.  IDE is
        # deliberately used here because the minimal EFI path has no virtio
        # firmware driver; the guest kernel still exposes the fixed target
        # mode and framebuffer below.
        '-drive', "file=$image,format=$diskFormat,if=ide",
        # Steam's first-run client downloads the Gamepad UI/bootstrap payload.
        # Give the appliance a private user-mode NAT interface so this works
        # in a VM without exposing or depending on a host bridge.
        '-nic', 'user,model=e1000',
        # The classic VGA device is intentionally used for the software VM.
        # Unlike virtio-vga on hosts without GL/DMABUF support, it exposes a
        # deterministic 2880x1600 EDID mode.  gamescope still runs in its SDL
        # nested backend and its Wayland surface is the Steam session tested.
        '-vga', 'none',
        '-device', "VGA,xres=$expectedWidth,yres=$expectedHeight,vgamem_mb=32,edid=on",
        # Keep the guest scanout at 2880x1600, but let GTK scale it into a
        # normal desktop window instead of opening a 2880-pixel-wide host
        # window.  zoom-to-fit preserves the stereo aspect ratio.
        '-display', 'gtk,gl=on,zoom-to-fit=on,show-menubar=off,window-close=on',
        '-serial', "file:$serialLog",
        '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off",
        '-no-reboot'
    )
    if (-not $PersistDisk) {
        $qemuArgs += '-snapshot'
    }
    $qemuProcess = $null
    $testSucceeded = $false
    try {
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs `
            -WorkingDirectory $output -PassThru
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)
        Write-Host "QEMU started (PID $($qemuProcess.Id)); waiting for the ${expectedWidth}x${expectedHeight}@${expectedRefresh}Hz UI..."
        if ($MeasureFps) {
            Measure-QemuBootFps -Port $monitorPort -Process $qemuProcess `
                -Path $serialLog
        }
        [void](Wait-SerialReady -Path $serialLog -Process $qemuProcess `
            -TimeoutSeconds $BootSeconds)
        Write-Host "QEMU UI readiness passed. Serial log: $serialLog"

        # The guest mode switch can recreate the GTK drawing area.  Apply the
        # same correction once more after the exact display mode is ready.
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)

        # The first boot frame is decoded lazily.  Allow the 4.6 s boot movie
        # (and TCG's initial FFmpeg setup) to reach a visible frame before
        # taking the framebuffer sample.
        Start-Sleep -Seconds ([Math]::Max(0, $CaptureDelaySeconds))
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
        if ($qemuProcess -and -not $KeepRunning) {
            # A persistent qcow2 is useful for inspecting Steam's logs, but
            # killing QEMU while ext4 is mounted corrupts its journal (the
            # next run then drops into the initramfs recovery shell).  Ask the
            # guest to shut down first and only use Kill as a last resort.
            if ($PersistDisk -and -not $qemuProcess.HasExited) {
                try {
                    [void](Invoke-QemuMonitor -Port $monitorPort -Command 'system_powerdown' -DelayMilliseconds 250)
                } catch { }
                $shutdownDeadline = [DateTime]::UtcNow.AddSeconds(20)
                while (-not $qemuProcess.HasExited -and
                       [DateTime]::UtcNow -lt $shutdownDeadline) {
                    Start-Sleep -Milliseconds 250
                }
                if (-not $qemuProcess.HasExited) {
                    try {
                        [void](Invoke-QemuMonitor -Port $monitorPort -Command 'quit' -DelayMilliseconds 250)
                    } catch { }
                    [void]$qemuProcess.WaitForExit(5000)
                }
            }
            try {
                if (-not $qemuProcess.HasExited) { $qemuProcess.Kill() }
                [void]$qemuProcess.WaitForExit(5000)
            } catch { }
            Write-Host 'QEMU stopped.'
        } elseif ($qemuProcess -and $KeepRunning) {
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
