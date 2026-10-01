[CmdletBinding()]
param(
    [string]$ImagePath,
    [string]$OutputDirectory,
    [string]$SteamVRDirectory = $env:STEARLIGHT_STEAMVR_SOURCE,
    [string]$RecoveryImageUrl = $env:STEARLIGHT_FRAME_RECOVERY_URL,
    [string]$QemuExecutable = $env:STEARLIGHT_QEMU,
    [string]$QemuFirmwareDirectory = $env:QEMU_FIRMWARE_DIR,
    [switch]$Rebuild,
    [switch]$NoCache,
    # A clean Steam tree may spend about two minutes in Valve's bootstrap and
    # WebHelper startup before it maps the first Gamepad UI surface. Keep the
    # default long enough to test a true cold boot, not only a warm cache.
    [int]$BootSeconds = 300,
    # Allow SteamVR's dashboard-toggle input to reach the mirror before
    # capture. Override with zero only for an early-start diagnostic.
    [int]$CaptureDelaySeconds = 15,
    # The first SteamOS client update can finish after the compositor marker;
    # use the same bounded boot budget for the framebuffer wait instead of
    # declaring a healthy but still-updating client black after 120 seconds.
    [int]$VisibleTimeoutSeconds = 0,
    # Valve's 32-bit client, 64-bit WebHelper and SteamVR compositor share
    # this guest memory budget.
    [int]$MemoryMB = 4096,
    # Keep QEMU visible by default so a successful SteamOS session can be
    # inspected manually. Use -Headless for CI or framebuffer-only runs.
    [switch]$Headless,
    [switch]$SecondaryMonitor,
    [switch]$KeepRunning,
    [switch]$PersistDisk
)

$ErrorActionPreference = 'Stop'
$explicitImage = -not [string]::IsNullOrWhiteSpace($ImagePath)
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot 'build'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
if (-not $ImagePath) {
    $ImagePath = Join-Path $outputRoot 'stearlight-vm-x86_64.vdi'
}
if ($Rebuild -and $explicitImage) {
    throw '-Rebuild cannot be combined with a custom -ImagePath; rebuild into -OutputDirectory instead.'
}
if ($Rebuild -or -not [IO.File]::Exists($ImagePath)) {
    if ($explicitImage) {
        throw "VM image does not exist: $ImagePath"
    }
    $buildScript = Join-Path $PSScriptRoot 'build.ps1'
    $buildArguments = @('-OutputDirectory', $outputRoot)
    if ($SteamVRDirectory) { $buildArguments += @('-SteamVRDirectory', $SteamVRDirectory) }
    if ($RecoveryImageUrl) { $buildArguments += @('-RecoveryImageUrl', $RecoveryImageUrl) }
    if ($NoCache) { $buildArguments += '-NoCache' }
    & $buildScript @buildArguments
    if (-not [IO.File]::Exists($ImagePath)) {
        throw "VM builder completed without producing '$ImagePath'."
    }
}
$image = (Resolve-Path -LiteralPath $ImagePath).Path
$output = if ($OutputDirectory) {
    $outputRoot
} else {
    Split-Path -Parent $image
}
$qemuScreenshot = Join-Path $output 'stearlight-vm.ppm'
$serialLog = Join-Path $output 'stearlight-vm-serial.log'
$expectedWidth = 2880
$expectedHeight = 1600
$expectedRefresh = 60

function ConvertTo-ProcessArgumentList {
    param([string[]]$Arguments)

    $quotedArguments = foreach ($argument in $Arguments) {
        if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') {
            $argument
            continue
        }
        $escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
        $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
        '"' + $escaped + '"'
    }
    return $quotedArguments -join ' '
}

function Read-SerialLog {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    try {
        return Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
    } catch {
        return ''
    }
}

