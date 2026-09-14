param(
  [switch]$InstallToSteamVR,
  [switch]$LegacyH265,
  [switch]$InstallVrlinkResources,
  [string]$ProductName
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-win64'

$registry = Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
if (-not (Test-Path -LiteralPath $registry)) { throw "SteamVR vrpathreg.exe not found: $registry" }
$steamVrRoot = Split-Path (Split-Path (Split-Path $registry -Parent) -Parent) -Parent

if ($LegacyH265 -and $InstallVrlinkResources) {
  throw 'Choose either -LegacyH265 or -InstallVrlinkResources, not both.'
}
if ($ProductName -and -not $InstallVrlinkResources) {
  throw '-ProductName is only valid with -InstallVrlinkResources.'
}

if (-not $LegacyH265 -and -not $InstallVrlinkResources) {
  if ($InstallToSteamVR) {
    throw '-InstallToSteamVR requires -InstallVrlinkResources or -LegacyH265.'
  }
  # Valve ships and owns this transport. The native Stearlight path must not
  # register a guessed third-party driver or replace the Steam client flow.
  $nativeManifest = Join-Path $steamVrRoot 'drivers\vrlink\driver.vrdrivermanifest'
  if (-not (Test-Path -LiteralPath $nativeManifest)) {
    throw "Valve's native VRLink driver is missing: $nativeManifest"
  }
  & $registry removedriverswithname svrt
  if ($LASTEXITCODE -ne 0) {
    throw "Could not remove the legacy svrt SteamVR registration (exit code $LASTEXITCODE)"
  }
  Write-Host "Using Valve's native SteamVR VRLink driver at $($steamVrRoot)\drivers\vrlink"
  Write-Host 'No external driver was registered; Steam client owns pairing, streaming, tracking, and input.'
  return
}

if ($InstallVrlinkResources -and [string]::IsNullOrWhiteSpace($ProductName)) {
  throw '-ProductName is required because Steam Link selects VRLink settings by the exact headset product.'
}

$cmakeArgs = @(
  '-S', $root, '-B', $build, '-A', 'x64',
  ('-DSVRT_BUILD_DRIVER=' + $(if ($LegacyH265) { 'ON' } else { 'OFF' })),
  '-DSVRT_BUILD_PI_LIBRARY=OFF',
  '-DSVRT_BUILD_RECEIVER=OFF',
  ('-DSVRT_BUILD_VRLINK_RESOURCES=' + $(if ($InstallVrlinkResources) { 'ON' } else { 'OFF' }))
)
if ($InstallVrlinkResources) {
  $cmakeArgs += '-DSVRT_VRLINK_PRODUCT=' + $ProductName
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
& cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
$packageName = if ($LegacyH265) { 'svrt' } else { 'svrt-vrlink' }
$package = Join-Path $build $packageName

if (-not $LegacyH265) {
  # The old project driver is alwaysActivate and can claim the same HMD before
  # Valve's built-in VRLink path sees it. Remove only drivers registered under
  # this project's legacy name; unrelated SteamVR drivers are untouched.
  & $registry removedriverswithname svrt
  if ($LASTEXITCODE -ne 0) {
    throw "Could not remove the legacy svrt SteamVR registration (exit code $LASTEXITCODE)"
  }
}

$driverPath = $package
if ($InstallToSteamVR) {
  $target = Join-Path $steamVrRoot "drivers\$packageName"
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  $duplicate = Join-Path $target 'resources\resources'
  if (Test-Path -LiteralPath $duplicate) {
    Remove-Item -LiteralPath $duplicate -Recurse -Force
  }
  Copy-Item -Path (Join-Path $package '*') -Destination $target -Recurse -Force
  $driverPath = $target
  Write-Host "SVRT SteamVR package installed at $target"
}

& $registry adddriver $driverPath
if ($LASTEXITCODE -ne 0) { throw "vrpathreg failed with exit code $LASTEXITCODE" }

if ($LegacyH265) {
  Write-Host "Legacy SVRT H.265 driver packaged at $package"
} else {
  Write-Host "Native Steam Frame/VRLink resource package packaged at $package"
}
