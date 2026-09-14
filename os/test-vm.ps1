[CmdletBinding()]
param(
    [string]$ImagePath = (Join-Path $PSScriptRoot '..\out\stearlight-vm\stearlight-os-vm-x86_64.vdi'),
    [string]$Name = 'Stearlight-OS-Test',
    # A clean Steam tree may spend about two minutes in Valve's bootstrap and
    # WebHelper startup before it maps the first Gamepad UI surface. Keep the
    # default long enough to test a true cold boot, not only a warm cache.
    [int]$BootSeconds = 300,
    # Take the screenshot only after the C bridge has captured a real Steam
    # window and WebHelper has had time to paint its first page. Override with
    # zero only when intentionally testing the transition frame.
    [int]$CaptureDelaySeconds = 15,
    # The first SteamOS client update can finish after the compositor marker;
    # use the same bounded boot budget for the framebuffer wait instead of
    # declaring a healthy but still-updating client black after 120 seconds.
    [int]$VisibleTimeoutSeconds = 0,
    # Valve's 32-bit bootstrap, 64-bit WebHelper and software compositor
    # need the same 4 GiB budget used by the repeatability smoke test.
    [int]$MemoryMB = 4096,
    [ValidateSet('vga', 'virtio-gl')]
    [string]$QemuGpu = 'vga',
    # Keep QEMU visible by default so a successful SteamOS session can be
    # inspected manually. Use -Headless for CI or framebuffer-only runs.
    [switch]$Headless,
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
        if ($serial -match '(?im)(syntax error near unexpected token|STEARLIGHT STEAM: client exited|Steam exited unexpectedly|first-boot setup did not complete)') {
            throw "Steam failed before producing a frame. Serial log: $Path`n$serial"
        }
        # Both the real Gamescope session and the legacy Weston diagnostic
        # guarantee the same guest framebuffer dimensions and refresh rate.
        $displayReady = $serial -match "(?i)(SVRT UI READY|STEARLIGHT GAMESCOPE READY|STEARLIGHT VM DISPLAY READY) $expectedWidth`x$expectedHeight @ $expectedRefresh`Hz"
        # The standalone shell announces a captured Steam frame. Gamescope
        # owns the output directly, so its ready marker is followed by a
        # framebuffer visibility poll before the screenshot is accepted.
        $steamSessionReady = $serial -match "(?i)(STEARLIGHT STEAM SESSION STARTING|STEARLIGHT STEAM SHELL STARTING)"
        $steamFrameReady = $serial -match '(?i)STEARLIGHT STEAM FRAME READY [0-9]+x[0-9]+'
        $gamescopeSessionReady = $serial -match '(?i)STEARLIGHT GAMESCOPE READY [0-9]+x[0-9]+ @ [0-9]+Hz'
        if ($displayReady -and $steamSessionReady -and
            ($steamFrameReady -or $gamescopeSessionReady)) {
            return $serial
        }
        if ($Process -and $Process.HasExited) { break }
        Start-Sleep -Seconds 1
    }
    $exitCode = if ($Process -and $Process.HasExited) {
        $Process.ExitCode
    } else { 'timeout' }
    throw "VM did not report a captured Steam frame at ${expectedWidth}x${expectedHeight}@${expectedRefresh}Hz (code $exitCode). Serial log: $Path`n$serial"
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

function Wait-QemuUiInitialized {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 45
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Read-SerialLog -Path $Path) -match '(?i)(SVRT UI INITIALIZED|STEARLIGHT GAMESCOPE READY)') { return $true }
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
        throw "VM framebuffer remained black ($visibleSamples visible samples). See $serialLog"
    }
    Write-Host "Framebuffer check: $visibleSamples visible samples ($expectedWidth`x$expectedHeight)"
}

