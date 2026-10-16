[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$Adb='adb', [string]$Serial, [switch]$Undo)
$ErrorActionPreference='Stop'
$adapters=@(Get-NetAdapter | Where-Object { $_.InterfaceDescription -like 'UsbNcm*' -and $_.Status -eq 'Up' })
if($adapters.Count -ne 1){throw 'Connect one Quest USB network and allow its Horizon consent prompt first.'}
$adapter=$adapters[0]
$guid=$adapter.InterfaceGuid.ToString().Trim('{}')
$recordPath=Join-Path $env:LOCALAPPDATA "Stearlight\usb-network-$guid.json"
$administrator=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if($Undo){
    if(!(Test-Path -LiteralPath $recordPath)){throw 'This USB adapter has no saved Stearlight address configuration.'}
    $record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    if($record.InterfaceGuid -ne $guid){throw 'USB adapter identity changed.'}
    if($PSCmdlet.ShouldProcess($adapter.Name,"Remove Stearlight USB address $($record.Address) and restore DHCP")){
        if(!$administrator){throw 'Run this script from an Administrator PowerShell to configure the USB adapter.'}
        foreach($store in @('PersistentStore','ActiveStore')){
            Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -PolicyStore $store -ErrorAction SilentlyContinue |
                Where-Object IPAddress -eq $record.Address | Remove-NetIPAddress -Confirm:$false
        }
        $other=@(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notlike '169.254.*' })
        if($record.Dhcp -eq 'Enabled' -and !$other.Count){Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -Dhcp Enabled}
        elseif($record.Dhcp -eq 'Enabled' -and $other.Count){throw 'The Stearlight address was removed. Additional IPv4 addresses are present, so DHCP was left unchanged.'}
        Remove-Item -LiteralPath $recordPath
        Write-Output 'Stearlight USB address configuration restored.'
    }
    return
}
if(!$Serial){
    $devices=@(& $Adb devices | Where-Object { $_ -match '^\S+\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if($LASTEXITCODE -or $devices.Count -ne 1){throw 'Pass -Serial for the connected Quest ADB device.'}
    $Serial=$devices[0]
}
$json=& $Adb -s $Serial exec-out run-as net.stearlight.quest cat files/state/usb-network.json
if($LASTEXITCODE){throw 'Open Stearlight on the Quest before configuring its USB network.'}
$link=($json -join "`n") | ConvertFrom-Json
if(!$link.available){throw 'Allow the USB-network prompt in the Quest, then retry.'}
$cidr=@($link.addresses | Where-Object { $_ -match '^\d+\.\d+\.\d+\.\d+/\d+$' })
if($cidr.Count -ne 1){throw 'The Quest USB network must report one IPv4 address.'}
$parts=$cidr[0].Split('/');$ip=[Net.IPAddress]::Parse($parts[0]);$prefix=[int]$parts[1]
if($prefix -lt 8 -or $prefix -gt 30){throw 'Unsupported USB IPv4 subnet size.'}
[uint64]$guest=0
foreach($byte in $ip.GetAddressBytes()){$guest=$guest*256+$byte}
[uint64]$hostMask=([uint64]1 -shl (32-$prefix))-1
[uint64]$network=$guest -band ([uint64]4294967295 -bxor $hostMask)
[uint64]$peer=$network+$hostMask-1
if($peer -eq $guest){$peer--}
$peerAddress=([Net.IPAddress]::new([byte[]]@((($peer -shr 24) -band 255),(($peer -shr 16) -band 255),(($peer -shr 8) -band 255),($peer -band 255)))).ToString()
$addresses=@(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue)
foreach($existing in $addresses){
    if($existing.IPAddress -like '169.254.*'){continue}
    $value=[uint64]0;foreach($byte in ([Net.IPAddress]::Parse($existing.IPAddress)).GetAddressBytes()){$value=$value*256+$byte}
    if(($value -band ([uint64]4294967295 -bxor $hostMask)) -eq $network){Write-Output "USB adapter already has a compatible address: $($existing.IPAddress)";return}
    throw 'The USB adapter already has a different IPv4 configuration; it was left unchanged.'
}
$dhcp=(Get-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4).Dhcp.ToString()
$record=[pscustomobject]@{InterfaceGuid=$guid;Address=$peerAddress;PrefixLength=$prefix;Dhcp=$dhcp}
if(Test-Path -LiteralPath $recordPath){
    $saved=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    if($saved.InterfaceGuid -ne $guid -or $saved.Address -ne $peerAddress){throw 'Undo the previous Stearlight USB configuration before changing subnets.'}
    $record=$saved
}
Write-Output "Quest USB: $($ip.IPAddressToString)/$prefix; PC USB: $peerAddress/$prefix; adapter: $($adapter.Name)"
if($PSCmdlet.ShouldProcess($adapter.Name,"Add $peerAddress/$prefix to the USB adapter (DHCP becomes disabled on this adapter)")){
    if(!$administrator){throw 'Run this script from an Administrator PowerShell to configure the USB adapter.'}
    New-Item -ItemType Directory -Path (Split-Path $recordPath) -Force | Out-Null
    $record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8
    try{New-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -IPAddress $peerAddress -PrefixLength $prefix | Out-Null}
    catch{if($record.Dhcp -eq 'Enabled'){Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -Dhcp Enabled};throw}
    Write-Output 'USB IPv4 configured. Restart the Steam streaming session. Use -Undo to restore the adapter.'
}
