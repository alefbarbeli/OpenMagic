# Removes the OpenMagic driver packages; the keyboard and mouse fall back to
# the inbox Windows drivers. Run from an elevated PowerShell.
$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this script from an elevated (Administrator) PowerShell.'
}

$ours = 'openmagickbd.inf', 'openmagicmouse.inf', 'openmagicmousebt.inf'
$packages = Get-WindowsDriver -Online | Where-Object { $ours -contains (Split-Path $_.OriginalFileName -Leaf).ToLower() }
if (-not $packages) { 'No OpenMagic driver packages are installed.'; return }

foreach ($package in $packages) {
  "Removing $($package.Driver) ($(Split-Path $package.OriginalFileName -Leaf))"
  & pnputil.exe /delete-driver $package.Driver /uninstall /force
}
& pnputil.exe /scan-devices | Out-Null
'Done. The devices now use the inbox drivers again.'