function Assert-StereoScreenshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                               [IO.FileAccess]::Read,
                               [IO.FileShare]::ReadWrite)
    try {
        if ((Read-PpmToken -Stream $stream) -ne 'P6') {
            throw "Stereo check requires a binary PPM screenshot: $Path"
        }
        $width = 0; $height = 0; $maxValue = 0
        if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue) -or
            $width -ne $expectedWidth -or $height -ne $expectedHeight -or
            $maxValue -ne 255) {
            throw "Stereo check received a ${width}x${height} PPM; expected ${expectedWidth}x${expectedHeight}."
        }
        $pixelBytes = [int64]$width * [int64]$height * 3
        if ($stream.Length -lt $pixelBytes) { throw "Stereo screenshot is truncated: $Path" }
        $pixelOffset = $stream.Length - $pixelBytes
        $half = [int]($width / 2)
        $leftSamples = 0; $rightSamples = 0
        $pixel = New-Object byte[] 3
        for ($y = 0; $y -lt $height; $y += 64) {
            for ($x = 0; $x -lt $half; $x += 64) {
                [void]$stream.Seek($pixelOffset + (([int64]$y * $width + $x) * 3),
                                   [IO.SeekOrigin]::Begin)
                if ($stream.Read($pixel, 0, 3) -eq 3 -and
                    ([int]$pixel[0] + [int]$pixel[1] + [int]$pixel[2]) -gt 18) {
                    $leftSamples++
                }
                $rightX = $x + $half
                [void]$stream.Seek($pixelOffset + (([int64]$y * $width + $rightX) * 3),
                                   [IO.SeekOrigin]::Begin)
                if ($stream.Read($pixel, 0, 3) -eq 3 -and
                    ([int]$pixel[0] + [int]$pixel[1] + [int]$pixel[2]) -gt 18) {
                    $rightSamples++
                }
            }
        }
        if ($leftSamples -lt 1 -or $rightSamples -lt 1) {
            throw "Stereo framebuffer is incomplete (left $leftSamples, right $rightSamples samples)."
        }
        Write-Host "Stereo check: left $leftSamples, right $rightSamples visible samples"
    } finally {
        $stream.Dispose()
    }
}

function Assert-CentralSteamSurface {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                               [IO.FileAccess]::Read,
                               [IO.FileShare]::ReadWrite)
    try {
        if ((Read-PpmToken -Stream $stream) -ne 'P6') {
            throw "Steam surface check requires a binary PPM screenshot: $Path"
        }
        $width = 0; $height = 0; $maxValue = 0
        if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue) -or
            $width -ne $expectedWidth -or $height -ne $expectedHeight -or
            $maxValue -ne 255) {
            throw "Steam surface check received a ${width}x${height} PPM; expected ${expectedWidth}x${expectedHeight}."
        }
        $pixelBytes = [int64]$width * [int64]$height * 3
        if ($stream.Length -lt $pixelBytes) {
            throw "Steam surface screenshot is truncated: $Path"
        }
        $pixelOffset = $stream.Length - $pixelBytes
        $half = [int]($width / 2)
        $leftSamples = 0; $rightSamples = 0
        $pixel = New-Object byte[] 3
        for ($eye = 0; $eye -lt 2; ++$eye) {
            $firstX = $eye * $half + [int]($half * 0.12)
            $lastX = $eye * $half + [int]($half * 0.88)
            # The lower navigation bar is shell chrome and must not satisfy
            # this check. Sample only the upper/central Steam surface, where
            # Valve's WebHelper page and welcome text are rendered.
            for ($y = [int]($height * 0.12); $y -lt [int]($height * 0.68); $y += 8) {
                for ($x = $firstX; $x -lt $lastX; $x += 8) {
                    $position = $pixelOffset + (([int64]$y * $width + $x) * 3)
                    [void]$stream.Seek($position, [IO.SeekOrigin]::Begin)
                    if ($stream.Read($pixel, 0, 3) -eq 3 -and
                        [Math]::Max($pixel[0], [Math]::Max($pixel[1], $pixel[2])) -gt 96) {
                        if ($eye -eq 0) { $leftSamples++ } else { $rightSamples++ }
                    }
                }
            }
        }
        if ($leftSamples -lt 20 -or $rightSamples -lt 20) {
            throw "Central Steam surface is missing (left $leftSamples, right $rightSamples samples)."
        }
        Write-Host "Central Steam surface check: left $leftSamples, right $rightSamples visible samples"
    } finally {
        $stream.Dispose()
    }
}

