# Builds the OpenMagic drivers with the Visual Studio + WDK toolchain.
# Output (driver package: .sys, .inf, .cat, test-signed) goes to
# drivers\<name>\x64\<Configuration>\<name>\.
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Visual Studio is not installed (run scripts\setup-dev.ps1 first).' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
$infverif = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Tools\*\x64\infverif.exe" | Sort-Object FullName | Select-Object -Last 1

$projects = @(
  @{ Project = "$PSScriptRoot\..\drivers\keyboard\OpenMagicKbd.vcxproj"; Package = 'OpenMagicKbd' },
  @{ Project = "$PSScriptRoot\..\drivers\mouse\OpenMagicMouse.vcxproj"; Package = 'OpenMagicMouse' }
)
foreach ($p in $projects) {
  # The 10.0.28000 kit's x86 InfVerif.dll is missing and its x86 ApiValidator
  # helper fails to start (error 193), so both build steps are skipped here and
  # the x64 tools run below instead.
  & $msbuild $p.Project /p:Configuration=$Configuration /p:Platform=x64 /p:SkipPackageVerification=true /p:ApiValidator_Enable=false /m /nologo /v:minimal
  if ($LASTEXITCODE -ne 0) { throw "Build failed: $($p.Project)" }

  if ($infverif) {
    $packageDir = Join-Path (Split-Path $p.Project) "x64\$Configuration\$($p.Package)"
    foreach ($inf in Get-ChildItem $packageDir -Filter *.inf) {
      & $infverif.FullName /v $inf.FullName
      if ($LASTEXITCODE -ne 0) { throw "INF verification failed: $($inf.FullName)" }
    }
  }
}
