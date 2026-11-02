# Installs the test-signed OpenMagic driver packages built by scripts\build.ps1.
# Run from an elevated PowerShell with test-signing active (bcdedit /set
# testsigning on + reboot). The devices restart while installing, so keep the
# laptop keyboard/touchpad at hand.
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release',
  [switch]$KeyboardOnly,
  [switch]$MouseOnly,
  # Turns on the drivers' diagnostic trace (Parameters\Debug = 1).
  [switch]$Debug
)

$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this script from an elevated (Administrator) PowerShell.'
}
$startOptions = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control').SystemStartOptions
if ($startOptions -notmatch 'TESTSIGNING') {
  throw 'Test-signing is not active yet. Run "bcdedit /set testsigning on" and reboot first.'
}

$drivers = Join-Path $PSScriptRoot '..\drivers'
$packages = @()
if (-not $MouseOnly) {
  $packages += [pscustomobject]@{ Dir = "$drivers\keyboard\x64\$Configuration\OpenMagicKbd"; Infs = @('OpenMagicKbd.inf') }
}
if (-not $KeyboardOnly) {
  # Bluetooth side first: it is the one that turns multitouch on.
  $packages += [pscustomobject]@{ Dir = "$drivers\mouse\x64\$Configuration\OpenMagicMouse"; Infs = @('OpenMagicMouseBt.inf', 'OpenMagicMouse.inf') }
}

foreach ($service in 'OpenMagicKbd', 'OpenMagicMouse') {
  $parameters = "HKLM:\SYSTEM\CurrentControlSet\Services\$service\Parameters"
  if ($Debug) {
    New-Item -Path $parameters -Force | Out-Null
    New-ItemProperty -Path $parameters -Name Debug -PropertyType DWord -Value 1 -Force | Out-Null
    Remove-ItemProperty -Path $parameters -Name DebugLog -ErrorAction SilentlyContinue
  } elseif (Test-Path $parameters) {
    Remove-ItemProperty -Path $parameters -Name Debug, DebugLog -ErrorAction SilentlyContinue
  }
}

foreach ($package in $packages) {
  if (-not (Test-Path $package.Dir)) { throw "Package not found: $($package.Dir) (run scripts\build.ps1 first)" }

  # Trust the WDK test certificate that signed this package.
  $catalog = Get-ChildItem $package.Dir -Filter *.cat | Select-Object -First 1
  $certificate = (Get-AuthenticodeSignature $catalog.FullName).SignerCertificate
  if (-not $certificate) { throw "$($catalog.Name) is not signed." }
  foreach ($storeName in 'Root', 'TrustedPublisher') {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, 'LocalMachine')
    $store.Open('ReadWrite')
    $store.Add($certificate)
    $store.Close()
  }
  "Trusted test certificate: $($certificate.Subject)"

  foreach ($inf in $package.Infs) {
    & pnputil.exe /add-driver (Join-Path $package.Dir $inf) /install
    if ($LASTEXITCODE -notin 0, 3010) { throw "pnputil failed for $inf (exit $LASTEXITCODE)" }
  }
}

''
'Installed. Devices using the drivers:'
Get-PnpDevice -PresentOnly | Where-Object {
  $filters = @((Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_LowerFilters, DEVPKEY_Device_UpperFilters -ErrorAction SilentlyContinue).Data)
  $filters -contains 'OpenMagicKbd' -or $filters -contains 'OpenMagicMouse'
} | ForEach-Object { '  {0,-8} {1}' -f $_.Status, $_.FriendlyName }