function Assert-NoNativeGpuFatal {
    $serial = Read-SerialLog -Path $serialLog
    if ($serial -match 'VUID-VkExportSemaphoreCreateInfo-handleTypes-01124') {
        throw "Valve compositor requires exportable Vulkan semaphore FDs, but this VM GPU does not provide them. Serial log: $serialLog"
    }
    if ($serial -match 'Fatal : VkResult is') {
        throw "Valve compositor exited after a fatal Vulkan error. Serial log: $serialLog"
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
        if ($serial -match '(?i)could not open user') {
            throw "stearlight SteamOS session reported a startup error. Serial log: $Path"
        }
        if ($serial -match '(?i)(Mounting root failed|initramfs emergency recovery shell|failed to mount .*/sysroot)') {
            throw "Stearlight root filesystem failed to mount; Steam/Gamescope cannot start. Serial log: $Path"
        }
        if ($serial -match '(?im)(syntax error near unexpected token|STEARLIGHT STEAM: client exited|Steam exited unexpectedly|first-boot setup did not complete)') {
            throw "Steam failed before the Gamescope session became ready. Serial log: $Path"
        }
        $expectedMode = '{0}x{1} @ {2}Hz' -f `
            $expectedWidth, $expectedHeight, $expectedRefresh
        $gamescopeReady = $serial -match ('(?i)STEARLIGHT GAMESCOPE READY ' +
            [regex]::Escape($expectedMode))
        $steamSessionReady = $serial -match '(?i)STEARLIGHT STEAM SESSION STARTING'
        if ($gamescopeReady -and $steamSessionReady) {
            return $serial
        }
        if ($Process -and $Process.HasExited) { break }
        Start-Sleep -Seconds 1
    }
    $exitCode = if ($Process -and $Process.HasExited) {
        $Process.ExitCode
    } else { 'timeout' }
    throw "QEMU did not start the SteamOS Gamescope session at $expectedWidth x $expectedHeight @ $expectedRefresh Hz (code $exitCode). Serial log: $Path"
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
        # QEMU's TCP HMP monitor uses the terminal line editor; carriage
        # return executes a command while line feed alone leaves it buffered.
        $bytes = [Text.Encoding]::ASCII.GetBytes("$Command`r")
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

function Wait-PpmComplete {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$TimeoutSeconds = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $pixelBytes = [int64]$expectedWidth * [int64]$expectedHeight * 3
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            try {
                # QEMU writes screendump asynchronously.  Sharing the read
                # handle is safe, but do not parse until the complete pixel
                # payload is present; otherwise a short PPM is mistaken for
                # a failed/black framebuffer while QEMU is still writing it.
                $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                                          [IO.FileAccess]::Read,
                                          [IO.FileShare]::ReadWrite)
                try {
                    if ($stream.Length -ge ($pixelBytes + 16)) {
                        $stream.Dispose()
                        if (Read-PpmFingerprint -Path $Path) { return $true }
                        continue
                    }
                } finally {
                    if ($stream) { $stream.Dispose() }
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Test-PpmVisible {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                                  [IO.FileAccess]::Read,
                                  [IO.FileShare]::ReadWrite)
    } catch {
        return $false
    }
    try {
        if ((Read-PpmToken -Stream $stream) -ne 'P6') { return $false }
        $width = 0; $height = 0; $maxValue = 0
        if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue) -or
            $width -ne $expectedWidth -or $height -ne $expectedHeight -or
            $maxValue -ne 255) { return $false }
        $pixelBytes = [int64]$width * [int64]$height * 3
        if ($stream.Length -lt $pixelBytes) { return $false }
        $pixelOffset = $stream.Length - $pixelBytes
        $pixel = New-Object byte[] 3
        for ($y = 0; $y -lt $height; $y += 64) {
            for ($x = 0; $x -lt $width; $x += 64) {
                [void]$stream.Seek($pixelOffset + (([int64]$y * $width + $x) * 3),
                                   [IO.SeekOrigin]::Begin)
                if ($stream.Read($pixel, 0, 3) -eq 3 -and
                    ([int]$pixel[0] + [int]$pixel[1] + [int]$pixel[2]) -gt 18) {
                    return $true
                }
            }
        }
        return $false
    } catch {
        return $false
    } finally {
        $stream.Dispose()
    }
}

