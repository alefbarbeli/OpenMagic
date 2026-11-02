<#
.SYNOPSIS
  Builds the OpenMagic drivers and the OpenMagic installer.

.DESCRIPTION
  1. Builds both driver projects with MSBuild (Visual Studio + WDK).
  2. Signs the .sys and .cat files with the project test certificate
     (CN=OpenMagic Test Signing, see new-signing-certificate.ps1).
  3. Verifies the INF files with the x64 infverif.
  4. Stages the driver packages in out\package and, unless -NoInstaller,
     compiles installer\OpenMagic.iss into out\OpenMagic-Setup-<version>.exe.
#>
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release',
  [switch]$NoInstaller
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out = Join-Path $root 'out'
$version = ([xml](Get-Content (Join-Path $root 'Directory.Build.props'))).Project.PropertyGroup.OpenMagicVersion |
  Where-Object { $_ } | Select-Object -First 1

# --- Tools -------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Visual Studio is not installed (see scripts\setup-dev.ps1).' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$signtool = Get-ChildItem "$kits\bin\10.*\x64\signtool.exe" | Sort-Object FullName | Select-Object -Last 1
$infverif = Get-ChildItem "$kits\Tools\10.*\x64\infverif.exe" | Sort-Object FullName | Select-Object -Last 1
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
  Where-Object { Test-Path $_ } | Select-Object -First 1
foreach ($tool in @{ MSBuild = $msbuild; signtool = $signtool; infverif = $infverif }.GetEnumerator()) {
  if (-not $tool.Value) { throw "$($tool.Key) not found (see scripts\setup-dev.ps1)." }
}

$certificate = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
  Where-Object { $_.Subject -eq 'CN=OpenMagic Test Signing' -and $_.HasPrivateKey } | Select-Object -First 1
if (-not $certificate) { throw 'Signing certificate not found. Run scripts\new-signing-certificate.ps1 first.' }

function Invoke-Checked([string]$what, [scriptblock]$command) {
  & $command
  if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)." }
}

# --- Drivers -----------------------------------------------------------------------
"OpenMagic $version ($Configuration), signing with $($certificate.Thumbprint)"
Remove-Item (Join-Path $out 'package') -Recurse -Force -ErrorAction SilentlyContinue

$drivers = @(
  @{ Name = 'keyboard'; Project = 'drivers\keyboard\OpenMagicKbd.vcxproj'; Package = 'OpenMagicKbd' },
  @{ Name = 'mouse'; Project = 'drivers\mouse\OpenMagicMouse.vcxproj'; Package = 'OpenMagicMouse' }
)
foreach ($driver in $drivers) {
  $project = Join-Path $root $driver.Project
  # The 10.0.28000 kit's x86 InfVerif.dll is missing and its x86 ApiValidator
  # helper fails to start (error 193), so both MSBuild steps are skipped and
  # the x64 tools run below instead.
  Invoke-Checked "Build of $($driver.Package)" {
    & $msbuild $project /t:Rebuild /p:Configuration=$Configuration /p:Platform=x64 `
      /p:SkipPackageVerification=true /p:ApiValidator_Enable=false /m /nologo /v:minimal
  }

  $packageDir = Join-Path (Split-Path $project) "x64\$Configuration\$($driver.Package)"
  $toSign = Get-ChildItem $packageDir -Include *.sys, *.cat -Recurse | ForEach-Object FullName
  Invoke-Checked "Signing of $($driver.Package)" {
    & $signtool.FullName sign /q /fd sha256 /sha1 $certificate.Thumbprint /d OpenMagic $toSign
  }
  foreach ($inf in Get-ChildItem $packageDir -Filter *.inf) {
    Invoke-Checked "INF verification of $($inf.Name)" { & $infverif.FullName $inf.FullName | Out-Null }
  }

  $staged = Join-Path $out "package\$($driver.Name)"
  New-Item -ItemType Directory -Force -Path $staged | Out-Null
  Copy-Item "$packageDir\*" $staged
  "  $($driver.Package): $((Get-ChildItem $staged).Name -join ', ')"
}

# --- Installer ---------------------------------------------------------------------
if ($NoInstaller) { return }
if (-not $iscc) { throw 'Inno Setup 6 not found (winget install JRSoftware.InnoSetup), or use -NoInstaller.' }

Invoke-Checked 'Installer compilation' {
  & $iscc /Qp "/DAppVersion=$version" "/DCertThumbprint=$($certificate.Thumbprint)" "/O$out" (Join-Path $root 'installer\OpenMagic.iss')
}
$setup = Join-Path $out "OpenMagic-Setup-$version.exe"
Invoke-Checked 'Signing of the installer' {
  & $signtool.FullName sign /q /fd sha256 /sha1 $certificate.Thumbprint /d 'OpenMagic Setup' $setup
}
"Installer: $setup"
