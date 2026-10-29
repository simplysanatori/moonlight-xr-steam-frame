# usb-probe-windows.ps1 - PC side of the Steam Frame USB-C network probe (plan 4c.1). GPLv3.
#
# Read-only. Lists network adapters that look like USB NCM / RNDIS / ECM links
# with their IPv4 addresses and routes, lists present USB devices that look like
# the Frame or a gadget, pings the Frame's device-side address, and prints an
# iperf3 hint when iperf3 is on PATH. Changes nothing.
#
# Usage (Frame connected by its USB-C data cable, Developer Mode on):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\probe\usb-probe-windows.ps1

param(
    [string]$FrameAddress = '10.86.200.233',
    [string]$PcSubnetPrefix = '10.86.200.'
)

$ErrorActionPreference = 'Continue'
$pattern = 'NCM|RNDIS|Remote NDIS|USB Ethernet|Gadget'

function Section([string]$Title) {
    Write-Host ''
    Write-Host "==== $Title ===="
}

Section 'Network adapters that look like USB Ethernet (NCM / RNDIS / ECM)'
$all = @()
try {
    $all = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
} catch {
    Write-Host "Get-NetAdapter failed: $($_.Exception.Message)"
}
$matched = @($all | Where-Object { $_.Name -match $pattern -or $_.InterfaceDescription -match $pattern })

if ($matched.Count -eq 0) {
    Write-Host 'No adapter name matched. All adapters, for comparison:'
    $all | Select-Object Name, InterfaceDescription, Status, ifIndex | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
} else {
    foreach ($a in $matched) {
        Write-Host "Name:        $($a.Name)"
        Write-Host "Description: $($a.InterfaceDescription)"
        Write-Host "Status:      $($a.Status)    ifIndex: $($a.ifIndex)"
        $ips = @(Get-NetIPAddress -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue)
        if ($ips.Count -eq 0) {
            Write-Host '  IPv4: none'
        } else {
            foreach ($ip in $ips) {
                Write-Host "  IPv4: $($ip.IPAddress)/$($ip.PrefixLength)  ($($ip.PrefixOrigin))"
            }
        }
        Write-Host ''
    }
}

Section "IPv4 routes into $($PcSubnetPrefix)0/24"
$routes = @(Get-NetRoute -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.DestinationPrefix -like "$PcSubnetPrefix*" })
if ($routes.Count -eq 0) {
    Write-Host 'no routes in that subnet'
} else {
    $routes | Select-Object DestinationPrefix, NextHop, InterfaceAlias, RouteMetric | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}

Section 'USB devices that look like the Frame or a gadget'
$usb = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like 'USB*' -and ($_.FriendlyName -match 'Valve|Steam|Frame|Gadget|NCM|RNDIS|Remote NDIS|USB Ethernet') })
if ($usb.Count -eq 0) {
    Write-Host 'none matched (check Device Manager; the gadget may show only in Developer Mode)'
} else {
    $usb | Select-Object Status, Class, FriendlyName | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}

Section "Ping $FrameAddress"
& ping.exe -n 4 -w 1000 $FrameAddress
if ($LASTEXITCODE -eq 0) {
    Write-Host 'reply received'
} else {
    Write-Host "no reply (exit $LASTEXITCODE): check the adapter above has a $($PcSubnetPrefix)x address and the cable is data-capable"
}

Section 'iperf3'
$iperf = Get-Command iperf3 -ErrorAction SilentlyContinue
if ($iperf) {
    Write-Host "found: $($iperf.Source)"
    Write-Host 'The Frame needs an iperf3 server running first (only if iperf3 exists there): iperf3 -s'
    Write-Host "PC to Frame:  iperf3 -c $FrameAddress -t 10"
    Write-Host "Frame to PC:  iperf3 -c $FrameAddress -t 10 -R"
} else {
    Write-Host 'iperf3 not on PATH; skipped (the throughput test is optional)'
}

Write-Host ''
Write-Host 'Done. Also note by hand: does the PC start charging the Frame over this cable?'
