<#
.SYNOPSIS
  Shows and edits the OpenMagic keyboard keymaps.

.DESCRIPTION
  The OpenMagicKbd driver reads two REG_MULTI_SZ values when the keyboard starts:
    Map    - always applied (e.g. F13 -> PrintScreen)
    FnMap  - applied while Fn is held (e.g. F8 -> MediaPlayPause)
  Changing them requires an elevated shell; the keyboard is restarted afterwards
  so the driver picks up the new maps.

.EXAMPLE
  .\OpenMagic-Keymap.ps1                         # list both maps
  .\OpenMagic-Keymap.ps1 set F16 MediaStop       # F16 -> Stop
  .\OpenMagic-Keymap.ps1 set F3 Win+Tab -Fn      # Fn+F3 -> Task View
  .\OpenMagic-Keymap.ps1 set F17 None            # disable F17
  .\OpenMagic-Keymap.ps1 remove F16              # back to plain F16
  .\OpenMagic-Keymap.ps1 keys                    # list key names
#>
param(
  [ValidateSet('list', 'set', 'remove', 'keys', 'apply')]
  [string]$Command = 'list',
  [string]$Key,
  [string]$Target,
  [switch]$Fn
)

$ErrorActionPreference = 'Stop'
$ParametersKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\OpenMagicKbd\Parameters'

# Set-1 scancodes; E0-prefixed keys are 0xE0xx, Pause is 0xE11D.
$KeyCodes = [ordered]@{
  Esc = 0x01; '1' = 0x02; '2' = 0x03; '3' = 0x04; '4' = 0x05; '5' = 0x06; '6' = 0x07; '7' = 0x08; '8' = 0x09; '9' = 0x0A; '0' = 0x0B
  Minus = 0x0C; Equals = 0x0D; Backspace = 0x0E; Tab = 0x0F
  Q = 0x10; W = 0x11; E = 0x12; R = 0x13; T = 0x14; Y = 0x15; U = 0x16; I = 0x17; O = 0x18; P = 0x19
  LBracket = 0x1A; RBracket = 0x1B; Enter = 0x1C; LCtrl = 0x1D
  A = 0x1E; S = 0x1F; D = 0x20; F = 0x21; G = 0x22; H = 0x23; J = 0x24; K = 0x25; L = 0x26
  Semicolon = 0x27; Quote = 0x28; Grave = 0x29; LShift = 0x2A; Backslash = 0x2B
  Z = 0x2C; X = 0x2D; C = 0x2E; V = 0x2F; B = 0x30; N = 0x31; M = 0x32
  Comma = 0x33; Period = 0x34; Slash = 0x35; RShift = 0x36; NumMultiply = 0x37; LAlt = 0x38; Space = 0x39; CapsLock = 0x3A
  F1 = 0x3B; F2 = 0x3C; F3 = 0x3D; F4 = 0x3E; F5 = 0x3F; F6 = 0x40; F7 = 0x41; F8 = 0x42; F9 = 0x43; F10 = 0x44
  NumLock = 0x45; ScrollLock = 0x46
  Num7 = 0x47; Num8 = 0x48; Num9 = 0x49; NumMinus = 0x4A; Num4 = 0x4B; Num5 = 0x4C; Num6 = 0x4D; NumPlus = 0x4E
  Num1 = 0x4F; Num2 = 0x50; Num3 = 0x51; Num0 = 0x52; NumDecimal = 0x53
  F11 = 0x57; F12 = 0x58
  F13 = 0x64; F14 = 0x65; F15 = 0x66; F16 = 0x67; F17 = 0x68; F18 = 0x69; F19 = 0x6A
  F20 = 0x6B; F21 = 0x6C; F22 = 0x6D; F23 = 0x6E; F24 = 0x76
  MediaPrev = 0xE010; MediaNext = 0xE019; Mute = 0xE020; Calculator = 0xE021; MediaPlayPause = 0xE022; MediaStop = 0xE024
  VolumeDown = 0xE02E; VolumeUp = 0xE030; BrowserHome = 0xE032
  NumEnter = 0xE01C; RCtrl = 0xE01D; NumDivide = 0xE035; PrintScreen = 0xE037; RAlt = 0xE038
  Home = 0xE047; Up = 0xE048; PageUp = 0xE049; Left = 0xE04B; Right = 0xE04D; End = 0xE04F
  Down = 0xE050; PageDown = 0xE051; Insert = 0xE052; Delete = 0xE053
  Win = 0xE05B; RWin = 0xE05C; Apps = 0xE05D
  BrowserSearch = 0xE065; BrowserFavorites = 0xE066; BrowserRefresh = 0xE067; BrowserStop = 0xE068
  BrowserForward = 0xE069; BrowserBack = 0xE06A; MyComputer = 0xE06B; Mail = 0xE06C; MediaSelect = 0xE06D
  Pause = 0xE11D
}
$KeyNames = @{}
foreach ($entry in $KeyCodes.GetEnumerator()) { if (-not $KeyNames.ContainsKey($entry.Value)) { $KeyNames[$entry.Value] = $entry.Key } }

