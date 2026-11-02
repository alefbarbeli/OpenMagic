<#
.SYNOPSIS
  System actions behind the OpenMagic installer (run elevated, 64-bit).

.DESCRIPTION
  -Action EnableTestMode    Turns test-signing on (suspends BitLocker for one reboot).
  -Action DisableTestMode   Turns test-signing off (suspends BitLocker for one reboot).
  -Action TrustCertificate  Adds the OpenMagic test certificate to the machine's
                            Trusted Root and Trusted Publishers stores.
  -Action InstallDrivers    Installs the given INF packages on matching devices.
  -Action ScheduleInstall   Adds the packages to the driver store now and installs
                            them on the devices at the next startup (used when
                            test-signing only becomes active after a reboot).
  -Action Uninstall         Removes the driver packages, services, scheduled task
                            and certificate.
  Exit code 0 = success, 3010 = success but a restart is required, other = failure.
#>
param(
  [Parameter(Mandatory)]
  [ValidateSet('EnableTestMode', 'DisableTestMode', 'TrustCertificate', 'InstallDrivers', 'ScheduleInstall', 'Uninstall')]
  [string]$Action,
  [string]$Inf = '',          # INF paths separated by ';' (powershell -File can't pass arrays)
  [string]$Certificate,
  [string]$Thumbprint
)
$Inf = @($Inf -split ';' | Where-Object { $_ })

$ErrorActionPreference = 'Stop'
$logFile = Join-Path $PSScriptRoot 'setup.log'
$taskName = 'OpenMagic - finish driver setup'
$packages = 'openmagickbd.inf', 'openmagicmouse.inf', 'openmagicmousebt.inf'
$exitCode = 0

function Log([string]$message) {
  '[{0:yyyy-MM-dd HH:mm:ss}] [{1}] {2}' -f (Get-Date), $Action, $message | Out-File -FilePath $logFile -Append -Encoding utf8
}

function Invoke-Tool([string]$exe, [string[]]$arguments) {
  $output = & $exe @arguments 2>&1 | Out-String
  Log "> $exe $($arguments -join ' ') (exit $LASTEXITCODE)`n$($output.Trim())"
  return $LASTEXITCODE
}

function Suspend-BitLockerOnce {
  $volume = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction SilentlyContinue
  if ($volume -and $volume.ProtectionStatus -eq 'On') {
    Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 | Out-Null
    Log 'BitLocker suspended for the next reboot.'
  }
}

function Install-Packages([string[]]$infs) {
  foreach ($path in $infs) {
    $code = Invoke-Tool "$env:SystemRoot\System32\pnputil.exe" @('/add-driver', $path, '/install')
    # 259 = no matching device present yet; the package installs when one appears.
    if ($code -eq 3010) { $script:exitCode = 3010 }
    elseif ($code -notin 0, 259) { throw "pnputil failed for $path (exit $code)" }
  }
}

function Get-OpenMagicPackages {
  Get-WindowsDriver -Online | Where-Object { $packages -contains (Split-Path $_.OriginalFileName -Leaf).ToLower() }
}

# Removes every OpenMagic package from the driver store; devices fall back to
# the inbox drivers until a package is installed again.
function Remove-Packages {
  foreach ($package in Get-OpenMagicPackages) {
    Invoke-Tool "$env:SystemRoot\System32\pnputil.exe" @('/delete-driver', $package.Driver, '/uninstall', '/force') | Out-Null
  }
}

try {
  switch ($Action) {
    'EnableTestMode' {
      Suspend-BitLockerOnce
      if ((Invoke-Tool "$env:SystemRoot\System32\bcdedit.exe" @('/set', 'testsigning', 'on')) -ne 0) { throw 'bcdedit failed' }
      $exitCode = 3010
    }
    'DisableTestMode' {
      Suspend-BitLockerOnce
      if ((Invoke-Tool "$env:SystemRoot\System32\bcdedit.exe" @('/set', 'testsigning', 'off')) -ne 0) { throw 'bcdedit failed' }
      $exitCode = 3010
    }
    'TrustCertificate' {
      foreach ($store in 'Root', 'TrustedPublisher') {
        Import-Certificate -FilePath $Certificate -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
      }
      Log "Trusted $Certificate"
    }
    'InstallDrivers' {
      # Previous packages go first, so a build with an older DriverVer can
      # never outrank the one being installed.
      Remove-Packages
      foreach ($service in 'OpenMagicKbd', 'OpenMagicMouse') {
        Remove-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$service\Parameters" -Name Debug, DebugLog -ErrorAction SilentlyContinue
      }
      Install-Packages $Inf
    }
    'ScheduleInstall' {
      Remove-Packages
      foreach ($path in $Inf) {
        if ((Invoke-Tool "$env:SystemRoot\System32\pnputil.exe" @('/add-driver', $path)) -notin 0, 259) { throw "pnputil failed for $path" }
      }
      $command = "& '$PSCommandPath' -Action InstallDrivers -Inf '$($Inf -join ';')'; Unregister-ScheduledTask -TaskName '$taskName' -Confirm:`$false"
      $taskAction = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
        -Argument "-NoProfile -ExecutionPolicy Bypass -Command `"$command`""
      Register-ScheduledTask -TaskName $taskName -Action $taskAction -Trigger (New-ScheduledTaskTrigger -AtStartup) `
        -Principal (New-ScheduledTaskPrincipal -UserId 'SYSTEM' -RunLevel Highest) -Force | Out-Null
      Log "Scheduled installation at next startup: $($Inf -join ', ')"
    }
    'Uninstall' {
      Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
      Remove-Packages
      Invoke-Tool "$env:SystemRoot\System32\pnputil.exe" @('/scan-devices') | Out-Null
      foreach ($service in 'OpenMagicKbd', 'OpenMagicMouse') {
        if (Get-Service $service -ErrorAction SilentlyContinue) { Invoke-Tool "$env:SystemRoot\System32\sc.exe" @('delete', $service) | Out-Null }
      }
      if ($Thumbprint) {
        foreach ($store in 'Root', 'TrustedPublisher') {
          Remove-Item "Cert:\LocalMachine\$store\$Thumbprint" -ErrorAction SilentlyContinue
        }
        Log "Removed certificate $Thumbprint"
      }
    }
  }
} catch {
  Log "FAILED: $($_.Exception.Message)"
  exit 1
}
exit $exitCode
