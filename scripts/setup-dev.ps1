<#
.SYNOPSIS
  Prepares a machine to build (and optionally test) the OpenMagic drivers.

.DESCRIPTION
  Run from an elevated Windows PowerShell. Installs, through winget:
    - Visual Studio 2026 Community with C++, Spectre-mitigated libraries and
      the Windows Driver Kit extension
    - Windows SDK and Windows Driver Kit 10.0.28000 (matching build numbers)
  With -EnableTestSigning it also turns on Windows test-signing mode, which the
  test-signed drivers need in order to load. That requires Secure Boot to be
  off; BitLocker, if on, is suspended for the next reboot so the boot
  configuration change doesn't trigger recovery. A system restore point is
  created first.

.EXAMPLE
  .\setup-dev.ps1 -EnableTestSigning
#>
param(
  [switch]$EnableTestSigning,
  [string]$LogFile = (Join-Path $PSScriptRoot 'setup-dev.log')
)

function Log([string]$message) {
  $line = '[{0:HH:mm:ss}] {1}' -f (Get-Date), $message
  $line
  $line | Out-File -FilePath $LogFile -Append -Encoding utf8
}

function Invoke-Logged([string]$exe, [string[]]$arguments) {
  Log "> $exe $($arguments -join ' ')"
  & $exe @arguments 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_.Trim() -and $_ -notmatch '^\s*[-\\|/]\s*$' -and $_ -notmatch '[▀-▟]' } |
    Out-File -FilePath $LogFile -Append -Encoding utf8
  Log "exit code: $LASTEXITCODE"
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this script from an elevated (Administrator) PowerShell.'
}

Log '=== OpenMagic development setup ==='

if ($EnableTestSigning) {
  $secureBoot = try { Confirm-SecureBootUEFI } catch { $false }
  if ($secureBoot) {
    throw 'Secure Boot is on. Test-signing cannot be enabled until Secure Boot is turned off in the firmware settings.'
  }

  try {
    Checkpoint-Computer -Description 'Before enabling test-signing (OpenMagic)' -RestorePointType MODIFY_SETTINGS -ErrorAction Stop
    Log 'Restore point created.'
  } catch { Log "Restore point not created: $($_.Exception.Message)" }

  $bitlocker = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction SilentlyContinue
  if ($bitlocker -and $bitlocker.ProtectionStatus -eq 'On') {
    Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 -ErrorAction Stop | Out-Null
    Log 'BitLocker suspended for the next reboot.'
  }
  Invoke-Logged 'bcdedit.exe' @('/set', 'testsigning', 'on')
  Log 'Test-signing will be active after a reboot.'
}

$winget = @('--exact', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
$vsOverride = '--wait --quiet --norestart ' +
  '--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended ' +
  '--add Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre ' +
  '--add Component.Microsoft.Windows.DriverKit'

Log 'Installing Visual Studio 2026 Community (this takes a while)...'
Invoke-Logged 'winget.exe' (@('install', '--id', 'Microsoft.VisualStudio.Community', '--override', $vsOverride) + $winget)
Log 'Installing Windows SDK 10.0.28000...'
Invoke-Logged 'winget.exe' (@('install', '--id', 'Microsoft.WindowsSDK.10.0.28000', '--silent') + $winget)
Log 'Installing Windows Driver Kit 10.0.28000...'
Invoke-Logged 'winget.exe' (@('install', '--id', 'Microsoft.WindowsWDK.10.0.28000', '--silent') + $winget)

$wdk = Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.28000.0\km\wdm.h"
Log "Windows Driver Kit headers present: $wdk"
Log '=== Done ==='