function Wait-QemuVisibleScreenshot {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$TimeoutSeconds = 120
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            [void](Invoke-QemuMonitor -Port $Port -Command 'screendump stearlight-vm.ppm')
            if ((Wait-PpmComplete -Path $Path -TimeoutSeconds 5) -and
                (Test-PpmVisible -Path $Path)) {
                return $true
            }
        } catch { }
        Start-Sleep -Seconds 1
    }
    throw "QEMU framebuffer stayed black after Steam session start: $Path"
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
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                               [IO.FileAccess]::Read,
                               [IO.FileShare]::ReadWrite)
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
        Assert-NoNativeGpuFatal
        throw "VM framebuffer remained black ($visibleSamples visible samples). See $serialLog"
    }
    Write-Host "Framebuffer check: $visibleSamples visible samples ($expectedWidth`x$expectedHeight)"
}


function Assert-NativeSteamVrReady {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$TimeoutSeconds = 300
    )

    if (-not [IO.File]::Exists($Path)) {
        throw "Native SteamVR launch check requires the serial log: $Path"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        try {
            $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                                      [IO.FileAccess]::Read,
                                      [IO.FileShare]::ReadWrite)
            try {
                $reader = New-Object IO.StreamReader($stream)
                $serial = $reader.ReadToEnd()
                $reader.Dispose()
            } finally {
                $stream.Dispose()
            }
        } catch {
            $serial = ''
        }
        if ($serial -match '(?i)STEARLIGHT (NATIVE COMPONENTS|DASHBOARD INPUT) NOT READY') {
            throw "Valve compositor, dashboard, or synthetic controller input failed to initialize. Serial log: $Path"
        }
        if ($serial -match 'STEARLIGHT NATIVE COMPONENTS READY' -and
            $serial -match 'STEARLIGHT DASHBOARD INPUT READY') {
            Write-Host 'Valve compositor, dashboard, and synthetic system-button readiness markers found.'
            return
        }
        Assert-NoNativeGpuFatal
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Valve compositor/dashboard or synthetic dashboard input did not become ready.'
}

$qemuCommand = if ($QemuExecutable) {
    Get-Command $QemuExecutable -ErrorAction SilentlyContinue
} else {
    Get-Command 'qemu-system-x86_64.exe' -ErrorAction SilentlyContinue
}
if (-not $qemuCommand -and -not $QemuExecutable) {
    $qemuCommand = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
}
if (-not $qemuCommand) {
    throw 'qemu-system-x86_64 is required; add its installation directory to PATH or set STEARLIGHT_QEMU.'
}
$qemu = $qemuCommand.Source

$qemuImgCommand = Get-Command 'qemu-img.exe' -ErrorAction SilentlyContinue
if (-not $qemuImgCommand) {
    $qemuImgCommand = Get-Command 'qemu-img' -ErrorAction SilentlyContinue
}
$qemuImg = if ($qemuImgCommand) { $qemuImgCommand.Source } else { $null }
if (-not $qemuImg) {
    $siblingQemuImg = Join-Path (Split-Path -Parent $qemu) 'qemu-img.exe'
    if ([IO.File]::Exists($siblingQemuImg)) { $qemuImg = $siblingQemuImg }
}
if (-not $PersistDisk -and -not $qemuImg) {
    throw 'qemu-img is required to create the disposable writable overlay.'
}

$firmwareDirectories = @()
if ($QemuFirmwareDirectory) { $firmwareDirectories += $QemuFirmwareDirectory }
if ($env:QEMU_HOME) { $firmwareDirectories += (Join-Path $env:QEMU_HOME 'share') }
$qemuBinaryDirectory = Split-Path -Parent $qemu
$firmwareDirectories += @(
    (Join-Path $qemuBinaryDirectory 'share'),
    (Join-Path (Split-Path -Parent $qemuBinaryDirectory) 'share'),
    (Join-Path (Split-Path -Parent $qemuBinaryDirectory) 'share\qemu'),
    (Join-Path (Split-Path -Parent $qemuBinaryDirectory) 'share\qemu\firmware'))
