<#
.SYNOPSIS
  Shows and edits the OpenMagic Magic Mouse scroll settings.

.DESCRIPTION
  The OpenMagicMouse driver reads these values when the mouse connects:
    ScrollSpeed          percent, 10-400 (default 100)
    NaturalScrolling     1 = content follows the finger, like macOS (default 1)
    HorizontalScrolling  1 = sideways swipes scroll horizontally (default 1)
    SmoothScrolling      1 = high-resolution wheel steps, 0 = classic notches (default 1)
  Changing them requires an elevated shell; the mouse is restarted afterwards.

.EXAMPLE
  .\OpenMagic-Mouse.ps1                            # show settings
  .\OpenMagic-Mouse.ps1 -ScrollSpeed 150
  .\OpenMagic-Mouse.ps1 -NaturalScrolling 0 -SmoothScrolling 0
#>
param(
  [ValidateRange(10, 400)][int]$ScrollSpeed,
  [ValidateSet(0, 1)][int]$NaturalScrolling = -1,
  [ValidateSet(0, 1)][int]$HorizontalScrolling = -1,
  [ValidateSet(0, 1)][int]$SmoothScrolling = -1
)

$ErrorActionPreference = 'Stop'
$ParametersKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\OpenMagicMouse\Parameters'

$changes = @{}
if ($PSBoundParameters.ContainsKey('ScrollSpeed')) { $changes.ScrollSpeed = $ScrollSpeed }
if ($NaturalScrolling -ge 0) { $changes.NaturalScrolling = $NaturalScrolling }
if ($HorizontalScrolling -ge 0) { $changes.HorizontalScrolling = $HorizontalScrolling }
if ($SmoothScrolling -ge 0) { $changes.SmoothScrolling = $SmoothScrolling }

if ($changes.Count -gt 0) {
  $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Changing the settings needs an elevated (Administrator) PowerShell.'
  }
  if (-not (Test-Path $ParametersKey)) { New-Item -Path $ParametersKey -Force | Out-Null }
  foreach ($name in $changes.Keys) {
    New-ItemProperty -Path $ParametersKey -Name $name -PropertyType DWord -Value $changes[$name] -Force | Out-Null
  }

  # The settings are read by the Bluetooth-side filter, so restart that device.
  $devices = Get-PnpDevice -PresentOnly -Class HIDClass | Where-Object {
    (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_LowerFilters -ErrorAction SilentlyContinue).Data -contains 'OpenMagicMouse'
  }
  if (-not $devices) { Write-Warning 'No mouse is using the OpenMagicMouse driver right now.' }
  foreach ($d in $devices) {
    & pnputil.exe /restart-device "$($d.InstanceId)" | Out-Null
    "Restarted: $($d.FriendlyName)"
  }
}

$current = Get-ItemProperty -Path $ParametersKey -ErrorAction SilentlyContinue
foreach ($name in 'ScrollSpeed', 'NaturalScrolling', 'HorizontalScrolling', 'SmoothScrolling') {
  '{0,-20} {1}' -f $name, $(if ($current -and $null -ne $current.$name) { $current.$name } else { '(default)' })
}
