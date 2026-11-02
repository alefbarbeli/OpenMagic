<#
.SYNOPSIS
  Creates the self-signed code-signing certificate used to test-sign the
  OpenMagic driver packages.

.DESCRIPTION
  The private key stays in the current user's certificate store
  (Cert:\CurrentUser\My); only the public certificate is exported, to
  certs\OpenMagic-TestSigning.cer, which the installer trusts on the target
  machine. Keep a private backup of the key (Export-PfxCertificate) so future
  releases are signed by the same certificate.
#>
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$subject = 'CN=OpenMagic Test Signing'
$certPath = Join-Path $PSScriptRoot '..\certs\OpenMagic-TestSigning.cer'

$existing = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey }
if ($existing -and -not $Force) {
  "Signing certificate already exists: $($existing[0].Thumbprint)"
  $cert = $existing[0]
} else {
  $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -FriendlyName 'OpenMagic Test Signing' `
    -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(10)
  "Created signing certificate: $($cert.Thumbprint)"
}

New-Item -ItemType Directory -Force -Path (Split-Path $certPath) | Out-Null
Export-Certificate -Cert $cert -FilePath $certPath -Type CERT | Out-Null
"Public certificate: $((Resolve-Path $certPath).Path)"