$firmwareDirectories = @($firmwareDirectories | Select-Object -Unique)
$qemuCode = $null
$qemuVarsSource = $null
foreach ($firmwareDirectory in $firmwareDirectories) {
    if (-not $qemuCode) {
        $candidateCode = Join-Path $firmwareDirectory 'edk2-x86_64-code.fd'
        if ([IO.File]::Exists($candidateCode)) { $qemuCode = $candidateCode }
    }
    if (-not $qemuVarsSource) {
        foreach ($varsName in @('edk2-x86_64-vars.fd', 'edk2-i386-vars.fd')) {
            $candidateVars = Join-Path $firmwareDirectory $varsName
            if ([IO.File]::Exists($candidateVars)) {
                $qemuVarsSource = $candidateVars
                break
            }
        }
    }
}
if (-not $qemuCode -or -not $qemuVarsSource) {
    throw 'QEMU UEFI firmware was not found beside QEMU; set QEMU_FIRMWARE_DIR or -QemuFirmwareDirectory.'
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

    # QEMU's global -snapshot flag made the ext4 root appear read-only to
    # Steam's updater on this image.  Steam needs a writable home and package
    # directory while it completes its first client update.  Use an explicit
    # qcow2 copy-on-write overlay instead: the base VDI remains untouched, the
    # update can finish normally, and the overlay is removed after the VM has
    # shut down.  PersistDisk is the deliberate inspection mode and boots the
    # supplied image directly.
    $temporaryOverlay = $null
    $testImage = $image
    $testDiskFormat = $diskFormat
    if (-not $PersistDisk) {
        $temporaryOverlay = Join-Path $output ".stearlight-vm-overlay-$([Guid]::NewGuid().ToString('N')).qcow2"
        $outputRoot = [IO.Path]::GetFullPath($output).TrimEnd('\') + '\'
        $overlayFullPath = [IO.Path]::GetFullPath($temporaryOverlay)
        if (-not $overlayFullPath.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to create the disposable VM overlay outside the image output directory.'
        }
        & $qemuImg create -f qcow2 -F $diskFormat -b $image $temporaryOverlay
        if ($LASTEXITCODE -ne 0) {
            throw "qemu-img could not create the writable VM overlay: $temporaryOverlay"
        }
        $testImage = $temporaryOverlay
        $testDiskFormat = 'qcow2'
        Write-Host "QEMU writable overlay: $temporaryOverlay"
    }

    # Resize the host window while preserving the 16:9 guest aspect ratio;
    # the stereo framebuffer itself remains 2880x1600.
    # Keep the transient Add-Type output beside the image, and isolate it so
    # concurrent test runs cannot overwrite one another.
    $testTemp = Join-Path $output ".stearlight-test-temp-$([Guid]::NewGuid().ToString('N'))"
    [IO.Directory]::CreateDirectory($testTemp) | Out-Null
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $env:TEMP = $testTemp
    $env:TMP = $testTemp
    try {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class stearlightQemuWindow {
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
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        if ([IO.Directory]::Exists($testTemp)) {
            [IO.Directory]::Delete($testTemp, $true)
        }
    }

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
                $outer = New-Object stearlightQemuWindow+Rect
                $client = New-Object stearlightQemuWindow+Rect
                if ([stearlightQemuWindow]::GetWindowRect($handle, [ref]$outer) -and
                    [stearlightQemuWindow]::GetClientRect($handle, [ref]$client)) {
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
                            if (-not [stearlightQemuWindow]::SetWindowPos(
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
            $outer = New-Object stearlightQemuWindow+Rect
            if (-not [stearlightQemuWindow]::GetWindowRect($handle, [ref]$outer)) {
                return $false
            }
            $width = [Math]::Max(320, $outer.Right - $outer.Left)
            $height = [Math]::Max(240, $outer.Bottom - $outer.Top)
            $x = $work.Left + 20
            $y = $work.Top + 20
            $flags = [uint32]0x0004 -bor [uint32]0x0010 # SWP_NOZORDER | SWP_NOACTIVATE
            if (-not [stearlightQemuWindow]::SetWindowPos(
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

    $qemuVideoArgs = @(
        # The VGA-compatible Virtio-GPU gives UEFI an early scanout.  Linux
        # still uses virtio_gpu/Venus for the native Gamescope session.
        '-device', "virtio-vga-gl,venus=on,blob=on,hostmem=512M,xres=$expectedWidth,yres=$expectedHeight"
    )
    $qemuDisplay = if ($Headless) {
        'egl-headless,gl=on'
    } else {
        # The Windows QEMU bundle exposes SDL rather than GTK.  Virtio-GPU's
        # GL/Venus renderer also requires a GL-capable display backend.
        'sdl,gl=on,show-cursor=on'
    }
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
        '-drive', "file=$testImage,format=$testDiskFormat,if=ide",
        # Steam's first-run client downloads the Gamepad UI/bootstrap payload.
        # Give the appliance a private user-mode NAT interface so this works
        # in a VM without exposing or depending on a host bridge.
        '-nic', 'user,model=e1000',
        # Use an absolute tablet for the visible QEMU harness.  The default
        # PS/2 mouse is relative and requires pointer capture; the tablet
        # keeps host coordinates aligned with the curved Steam surface and
        # lets the user click without first grabbing the QEMU window.
        '-device', 'ich9-usb-ehci1,id=stearlight-usb',
        '-device', 'usb-tablet,bus=stearlight-usb.0'
    ) + $qemuVideoArgs + @(
        # Virtio-GPU/Venus is required for the native Gamescope and SteamVR
        # Vulkan compositors.
        # Keep the guest scanout at 2880x1600, but let GTK scale it into a
        # normal desktop window instead of opening a 2880-pixel-wide host
        # window.  zoom-to-fit preserves the stereo aspect ratio.
        '-display', $qemuDisplay,
        '-serial', "file:$serialLog",
        '-monitor', "tcp:127.0.0.1:$monitorPort,server=on,wait=off",
        '-no-reboot'
    )
    $qemuProcess = $null
    $testSucceeded = $false
    $qemuErrorLog = Join-Path $output 'stearlight-vm-qemu-stderr.log'
    try {
        $qemuStart = @{
            FilePath = $qemu
            ArgumentList = (ConvertTo-ProcessArgumentList $qemuArgs)
            WorkingDirectory = $output
            RedirectStandardError = $qemuErrorLog
            PassThru = $true
        }
        if ($Headless) { $qemuStart.WindowStyle = 'Hidden' }
        $qemuProcess = Start-Process @qemuStart
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)
        Write-Host "QEMU started (PID $($qemuProcess.Id)); waiting for the Gamescope Steam session..."
        [void](Wait-SerialReady -Path $serialLog -Process $qemuProcess `
            -TimeoutSeconds $BootSeconds)
        Write-Host "SteamOS Gamescope session started. Serial log: $serialLog"

        # The guest mode switch can recreate the GTK drawing area.  Apply the
        # same correction once more after the exact display mode is ready.
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)

        Assert-NativeSteamVrReady -Path $serialLog -TimeoutSeconds $BootSeconds
        # Allow the fake controller's system-button and onboarding-select
        # inputs to reach the native dashboard before capturing the mirror.
        Start-Sleep -Seconds ([Math]::Max(0, $CaptureDelaySeconds))
        $visibleTimeout = if ($VisibleTimeoutSeconds -gt 0) {
            $VisibleTimeoutSeconds
        } else {
            [Math]::Max(120, $BootSeconds)
        }
        [void](Wait-QemuVisibleScreenshot -Port $monitorPort -Path $qemuScreenshot `
            -TimeoutSeconds $visibleTimeout)
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
        if ($temporaryOverlay -and -not $KeepRunning) {
            Remove-Item -LiteralPath $temporaryOverlay -Force -ErrorAction SilentlyContinue
            Write-Host 'QEMU writable overlay removed.'
        }
    }
return
