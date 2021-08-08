#Requires -RunAsAdministrator
$SERVICE_GUID_TEMPLATE = "{0:x8}-facb-11e6-bd58-64006a7986d3"
$REG_BASE = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Virtualization\GuestCommunicationServices"
$REG_ELEMENT_NAME_VALUE = "ssh agent bridge listener"
$SOCAT_OLD = "SOCKET-CONNECT:40:0:x0000x{0}x02000000x00000000"
$SOCAT_NEW = "VSOCK-CONNECT:2:0x{0:x8}"
$VSOCK_PORT = 1145141919

Write-Output "=== HyperV Integration Service Register Wizard ==="

$result = Read-Host "Choose a port(leave it empty to generate a random one)"
if($result -eq ""){
    $VSOCK_PORT = Get-Random
}else{
    $VSOCK_PORT = $result -as [int]
    if($VSOCK_PORT -le 0){
        $VSOCK_PORT = $null
    }
}

if($VSOCK_PORT -eq $null){
    Write-Host "Invalid Port"
    exit 1
}

Write-Output ("Using port: 0x{0:x}" -f $VSOCK_PORT)

$SERVICE_GUID = ($SERVICE_GUID_TEMPLATE -f $VSOCK_PORT)

Write-Output ("Using service guid: {0}" -f $SERVICE_GUID)

$service_hive_path = "$REG_BASE\$SERVICE_GUID"
$service_hive = Get-Item $service_hive_path -ErrorAction SilentlyContinue

if($service_hive -eq $null) {
    Write-Output "Registry entry do not exist. Create a new one."
    $service_hive = New-Item $service_hive_path
    if($service_hive -eq $null) {
        Write-Output "Failed to create entry!"
        exit 1
    }
    $element_name = New-ItemProperty -Path $service_hive_path -Name "ElementName" -Value $REG_ELEMENT_NAME_VALUE -PropertyType String
}else {
    Write-Output "Existing registry entry!"
}

Write-Output "For old socat:"
$bytes = [System.BitConverter]::GetBytes($VSOCK_PORT)
$le_str = ""
foreach($byte in $bytes) { $le_str = $le_str + ("{0:x2}" -f $byte) }
Write-Output ($SOCAT_OLD -f $le_str)

Write-Output "For new socat(>=1.7.4):"
Write-Output ($SOCAT_NEW -f $VSOCK_PORT)
