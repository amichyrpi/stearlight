[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath,

    [Parameter(Mandatory = $true)]
    [int]$DiskNumber,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedSerial,

    [Parameter(Mandatory = $true)]
    [UInt64]$ExpectedSize
)

$ErrorActionPreference = 'Stop'
$resolvedImage = (Resolve-Path -LiteralPath $ImagePath).Path
$outputDirectory = Split-Path -Parent $resolvedImage
$transcriptPath = Join-Path $outputDirectory 'rpi-imager-flash-output.log'
$imagerLogPath = Join-Path $outputDirectory 'rpi-imager-flash.log'
$resultPath = Join-Path $outputDirectory 'rpi-imager-flash-result.json'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

if (-not $isAdministrator) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', ('"{0}"' -f $PSCommandPath),
        '-ImagePath', ('"{0}"' -f $resolvedImage),
        '-DiskNumber', $DiskNumber,
        '-ExpectedSerial', ('"{0}"' -f $ExpectedSerial),
        '-ExpectedSize', $ExpectedSize
    )
    $elevated = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments `
        -Verb RunAs -Wait -PassThru
    if (-not (Test-Path -LiteralPath $resultPath)) {
        throw 'The elevated flash process did not create a result file.'
    }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($elevated.ExitCode -ne 0 -or $result.ExitCode -ne 0) {
        throw "Image flash failed. See $transcriptPath and $imagerLogPath"
    }
    Write-Host "Image written and verified on PhysicalDrive$DiskNumber."
    exit 0
}

$disk = Get-Disk -Number $DiskNumber
$actualSerial = $disk.SerialNumber.Trim()
if ($actualSerial -ne $ExpectedSerial -or
    [UInt64]$disk.Size -ne $ExpectedSize -or
    $disk.BusType -ne 'USB' -or
    $disk.IsBoot -or $disk.IsSystem -or $disk.IsReadOnly) {
    throw 'Target identity or safety validation failed; refusing to flash.'
}

$imager = 'C:\Program Files\Raspberry Pi Ltd\Imager\rpi-imager.exe'
if (-not (Test-Path -LiteralPath $imager)) {
    throw 'Raspberry Pi Imager is not installed in the expected location.'
}

$sourceSha256 = (Get-FileHash -LiteralPath $resolvedImage -Algorithm SHA256).Hash.ToLowerInvariant()
$flashImage = $resolvedImage
$stagedRawImage = $null
if ([IO.Path]::GetExtension($resolvedImage) -eq '.gz') {
    $stagedRawImage = [IO.Path]::ChangeExtension($resolvedImage, $null)
    if (Test-Path -LiteralPath $stagedRawImage) {
        throw "Refusing to overwrite the raw staging image: $stagedRawImage"
    }
    $input = [IO.File]::OpenRead($resolvedImage)
    $gzip = [IO.Compression.GzipStream]::new(
        $input, [IO.Compression.CompressionMode]::Decompress)
    $output = [IO.File]::Open(
        $stagedRawImage, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $gzip.CopyTo($output, 1MB)
    }
    finally {
        $output.Dispose()
        $gzip.Dispose()
        $input.Dispose()
    }
    $flashImage = $stagedRawImage
}

$sha256 = (Get-FileHash -LiteralPath $flashImage -Algorithm SHA256).Hash.ToLowerInvariant()
$target = "\\.\PhysicalDrive$DiskNumber"

$processInfo = [Diagnostics.ProcessStartInfo]::new()
$processInfo.FileName = $imager
$processInfo.UseShellExecute = $false
$processInfo.CreateNoWindow = $true
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processArguments = @(
    '--cli', '--debug', '--disable-eject',
    '--sha256', $sha256,
    '--log-file', $imagerLogPath,
    $flashImage, $target
)
$processInfo.Arguments = ($processArguments | ForEach-Object {
    '"' + ($_ -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}) -join ' '

$process = [Diagnostics.Process]::new()
$process.StartInfo = $processInfo
$processExitCode = $null
$cleanupError = $null
try {
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $output = $stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()
    $output | Set-Content -LiteralPath $transcriptPath -Encoding UTF8
    $processExitCode = $process.ExitCode
}
finally {
    if ($stagedRawImage -and (Test-Path -LiteralPath $stagedRawImage)) {
        try {
            [IO.File]::Delete($stagedRawImage)
        }
        catch {
            $cleanupError = $_.Exception.Message
        }
    }
}

if ($null -eq $processExitCode) {
    throw 'Raspberry Pi Imager terminated without returning an exit code.'
}

$result = [ordered]@{
    ExitCode = $processExitCode
    DiskNumber = $DiskNumber
    SerialNumber = $actualSerial
    Size = [UInt64]$disk.Size
    Image = $resolvedImage
    SourceSHA256 = $sourceSha256
    FlashSHA256 = $sha256
    CleanupError = $cleanupError
    CompletedAt = (Get-Date).ToUniversalTime().ToString('o')
}
$result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8

if ($processExitCode -ne 0) {
    exit $processExitCode
}

if ($cleanupError) {
    Write-Warning "Image verified, but temporary image cleanup failed: $cleanupError"
}
