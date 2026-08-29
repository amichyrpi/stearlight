[CmdletBinding()]
param(
    [string]$WslDistribution
)

$ErrorActionPreference = 'Stop'

$linuxScript = if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
    # Passing a native Windows path through wsl.exe can cause its backslashes
    # to be consumed as shell escapes.  wslpath also accepts the slash form,
    # which remains one argument across both PowerShell and WSL.
    $windowsScript = (Resolve-Path (Join-Path $PSScriptRoot 'build.sh')).Path `
        -replace '\\', '/'
    $translated = & wsl.exe -- wslpath -a -u $windowsScript
    if ($LASTEXITCODE -ne 0 -or -not $translated) {
        throw "WSL could not translate the build script path '$windowsScript'."
    }
    $translated.Trim()
} else {
    throw 'WSL2 is required to preserve Linux ownership and symlinks while building.'
}

$wslPrefix = @()
if ($WslDistribution) {
    $wslPrefix += @('--distribution', $WslDistribution)
}
& wsl.exe @wslPrefix -- sh -lc 'command -v bash >/dev/null 2>&1'
$wslHasBash = $LASTEXITCODE -eq 0

if ($wslHasBash) {
    & wsl.exe @wslPrefix -- bash $linuxScript
    if ($LASTEXITCODE -ne 0) {
        throw "Stearlight OS build failed with exit code $LASTEXITCODE."
    }
    exit 0
}

# Docker Desktop's internal WSL distribution intentionally has no bash.  A
# native buildx invocation is equivalent for this Dockerfile and avoids
# requiring users to install a second WSL distribution solely for the build.
$docker = Get-Command docker.exe -ErrorAction SilentlyContinue
if (-not $docker) { throw 'Docker Desktop with buildx is required.' }
& $docker.Source buildx version | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Docker buildx is not available.' }

$repoDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputRoot = Join-Path $repoDirectory 'out\stearlight-os'
$imageDirectory = Join-Path $outputRoot 'image'
$safePrefix = [IO.Path]::GetFullPath($outputRoot) + [IO.Path]::DirectorySeparatorChar
$resolvedImageDirectory = [IO.Path]::GetFullPath($imageDirectory)
if (-not $resolvedImageDirectory.StartsWith(
        $safePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to replace an output directory outside out/stearlight-os.'
}
if ([IO.Directory]::Exists($resolvedImageDirectory)) {
    [IO.Directory]::Delete($resolvedImageDirectory, $true)
}
[IO.Directory]::CreateDirectory($resolvedImageDirectory) | Out-Null

$buildDate = [DateTime]::UtcNow.ToString('yyyyMMdd')
$bakeSteam = if ($env:STEARLIGHT_BAKE_STEAM) {
    $env:STEARLIGHT_BAKE_STEAM
} else { '1' }
& $docker.Source buildx build --platform linux/arm64 `
    --build-arg "STEARLIGHT_BAKE_STEAM=$bakeSteam" `
    --build-arg "STEARLIGHT_BUILD_DATE=$buildDate" `
    --target image `
    --output "type=local,dest=$resolvedImageDirectory" `
    --file (Join-Path $PSScriptRoot 'Dockerfile') $repoDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Stearlight OS Docker build failed with exit code $LASTEXITCODE."
}
Write-Host "Built $(Join-Path $resolvedImageDirectory "stearlight-os-${buildDate}rp4.img.gz")"
