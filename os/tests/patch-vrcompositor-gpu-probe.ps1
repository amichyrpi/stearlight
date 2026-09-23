[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

# Diagnostic only: the Windows QEMU/lavapipe probe cannot complete Valve's
# startup GPU benchmark. Keep the production SteamVR binary untouched.
$expectedSha256 = '7BB64CD3CD813DEF581D402F0967871EA103CB153452E8C4927DF678079EF921'
$actualSha256 = (Get-FileHash -LiteralPath $InputPath -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "Unsupported vrcompositor binary: $actualSha256"
}

$inputFull = [IO.Path]::GetFullPath($InputPath)
$outputFull = [IO.Path]::GetFullPath($OutputPath)
if ($inputFull -eq $outputFull) {
    throw 'Refusing to overwrite the original Valve binary.'
}

$bytes = [IO.File]::ReadAllBytes($inputFull)
# ELF .text VMA 0x14cf30, file offset 0x14bf30. Function VMA 0x18e310.
$offset = 0x18d310
$original = [byte[]](0x55, 0x48, 0x89, 0xe5, 0x41, 0x57)
$replacement = [byte[]](0xb8, 0x52, 0x03, 0x00, 0x00, 0xc3)
for ($i = 0; $i -lt $original.Length; $i++) {
    if ($bytes[$offset + $i] -ne $original[$i]) {
        throw "Unexpected instruction at file offset 0x$('{0:x}' -f ($offset + $i))"
    }
    $bytes[$offset + $i] = $replacement[$i]
}

[IO.File]::WriteAllBytes($outputFull, $bytes)
Write-Output "Wrote VM-only GPU probe binary: $outputFull"