function Assert-LaserCursor {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
                               [IO.FileAccess]::Read,
                               [IO.FileShare]::ReadWrite)
    try {
        if ((Read-PpmToken -Stream $stream) -ne 'P6') {
            throw "Laser cursor check requires a binary PPM screenshot: $Path"
        }
        $width = 0; $height = 0; $maxValue = 0
        if (-not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$width) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$height) -or
            -not [int]::TryParse((Read-PpmToken -Stream $stream), [ref]$maxValue) -or
            $width -ne $expectedWidth -or $height -ne $expectedHeight -or
            $maxValue -ne 255) {
            throw "Laser cursor check received a ${width}x${height} PPM; expected ${expectedWidth}x${expectedHeight}."
        }
        $pixelBytes = [int64]$width * [int64]$height * 3
        if ($stream.Length -lt $pixelBytes) {
            throw "Laser cursor screenshot is truncated: $Path"
        }
        $pixelOffset = $stream.Length - $pixelBytes
        $half = [int]($width / 2)
        $eyeSamples = @(0, 0)
        $pixel = New-Object byte[] 3
        # The laser starts near the lower-left of every eye. Scan that small
        # origin region instead of relying on the current pointer target,
        # which may be different when a human is inspecting the VM.
        for ($eye = 0; $eye -lt 2; $eye++) {
            $firstX = $eye * $half
            $lastX = $firstX + [int]($half * 0.15)
            for ($y = [int]($height * 0.72); $y -lt [int]($height * 0.99); $y += 4) {
                for ($x = $firstX; $x -lt $lastX; $x += 4) {
                    $position = $pixelOffset + (([int64]$y * $width + $x) * 3)
                    [void]$stream.Seek($position, [IO.SeekOrigin]::Begin)
                    if ($stream.Read($pixel, 0, 3) -ne 3) { continue }
                    $red = [int]$pixel[0]
                    $green = [int]$pixel[1]
                    $blue = [int]$pixel[2]
                    if ($blue -gt 150 -and $green -gt 80 -and
                        $blue -gt ($red + 60)) {
                        $eyeSamples[$eye]++
                    }
                }
            }
        }
        if ($eyeSamples[0] -lt 3 -or $eyeSamples[1] -lt 3) {
            throw "Blue laser cursor is missing (left $($eyeSamples[0]), right $($eyeSamples[1]) samples)."
        }
        Write-Host "Laser cursor check: left $($eyeSamples[0]), right $($eyeSamples[1]) blue samples"
    } finally {
        $stream.Dispose()
    }
}