function ConvertTo-Code([string]$name) {
  foreach ($k in $KeyCodes.Keys) { if ($k -ieq $name) { return $KeyCodes[$k] } }
  if ($name -match '^(0x)?([0-9a-fA-F]{2}|E0[0-9a-fA-F]{2})$') { return [Convert]::ToInt32($Matches[2], 16) }
  throw "Unknown key '$name'. Run '.\OpenMagic-Keymap.ps1 keys' for the list."
}
function ConvertTo-Hex([int]$code) { if ($code -gt 0xFF) { '{0:X4}' -f $code } else { '{0:X2}' -f $code } }
function ConvertTo-Name([string]$hex) {
  $code = [Convert]::ToInt32($hex, 16)
  if ($KeyNames.ContainsKey($code)) { $KeyNames[$code] } else { "0x$hex" }
}

function Get-Keymap([string]$valueName) {
  $map = [ordered]@{}
  $values = (Get-ItemProperty -Path $ParametersKey -Name $valueName -ErrorAction SilentlyContinue).$valueName
  foreach ($line in @($values)) {
    if ($line -match '^([0-9A-Fa-f]+)=(.*)$') { $map[$Matches[1].ToUpper()] = $Matches[2].ToUpper() }
  }
  return $map
}

function Set-Keymap([string]$valueName, $map) {
  $lines = [string[]]@($map.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" })
  if (-not (Test-Path $ParametersKey)) { New-Item -Path $ParametersKey -Force | Out-Null }
  New-ItemProperty -Path $ParametersKey -Name $valueName -PropertyType MultiString -Value $lines -Force | Out-Null
}

function Show-Keymap([string]$title, $map) {
  "$title"
  if ($map.Count -eq 0) { '  (empty)'; return }
  foreach ($entry in $map.GetEnumerator()) {
    $target = if ($entry.Value) { ($entry.Value -split '\+' | ForEach-Object { ConvertTo-Name $_ }) -join '+' } else { 'None (disabled)' }
    '  {0,-12} -> {1}' -f (ConvertTo-Name $entry.Key), $target
  }
}

function Restart-OpenMagicKeyboards {
  $devices = Get-PnpDevice -PresentOnly -Class Keyboard | Where-Object {
    (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_LowerFilters -ErrorAction SilentlyContinue).Data -contains 'OpenMagicKbd'
  }
  if (-not $devices) { Write-Warning 'No keyboard is using the OpenMagicKbd driver right now.'; return }
  foreach ($d in $devices) {
    & pnputil.exe /restart-device "$($d.InstanceId)" | Out-Null
    "Restarted: $($d.FriendlyName)"
  }
}

function Assert-Admin {
  $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Changing the keymap needs an elevated (Administrator) PowerShell.'
  }
}

$valueName = if ($Fn) { 'FnMap' } else { 'Map' }
switch ($Command) {
  'list' {
    Show-Keymap 'Map (always):' (Get-Keymap 'Map')
    ''
    Show-Keymap 'FnMap (while Fn is held):' (Get-Keymap 'FnMap')
  }
  'keys' { ($KeyCodes.Keys | Sort-Object) -join ', ' }
  'set' {
    if (-not $Key -or -not $Target) { throw 'Usage: set <Key> <Target|Key+Key|None> [-Fn]' }
    Assert-Admin
    $source = ConvertTo-Code $Key
    if ($source -eq 0xE11D) { throw 'Pause cannot be remapped as a source key.' }
    $parts = if ($Target -ieq 'None') { @() } else { $Target -split '\+' }
    if ($parts.Count -gt 3) { throw 'A target can combine at most 3 keys.' }
    $map = Get-Keymap $valueName
    $map[(ConvertTo-Hex $source)] = ($parts | ForEach-Object { ConvertTo-Hex (ConvertTo-Code $_) }) -join '+'
    Set-Keymap $valueName $map
    Restart-OpenMagicKeyboards
  }
  'remove' {
    if (-not $Key) { throw 'Usage: remove <Key> [-Fn]' }
    Assert-Admin
    $map = Get-Keymap $valueName
    $map.Remove((ConvertTo-Hex (ConvertTo-Code $Key)))
    Set-Keymap $valueName $map
    Restart-OpenMagicKeyboards
  }
  'apply' { Assert-Admin; Restart-OpenMagicKeyboards }
}
