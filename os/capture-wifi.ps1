[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path $PSScriptRoot 'generated\wifi.env'),
    [string]$ProfileName
)

$ErrorActionPreference = 'Stop'

if (-not $ProfileName) {
    $connection = Get-NetConnectionProfile |
        Where-Object { $_.IPv4Connectivity -ne 'Disconnected' } |
        Select-Object -First 1
    if (-not $connection) {
        throw 'No connected Windows network profile was found.'
    }
    $ProfileName = $connection.Name
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    'stearlight-wifi-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
try {
    & netsh.exe wlan export profile name="$ProfileName" key=clear folder="$temporaryDirectory" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Windows could not export Wi-Fi profile '$ProfileName'."
    }
    $profileFile = Get-ChildItem -LiteralPath $temporaryDirectory -Filter '*.xml' |
        Select-Object -First 1
    if (-not $profileFile) {
        throw "Profile '$ProfileName' is not a Wi-Fi profile."
    }
    [xml]$profile = Get-Content -LiteralPath $profileFile.FullName -Raw
    $namespace = New-Object System.Xml.XmlNamespaceManager($profile.NameTable)
    $namespace.AddNamespace('w', $profile.DocumentElement.NamespaceURI)
    $ssid = $profile.SelectSingleNode('//w:SSIDConfig/w:SSID/w:name', $namespace).InnerText
    $keyNode = $profile.SelectSingleNode('//w:MSM/w:security/w:sharedKey/w:keyMaterial', $namespace)
    $psk = if ($keyNode) { $keyNode.InnerText } else { '' }

    $ssid64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($ssid))
    $psk64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($psk))
    $parent = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    @(
        "STEARLIGHT_WIFI_SSID_B64='$ssid64'"
        "STEARLIGHT_WIFI_PSK_B64='$psk64'"
        "STEARLIGHT_WIFI_INTERFACE='wlan0'"
    ) | Set-Content -LiteralPath $OutputPath -Encoding ascii
    Write-Host "Bundled Windows Wi-Fi profile '$ssid' (the password was not printed)."
}
finally {
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