# QEMU is the supported fallback on machines without VirtualBox.  Keep all
# firmware, logs and temporary state beside the VM image so a test never uses
# the system drive for VM data.
if (-not [IO.File]::Exists($vbox)) {
    $qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
    $qemuImg = 'C:\Program Files\qemu\qemu-img.exe'
    $qemuCode = 'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
    $qemuVarsSource = 'C:\Program Files\qemu\share\edk2-i386-vars.fd'
    if (-not [IO.File]::Exists($qemu)) {
        throw 'Neither VirtualBox 7.x nor QEMU was found. Install QEMU to run the VM test.'
    }
    if (-not $PersistDisk -and -not [IO.File]::Exists($qemuImg)) {
        throw 'qemu-img.exe is required for the disposable writable VM overlay.'
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
        $temporaryOverlay = Join-Path $output 'stearlight-vm-overlay.qcow2'
        $outputRoot = [IO.Path]::GetFullPath($output).TrimEnd('\') + '\'
        $overlayFullPath = [IO.Path]::GetFullPath($temporaryOverlay)
        if (-not $overlayFullPath.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to create the disposable VM overlay outside the image output directory.'
        }
        Remove-Item -LiteralPath $temporaryOverlay -Force -ErrorAction SilentlyContinue
        & $qemuImg create -f qcow2 -F $diskFormat -b $image $temporaryOverlay
        if ($LASTEXITCODE -ne 0) {
            throw "qemu-img could not create the writable VM overlay: $temporaryOverlay"
        }
        $testImage = $temporaryOverlay
        $testDiskFormat = 'qcow2'
        Write-Host "QEMU writable overlay: $temporaryOverlay"
    }

    # QEMU's GTK backend keeps the non-client (title-bar/border) height from
    # its default 4:3 window when zoom-to-fit is enabled.  That leaves a large
    # empty strip above/below the 16:9 stereo framebuffer.  Resize the outer
    # window from its current width while preserving the guest aspect ratio;
    # the guest framebuffer itself remains 2880x1600.
    # PowerShell Add-Type uses %TEMP% for its compiler output. Keep that
    # transient assembly beside the image so a nearly-full C: cannot prevent
    # an F:-backed VM test from starting.
    $testTemp = Join-Path $output '.stearlight-test-temp'
    [IO.Directory]::CreateDirectory($testTemp) | Out-Null
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $env:TEMP = $testTemp
    $env:TMP = $testTemp
    try {
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
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
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

    $qemuVideoArgs = if ($QemuGpu -eq 'virtio-gl') {
        @(
            '-vga', 'none',
            '-device', "virtio-gpu-gl-pci,venus=on,blob=on,hostmem=512M,xres=$expectedWidth,yres=$expectedHeight"
        )
    } else {
        @(
            '-vga', 'none',
            '-device', "VGA,xres=$expectedWidth,yres=$expectedHeight,vgamem_mb=32,edid=on"
        )
    }
    $qemuDisplay = if ($Headless) {
        'none'
    } else {
        'gtk,gl=on,zoom-to-fit=on,show-menubar=off,window-close=on'
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
        # VGA is the deterministic fallback.  virtio-gl is an optional host
        # path for QEMU builds that can provide Venus/DRM to the guest.
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
    try {
        $qemuProcess = Start-Process -FilePath $qemu -ArgumentList $qemuArgs `
            -WorkingDirectory $output -PassThru
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)
        Write-Host "QEMU started (PID $($qemuProcess.Id)); waiting for the Steam frame..."
        if ($MeasureFps) {
            Measure-QemuBootFps -Port $monitorPort -Process $qemuProcess `
                -Path $serialLog
        }
        [void](Wait-SerialReady -Path $serialLog -Process $qemuProcess `
            -TimeoutSeconds $BootSeconds)
        Write-Host "Steam frame readiness passed. Serial log: $serialLog"

        # The guest mode switch can recreate the GTK drawing area.  Apply the
        # same correction once more after the exact display mode is ready.
        [void](Set-QemuWindowAspect -Process $qemuProcess `
            -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
        [void](Move-QemuWindowToSecondaryMonitor -Process $qemuProcess)

        # For Gamescope, the serial marker means the compositor and Steam have
        # started but does not itself mean Steam has presented a frame. Poll
        # the guest framebuffer until a visible image exists; only then take
        # the one final screenshot used by the assertions below.
        Start-Sleep -Seconds ([Math]::Max(0, $CaptureDelaySeconds))
        $visibleTimeout = if ($VisibleTimeoutSeconds -gt 0) {
            $VisibleTimeoutSeconds
        } else {
            [Math]::Max(120, $BootSeconds)
        }
        [void](Wait-QemuVisibleScreenshot -Port $monitorPort -Path $qemuScreenshot `
            -TimeoutSeconds $visibleTimeout)
        Assert-VisibleScreenshot -Path $qemuScreenshot
        Assert-StereoScreenshot -Path $qemuScreenshot
        Assert-CentralSteamSurface -Path $qemuScreenshot
        Assert-LaserCursor -Path $qemuScreenshot
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
