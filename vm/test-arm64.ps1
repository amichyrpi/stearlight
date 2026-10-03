[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [Parameter(Mandatory = $true)] [string]$QemuDirectory,
    [ValidateRange(2048, 8192)] [int]$MemoryMegabytes = 4096,
    [ValidateRange(1, 12)] [int]$CpuCount = 6,
    [switch]$SecondaryMonitor,
    [ValidateRange(0, 65535)] [int]$DebugConsolePort = 0,
    [string]$DiskPath,
    [ValidateSet('raw', 'qcow2')] [string]$DiskFormat = 'raw',
    [string]$SteamClientArchiveTar,
    [string]$ScreenshotToolsTar,
    [switch]$PreserveDiskChanges,
    [switch]$KeepRunning,
    [switch]$CaptureOnVRHTMLInvalid,
    [switch]$AnimationPreview,
    [switch]$GamescopePreview,
    [switch]$WindowsGpuBridge,
    [switch]$LowResolution = $true
)

$ErrorActionPreference = 'Stop'
if ($AnimationPreview -and $GamescopePreview) { throw 'Choose one preview mode.' }
$displayPreview = $AnimationPreview -or $GamescopePreview
if ($WindowsGpuBridge -and -not $displayPreview -and
    -not $PSBoundParameters.ContainsKey('KeepRunning')) {
    # ARM emulation can finish the native dashboard after the boot capture.
    # Leave the full session available for inspection unless explicitly closed.
    $KeepRunning = $true
}
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$qemuDir = (Resolve-Path -LiteralPath $QemuDirectory).Path
foreach ($path in @($output, $qemuDir)) {
    if ($path -notmatch '^[FG]:\\') { throw "VM test files must be on F: or G: $path" }
}
$qemu = Join-Path $qemuDir 'qemu-system-aarch64.exe'
$kernel = Join-Path $output 'vmlinuz-virt'
$initramfs = Join-Path $output 'initramfs-virt'
$disk = if ($DiskPath) { (Resolve-Path -LiteralPath $DiskPath).Path } else { Join-Path $output 'stearlight-arm64.ext4' }
$steamClientTar = if ($SteamClientArchiveTar) {
    (Resolve-Path -LiteralPath $SteamClientArchiveTar).Path
} else {
    Join-Path $output 'steam-client.tar'
}
$screenshotTools = if ($ScreenshotToolsTar) {
    (Resolve-Path -LiteralPath $ScreenshotToolsTar).Path
} else {
    $defaultScreenshotTools = Join-Path $output 'runtime-extras.tar'
    if (Test-Path -LiteralPath $defaultScreenshotTools) { (Resolve-Path -LiteralPath $defaultScreenshotTools).Path } else { $null }
}
foreach ($path in @($qemu, $kernel, $initramfs, $disk, $steamClientTar, $screenshotTools)) {
    if (-not $path) { continue }
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing VM input: $path" }
}
if ($disk -notmatch '^[FG]:\\' -or $steamClientTar -notmatch '^[FG]:\\' -or
    ($screenshotTools -and $screenshotTools -notmatch '^[FG]:\\')) {
    throw 'VM disk and archives must be on F: or G:.'
}
$serial = Join-Path $output 'serial-test.log'
$stderr = Join-Path $output 'qemu-stderr.log'
$screenshot = Join-Path $output 'stearlight-arm64.png'
$expectedWidth = 2880
$expectedHeight = 1600
if ($LowResolution) { $expectedWidth = 1440; $expectedHeight = 800 }
$firmware = Join-Path $qemuDir 'share'
if (-not (Test-Path -LiteralPath $firmware)) {
    $firmware = Join-Path (Split-Path $qemuDir -Parent) 'qemu\share'
}
if (-not (Test-Path -LiteralPath $firmware)) { throw 'QEMU firmware share directory is missing.' }
Set-Content -LiteralPath $serial -Value ''
$shared = $output.Replace('\', '/')
$diskQemu = $disk.Replace('\', '/')
$steamTarQemu = $steamClientTar.Replace('\', '/')
$screenshotToolsQemu = if ($screenshotTools) { $screenshotTools.Replace('\', '/') } else { $null }
# Match test-vm.ps1 at e35df19: GTK owns zoom-to-fit, aspect-preserving
# scaling and the minimum window size (one quarter of the guest surface).
# Do not fall back to SDL: its sizing and minimum-size behavior differ.
$displayBackends = & $qemu -display help 2>&1
if ($LASTEXITCODE -ne 0 -or $displayBackends -notcontains 'gtk') {
    throw 'The original x86 window requires a GTK-enabled ARM64 QEMU build. Select that build with -QemuDirectory.'
}
$qemuDisplay = 'gtk,gl=on,zoom-to-fit=on,show-menubar=off,window-close=on,refresh-rate=60000'
Write-Verbose "QEMU display backend: gtk (original x86 window); guest mode ${expectedWidth}x${expectedHeight}"
$kernelCommandLine = 'console=ttyAMA0,115200 root=/dev/vda rootfstype=ext4 rw quiet loglevel=0 logo.nologo vt.global_cursor_default=0 rd.udev.log_priority=0 stearlight.capture=1'
if ($AnimationPreview) {
    $kernelCommandLine += ' stearlight.animations=1'
    $screenshot = Join-Path $output 'stearlight-animation-preview.png'
}
if ($GamescopePreview) {
    $kernelCommandLine += ' stearlight.gamescope-preview=1'
    $screenshot = Join-Path $output 'stearlight-gamescope-preview.png'
}
if ($WindowsGpuBridge) {
    $kernelCommandLine += ' stearlight.windows-gpu=1'
}
if ($LowResolution) { $kernelCommandLine += ' stearlight.low-resolution=1' }
if ($DebugConsolePort) { $kernelCommandLine += ' stearlight.debug=1' }
$args = @(
    '-L', $firmware.Replace('\', '/'),
    '-machine', 'virt', '-accel', 'tcg,thread=multi',
    # The VM needs NEON; emulated SVE/SME adds vector and context-switch work.
    '-cpu', 'max,sve=off,sme=off', '-smp', [string]$CpuCount, '-m', [string]$MemoryMegabytes,
    '-display', $qemuDisplay,
    '-device', "virtio-gpu-gl-pci,blob=true,hostmem=1G,venus=true,xres=$expectedWidth,yres=$expectedHeight",
    '-device', 'qemu-xhci',
    '-device', 'usb-kbd',
    '-device', 'usb-tablet',
    '-netdev', 'user,id=net0', '-device', 'virtio-net-device,netdev=net0',
    '-monitor', 'none',
    '-serial', "file:$shared/serial-test.log",
    '-kernel', $kernel, '-initrd', $initramfs,
    '-append', ('"' + $kernelCommandLine + '"'),
    '-drive'
)
$rootDrive = "file=$diskQemu,format=$DiskFormat,if=virtio"
if (-not $PreserveDiskChanges) { $rootDrive += ',snapshot=on' }
$args += @(
    $rootDrive,
    '-drive', "file=$steamTarQemu,format=raw,if=virtio,readonly=on"
)
if ($screenshotToolsQemu) {
    $args += @('-drive', "file=$screenshotToolsQemu,format=raw,if=virtio,readonly=on")
}
if ($DebugConsolePort) {
    $args += @('-device', 'virtio-serial-device',
        '-chardev', "socket,id=debug,host=127.0.0.1,port=$DebugConsolePort,server=on,wait=off",
        '-device', 'virtconsole,chardev=debug')
}
$qemuTemp = Join-Path $output 'qemu-temp'
New-Item -ItemType Directory -Force -Path $qemuTemp | Out-Null
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldTmpDir = $env:TMPDIR
$oldImageExport = $env:STEARLIGHT_VULKAN_IMAGE_EXPORT
$oldVirglLog = $env:VIRGL_LOG_FILE
try {
    $env:TEMP = $qemuTemp
    $env:TMP = $qemuTemp
    $env:TMPDIR = $qemuTemp
    if ($WindowsGpuBridge) {
        $env:STEARLIGHT_VULKAN_IMAGE_EXPORT = '1'
        $env:VIRGL_LOG_FILE = (Join-Path $output 'windows-venus.log').Replace('\', '/')
    }
    $process = Start-Process -FilePath $qemu -ArgumentList $args -PassThru `
        -WorkingDirectory $output -WindowStyle Normal -RedirectStandardError $stderr
    Write-Host "Windows QEMU started: $qemu"
    Write-Host "Waiting for the Frame display. Log: $serial"
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:TMPDIR = $oldTmpDir
    $env:STEARLIGHT_VULKAN_IMAGE_EXPORT = $oldImageExport
    $env:VIRGL_LOG_FILE = $oldVirglLog
}
try {
    if (-not ([System.Management.Automation.PSTypeName]'StearlightArm64QemuWindow').Type) {
        $previousTemp = $env:TEMP
        $previousTmp = $env:TMP
        try {
        $env:TEMP = $qemuTemp
        $env:TMP = $qemuTemp
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class StearlightArm64QemuWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);
}
'@
        } finally {
            $env:TEMP = $previousTemp
            $env:TMP = $previousTmp
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
                $outer = New-Object StearlightArm64QemuWindow+Rect
                $client = New-Object StearlightArm64QemuWindow+Rect
                if ([StearlightArm64QemuWindow]::GetWindowRect($handle, [ref]$outer) -and
                    [StearlightArm64QemuWindow]::GetClientRect($handle, [ref]$client)) {
                    $outerWidth = $outer.Right - $outer.Left
                    $outerHeight = $outer.Bottom - $outer.Top
                    $clientWidth = $client.Right - $client.Left
                    $clientHeight = $client.Bottom - $client.Top
                    if ($outerWidth -gt 0 -and $clientWidth -gt 0 -and $clientHeight -gt 0) {
                        # Preserve GTK's chosen width, exactly as the original
                        # x86 tester does; only correct the client height.
                        $nonClientHeight = $outerHeight - $clientHeight
                        $targetClientHeight = [Math]::Max(1, [int][Math]::Round(
                            $clientWidth * $FramebufferHeight / [double]$FramebufferWidth))
                        $targetOuterHeight = $targetClientHeight + $nonClientHeight
                        if ([Math]::Abs($targetOuterHeight - $outerHeight) -gt 1) {
                            $flags = [uint32]0x0004 -bor [uint32]0x0010 # SWP_NOZORDER | SWP_NOACTIVATE
                            if (-not [StearlightArm64QemuWindow]::SetWindowPos(
                                    $handle, [IntPtr]::Zero, $outer.Left, $outer.Top,
                                    $outerWidth, $targetOuterHeight, $flags)) {
                                Write-Warning 'Could not resize the QEMU display window.'
                                return $false
                            }
                        }
                        Write-Verbose "QEMU window fit: $outerWidth`x$targetOuterHeight outer ($clientWidth`x$targetClientHeight guest area)"
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
        param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)

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
            $outer = New-Object StearlightArm64QemuWindow+Rect
            if (-not [StearlightArm64QemuWindow]::GetWindowRect($handle, [ref]$outer)) {
                return $false
            }
            $width = [Math]::Max(320, $outer.Right - $outer.Left)
            $height = [Math]::Max(240, $outer.Bottom - $outer.Top)
            $x = $work.Left + 20
            $y = $work.Top + 20
            $flags = [uint32]0x0004 -bor [uint32]0x0010
            if (-not [StearlightArm64QemuWindow]::SetWindowPos(
                    $handle, [IntPtr]::Zero, $x, $y, $width, $height, $flags)) {
                Write-Warning 'Could not move the QEMU GTK window to the secondary monitor.'
                return $false
            }
            Write-Verbose "QEMU window placed on secondary monitor at ${x},${y}."
            return $true
        } catch {
            Write-Warning "Secondary-monitor placement unavailable: $($_.Exception.Message)"
            return $false
        }
    }

    [void](Set-QemuWindowAspect -Process $process `
        -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
    [void](Move-QemuWindowToSecondaryMonitor -Process $process)
    $deadline = (Get-Date).AddSeconds(1200)
    if ($displayPreview) { $deadline = (Get-Date).AddSeconds(240) }
    $startedAt = $null
    $nativeFailureAt = $null
    $captureReadyAt = $null
    $screenshotReady = $false
    $vrHtmlInvalidReported = $false
    $gamescopeWindowFitted = $false
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) { throw "QEMU exited before a screenshot. See $serial and $stderr" }
        if (Test-Path -LiteralPath $serial) {
            $log = Get-Content -LiteralPath $serial -Raw
            if ($AnimationPreview -and $log -match 'STEARLIGHT ARM64 ANIMATION (STARTUP|PLAYBACK) FAILED') {
                throw "The boot/loading animation failed. See $serial"
            }
            if ($AnimationPreview -and $log -match 'STEARLIGHT ARM64 ANIMATION PREVIEW READY') {
                $startedAt = Get-Date
                $screenshotReady = $true
                [void](Set-QemuWindowAspect -Process $process `
                    -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
                break
            }
            if ($GamescopePreview -and $log -match 'STEARLIGHT ARM64 GAMESCOPE PREVIEW READY') {
                $startedAt = Get-Date
                $screenshotReady = $true
                [void](Set-QemuWindowAspect -Process $process `
                    -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
                break
            }
            if ($log -match 'STEARLIGHT ARM64 (XORG (FAILED|DISPLAY MODE FAILED)|GAMESCOPE STARTUP FAILED)') {
                throw "The graphical display failed to start. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 AUDIO (SERVER|ENDPOINT) FAILED') {
                throw "The VM audio service failed before Frame startup. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 GNU LIBRARY CACHE FAILED') {
                throw "The GNU runtime library cache could not be refreshed. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 VRHTML VM UPLOAD FAILED') {
                throw "The VRHTML upload adaptation does not match this recovery runtime. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 STEAM IPC PREPARATION FAILED') {
                throw "The Steam IPC timeout adaptation does not match this recovery client. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 STEAMVR COMPOSITOR FAILED') {
                throw "Valve's compositor failed before it could present the dashboard. See $serial"
            }
            if ($log -match 'STEARLIGHT ARM64 STEAMVR EXITED BEFORE STEAM') {
                throw "Steam Frame's VR runtime failed before Steam startup. See $serial"
            }
            # Match test-vm.ps1: fit at launch and again as soon as the
            # Gamescope mode is ready, before waiting for Steam or SteamVR.
            if (-not $gamescopeWindowFitted -and
                $log -match 'STEARLIGHT GAMESCOPE READY 1920x1080 @ 60Hz') {
                [void](Set-QemuWindowAspect -Process $process `
                    -FramebufferWidth $expectedWidth -FramebufferHeight $expectedHeight)
                [void](Move-QemuWindowToSecondaryMonitor -Process $process)
                $gamescopeWindowFitted = $true
            }
            if ($log -match 'STEARLIGHT ARM64 STEAMVR EXITED BEFORE DASHBOARD') {
                if (-not $nativeFailureAt) { $nativeFailureAt = Get-Date }
                $diagnosticsComplete = $log -match 'STEARLIGHT ARM64 STEAMVR DIAGNOSTICS COMPLETE'
                $diagnosticsTimedOut = (Get-Date).Subtract($nativeFailureAt).TotalSeconds -ge 45
                if ($diagnosticsComplete -or $diagnosticsTimedOut) {
                    throw "Steam Frame session failed before the native dashboard became ready. See $serial"
                }
            } elseif ($log -match 'STEARLIGHT ARM64 (STEAM WEBHELPER TIMEOUT|STEAM EXITED BEFORE WEBHELPER|STEAM EXITED AFTER READY|STEAMVR SESSION ENDED BEFORE CAPTURE|DASHBOARD TIMEOUT|SCREENSHOT FAILED)') {
                throw "Steam Frame session failed before the native dashboard became ready. See $serial"
            }
            if ($log -match '(?i)X server .* INVALID|vrwebhelper will fail to start') {
                if (-not $CaptureOnVRHTMLInvalid) {
                    throw "SteamVR's VRHTML WebHelper could not connect to Xwayland. See $serial"
                }
                if (-not $vrHtmlInvalidReported) {
                    Write-Warning 'SteamVR marked its VRHTML X server invalid; continuing to capture the rendered Gamescope output.'
                    $vrHtmlInvalidReported = $true
                }
            }
            if ($log -match 'STEARLIGHT ARM64 GAMESCOPE EXIT=' -and
                $log -notmatch 'STEARLIGHT ARM64 STEAMVR EXITED BEFORE DASHBOARD') {
                throw "Gamescope exited before a screenshot. See $serial"
            }
            if (-not $nativeFailureAt -and
                $log -match 'STEARLIGHT ARM64 SCREENSHOT HOST CAPTURE READY') {
                if (-not $captureReadyAt) {
                    $captureReadyAt = Get-Date
                    Write-Verbose 'Native processes are ready; checking session stability for 60 seconds before capture.'
                }
                if ((Get-Date).Subtract($captureReadyAt).TotalSeconds -ge 60) {
                    $screenshotReady = $true
                    break
                }
            }
            if ($log -match 'STEARLIGHT ARM64 GAMESCOPE START') {
                if (-not $startedAt) { $startedAt = Get-Date }
            }
        }
        Start-Sleep -Seconds 2
    }
    if (-not $startedAt) { throw "Guest did not reach the requested display startup. See $serial" }
    if (-not $screenshotReady) { throw "The requested VM screenshot was not ready before the deadline. See $serial" }
    if ($process.HasExited) { throw "QEMU exited before capture. See $serial and $stderr" }
    if (-not ([System.Management.Automation.PSTypeName]'StearlightQemuScreenshotWindowCapture').Type) {
        $previousTemp = $env:TEMP
        $previousTmp = $env:TMP
        $previousTmpDir = $env:TMPDIR
        try {
            $env:TEMP = $qemuTemp
            $env:TMP = $qemuTemp
            $env:TMPDIR = $qemuTemp
            Add-Type -AssemblyName System.Drawing
            $drawingAssembly = [System.Drawing.Bitmap].Assembly.Location
            Add-Type -ReferencedAssemblies $drawingAssembly -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class StearlightQemuScreenshotWindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    private struct CursorInfo {
        public int Size;
        public int Flags;
        public IntPtr Cursor;
        public Point Position;
    }
    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr data);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr data);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out int processId);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, System.Text.StringBuilder title, int maxCount);
    [DllImport("user32.dll")]
    private static extern bool GetCursorInfo(ref CursorInfo info);
    [DllImport("user32.dll")]
    private static extern bool DrawIconEx(IntPtr dc, int x, int y, IntPtr icon,
        int width, int height, int step, IntPtr flickerFree, int flags);

    public static IntPtr FindWindow(int processId) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr data) {
            int owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId && IsWindowVisible(window)) {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static string GetTitle(IntPtr window) {
        System.Text.StringBuilder title = new System.Text.StringBuilder(512);
        GetWindowText(window, title, title.Capacity);
        return title.ToString();
    }

    public static void Capture(IntPtr window, string path, int targetWidth, int targetHeight) {
        ShowWindow(window, 9);
        SetWindowPos(window, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040);
        SetForegroundWindow(window);
        System.Threading.Thread.Sleep(750);
        Rect rect;
        if (!GetClientRect(window, out rect)) throw new InvalidOperationException("Could not read QEMU window size.");
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width < 100 || height < 100) throw new InvalidOperationException("QEMU window surface is too small.");
        Point origin = new Point { X = 0, Y = 0 };
        if (!ClientToScreen(window, ref origin)) throw new InvalidOperationException("Could not locate QEMU window on screen.");
        CursorInfo previousCursor = new CursorInfo();
        previousCursor.Size = Marshal.SizeOf(typeof(CursorInfo));
        bool hadPreviousCursor = GetCursorInfo(ref previousCursor);
        SetCursorPos(origin.X + width / 2, origin.Y + height / 2);
        System.Threading.Thread.Sleep(400);
        using (Bitmap source = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        using (Graphics graphics = Graphics.FromImage(source)) {
            graphics.CopyFromScreen(origin.X, origin.Y, 0, 0, new Size(width, height), CopyPixelOperation.SourceCopy);
            CursorInfo cursor = new CursorInfo();
            cursor.Size = Marshal.SizeOf(typeof(CursorInfo));
            if (GetCursorInfo(ref cursor) && (cursor.Flags & 1) != 0 &&
                cursor.Position.X >= origin.X && cursor.Position.Y >= origin.Y &&
                cursor.Position.X < origin.X + width && cursor.Position.Y < origin.Y + height) {
                IntPtr dc = graphics.GetHdc();
                try { DrawIconEx(dc, cursor.Position.X - origin.X, cursor.Position.Y - origin.Y,
                    cursor.Cursor, 0, 0, 0, IntPtr.Zero, 3); }
                finally { graphics.ReleaseHdc(dc); }
            }
            source.Save(path, ImageFormat.Png);
        }
        if (hadPreviousCursor) SetCursorPos(previousCursor.Position.X, previousCursor.Position.Y);
    }
}
'@
        } finally {
            $env:TEMP = $previousTemp
            $env:TMP = $previousTmp
            $env:TMPDIR = $previousTmpDir
        }
    }
    $process.Refresh()
    $windowHandle = $process.MainWindowHandle
    if (-not $windowHandle) {
        $windowHandle = [StearlightQemuScreenshotWindowCapture]::FindWindow($process.Id)
    }
    if (-not $windowHandle) { throw 'QEMU display window was not found.' }
    Write-Verbose ("QEMU display: {0}" -f [StearlightQemuScreenshotWindowCapture]::GetTitle($windowHandle))
    [StearlightQemuScreenshotWindowCapture]::Capture(
        $windowHandle, $screenshot, $expectedWidth, $expectedHeight)
    # The compositor can fail between the readiness marker and capture.
    $captureLog = Get-Content -LiteralPath $serial -Raw
    if ($captureLog -match 'STEARLIGHT ARM64 STEAMVR COMPOSITOR FAILED') {
        throw "Valve's compositor failed during capture. See $serial"
    }
    if ($captureLog -match 'STEARLIGHT ARM64 GAMESCOPE EXIT=') {
        throw "Gamescope exited during capture. See $serial"
    }
    if ($AnimationPreview -and $captureLog -match 'STEARLIGHT ARM64 ANIMATION (STARTUP|PLAYBACK) FAILED') {
        throw "Animation playback failed during capture. See $serial"
    }
    # Preserve the capture at its real size. The guest mode is verified from
    # Gamescope's readiness marker, independently of the host window size.
    $bitmap = [Drawing.Bitmap]::FromFile($screenshot)
    try {
        $captureWidth = $bitmap.Width
        $captureHeight = $bitmap.Height
        $visibleSamples = 0
        $colorSamples = 0
        $sampleColors = New-Object 'System.Collections.Generic.HashSet[int]'
        $sampleStep = if ($displayPreview) { 4 } else { 24 }
        for ($y = 0; $y -lt $bitmap.Height; $y += $sampleStep) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $sampleStep) {
                $pixel = $bitmap.GetPixel($x, $y)
                # A solid blue compositor background also passes the brightness
                # and color checks. Require variation in the captured surface.
                $colorKey = ($pixel.R -shr 4) * 256 + ($pixel.G -shr 4) * 16 + ($pixel.B -shr 4)
                [void]$sampleColors.Add($colorKey)
                if (($pixel.R + $pixel.G + $pixel.B) -gt 18) { $visibleSamples++ }
                $maxChannel = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
                $minChannel = [Math]::Min($pixel.R, [Math]::Min($pixel.G, $pixel.B))
                if (($maxChannel - $minChannel) -gt 24) { $colorSamples++ }
            }
        }
    } finally {
        $bitmap.Dispose()
    }
    if ($visibleSamples -lt 5 -or $sampleColors.Count -lt 8 -or
        (-not $displayPreview -and $colorSamples -lt 5)) {
        throw "QEMU window capture is blank, uniform or still on the boot console ($visibleSamples visible, $colorSamples color samples, $($sampleColors.Count) sampled colors). See $serial"
    }
    Write-Verbose "Capture check: $visibleSamples visible samples, $colorSamples color samples, $($sampleColors.Count) sampled colors (${captureWidth}x${captureHeight}); guest mode ${expectedWidth}x${expectedHeight}."
    Write-Host "Screenshot: $screenshot"
    if ($AnimationPreview) {
        Write-Host 'Animation preview only; Gamescope, SteamVR and dashboard readiness were not tested.'
    }
    if ($GamescopePreview) {
        Write-Host 'Gamescope SDL preview only; native SteamVR dock, laser and stereo output were not tested.'
    }
    if (-not $displayPreview) {
        Write-Host 'Runtime and capture checks completed; inspect the native dock, laser and panel in QEMU.'
        if ($KeepRunning) {
            Write-Host "QEMU remains running (PID $($process.Id))."
        }
    }
} finally {
    if (-not $KeepRunning -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}
