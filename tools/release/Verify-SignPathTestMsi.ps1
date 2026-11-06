# Fail-closed test-certificate verification for the SignPath Foundation MSI test.
# Only execute on an ephemeral GitHub-hosted Windows runner. Do not install MSI.
# A SHA-256-pinned *public* test certificate is temporarily trusted only to
# allow Windows to verify the test signature cryptographically, then removed.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$UnsignedMsi,
  [Parameter(Mandatory=$true)][string]$SignedDirectory,
  [Parameter(Mandatory=$true)][string]$ManifestPath,
  [Parameter(Mandatory=$true)][string]$ExpectedFilename,
  [Parameter(Mandatory=$true)][string]$ExpectedCertificateSha256,
  [Parameter(Mandatory=$true)][string]$ReportPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Ok([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function Get-MsiProperties([string]$Path) {
  $installer = $null; $db = $null; $view = $null
  try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $db = $installer.OpenDatabase($Path, 0)
    $view = $db.OpenView('SELECT `Property`, `Value` FROM `Property`')
    $null = $view.Execute()
    $properties = @{}
    while ($true) {
      $record = $view.Fetch()
      if ($null -eq $record) { break }
      try { $properties[[string]$record.StringData(1)] = [string]$record.StringData(2) }
      finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record) }
    }
    return $properties
  } finally {
    if ($null -ne $view) { try { $null = $view.Close() } catch {}; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null }
    if ($null -ne $db) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null }
    if ($null -ne $installer) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null }
  }
}

function Get-ExtractedFileHashMap([string]$Msi, [string]$Destination) {
  New-Item -ItemType Directory -Path $Destination -Force | Out-Null
  $arguments = @('/a', ('"' + $Msi + '"'), '/qn', ('TARGETDIR="' + $Destination + '"'))
  $process = Start-Process -FilePath "$env:WINDIR\System32\msiexec.exe" -ArgumentList $arguments -Wait -PassThru
  Assert-Ok ($process.ExitCode -eq 0) "MSI administrative extraction failed: $($process.ExitCode)"
  $root = [IO.Path]::GetFullPath($Destination).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
  $map = @{}
  foreach ($item in @(Get-ChildItem -LiteralPath $Destination -Recurse -File -ErrorAction Stop)) {
    # msiexec /a may create a new administrative MSI database; that DB differs
    # when the original MSI acquires a signature. Compare installed payload only.
    if ($item.Extension -ieq '.msi') { continue }
    $relative = $item.FullName.Substring($root.Length)
    Assert-Ok (-not $map.ContainsKey($relative)) "Duplicate administrative image path: $relative"
    $map[$relative] = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
  }
  Assert-Ok ($map.Count -ge 10) "Unexpectedly small MSI payload: $($map.Count) files"
  return ,$map
}

Assert-Ok ([bool]($ExpectedCertificateSha256 -match '^[a-fA-F0-9]{64}$')) 'Expected test certificate SHA256 must be exactly 64 hex characters.'
Assert-Ok ($ExpectedFilename -ceq [IO.Path]::GetFileName($ExpectedFilename)) 'Expected filename must not contain a directory.'
$UnsignedMsi = (Resolve-Path -LiteralPath $UnsignedMsi).ProviderPath
$SignedDirectory = (Resolve-Path -LiteralPath $SignedDirectory).ProviderPath
$ManifestPath = (Resolve-Path -LiteralPath $ManifestPath).ProviderPath
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-Ok ($manifest.status -ceq 'UNSIGNED_CANDIDATE_NOT_FOR_RELEASE') 'Input was not a verified unsigned candidate.'
Assert-Ok ($manifest.release_filename -ceq $ExpectedFilename) 'Manifest filename mismatch.'
Assert-Ok ([IO.Path]::GetFileName($UnsignedMsi) -ceq $ExpectedFilename) 'Unsigned input has the wrong filename.'
Assert-Ok ((Get-FileHash -LiteralPath $UnsignedMsi -Algorithm SHA256).Hash -ieq [string]$manifest.unsigned_msi_sha256) 'Original unsigned SHA-256 mismatch.'
Assert-Ok ((Get-AuthenticodeSignature -LiteralPath $UnsignedMsi).Status -eq 'NotSigned') 'Original input was signed.'

$signedCandidates = @(Get-ChildItem -LiteralPath $SignedDirectory -Recurse -File -ErrorAction Stop)
Assert-Ok ($signedCandidates.Count -eq 1) "Expected exactly one returned signed file; found $($signedCandidates.Count)."
$signed = $signedCandidates[0]
Assert-Ok ($signed.Name -ceq $ExpectedFilename) "SignPath returned a different filename: $($signed.Name)"
Assert-Ok ($signed.FullName -cne $UnsignedMsi) 'Signed and unsigned paths must be distinct.'
$signature = Get-AuthenticodeSignature -LiteralPath $signed.FullName
Assert-Ok ($null -ne $signature.SignerCertificate) 'SignPath result has no embedded Authenticode signer certificate.'
$certificate = $signature.SignerCertificate
$actualPin = $certificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)
Assert-Ok ($actualPin -ieq $ExpectedCertificateSha256) "Test signer SHA-256 does not match the PINNED public certificate. Actual=$actualPin"
Assert-Ok ($signature.Status -ne 'NotSigned') 'Signed MSI is not Authenticode signed.'

$publicCertPath = Join-Path $env:RUNNER_TEMP ('signpath-test-public-' + [guid]::NewGuid().ToString('N') + '.cer')
$thumbprint = $certificate.Thumbprint
$certStorePath = "Cert:\CurrentUser\Root\$thumbprint"
Assert-Ok (-not (Test-Path -LiteralPath $certStorePath)) 'The test certificate is unexpectedly already in CurrentUser Root.'
$addedCert = $false
try {
  [IO.File]::WriteAllBytes($publicCertPath, $certificate.RawData)
  $null = Import-Certificate -FilePath $publicCertPath -CertStoreLocation 'Cert:\CurrentUser\Root' -ErrorAction Stop
  $addedCert = $true
  Assert-Ok (Test-Path -LiteralPath $certStorePath) 'Cannot find the temporarily trusted test certificate.'
  # Fail closed even if signature is well-formed but altered, has a bad EKU,
  # or is not trusted by Windows after the known certificate is pinned.
  $trustedSignature = Get-AuthenticodeSignature -LiteralPath $signed.FullName
  Assert-Ok ($trustedSignature.Status -eq 'Valid') "Windows Authenticode verification failed: $($trustedSignature.Status); $($trustedSignature.StatusMessage)"
  Assert-Ok ($trustedSignature.SignerCertificate.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256) -ieq $actualPin) 'Signing certificate changed after trust.'
} finally {
  if ($addedCert) {
    Remove-Item -LiteralPath $certStorePath -Force -ErrorAction Stop
    Assert-Ok (-not (Test-Path -LiteralPath $certStorePath)) 'Temporary test root certificate cleanup failed.'
  }
  if (Test-Path -LiteralPath $publicCertPath) { Remove-Item -LiteralPath $publicCertPath -Force -ErrorAction Stop }
}

$unsignedProperties = Get-MsiProperties $UnsignedMsi
$signedProperties = Get-MsiProperties $signed.FullName
foreach ($key in @('ProductName','Manufacturer','ProductVersion','ProductCode','UpgradeCode')) {
  Assert-Ok ($unsignedProperties.ContainsKey($key) -and $signedProperties.ContainsKey($key)) "Missing MSI property: $key"
  Assert-Ok ($unsignedProperties[$key] -ceq $signedProperties[$key]) "MSI metadata changed after signing: $key"
}
Assert-Ok ($signedProperties['ProductName'] -ceq 'Mozkey') 'Wrong MSI product name.'
Assert-Ok ($signedProperties['Manufacturer'] -ceq 'koyasi777') 'Wrong MSI manufacturer.'
Assert-Ok ($signedProperties['ProductVersion'] -ceq [string]$manifest.msi_product_version) 'Wrong MSI product version.'
Assert-Ok ($signedProperties['ProductCode'] -ceq [string]$manifest.msi_product_code) 'Wrong MSI ProductCode.'
Assert-Ok ($signedProperties['UpgradeCode'] -ceq [string]$manifest.msi_upgrade_code) 'Wrong MSI UpgradeCode.'

$scratch = Join-Path $env:RUNNER_TEMP ('mozkey-signpath-payload-' + [guid]::NewGuid().ToString('N'))
try {
  $unsignedFiles = Get-ExtractedFileHashMap $UnsignedMsi (Join-Path $scratch 'unsigned')
  $signedFiles = Get-ExtractedFileHashMap $signed.FullName (Join-Path $scratch 'signed')
  Assert-Ok ($unsignedFiles.Count -eq $signedFiles.Count) 'MSI payload file counts changed after signing.'
  foreach ($name in $unsignedFiles.Keys) {
    Assert-Ok ($signedFiles.ContainsKey($name)) "A payload file is missing after signing: $name"
    Assert-Ok ($unsignedFiles[$name] -ieq $signedFiles[$name]) "MSI packaged file changed after signing: $name"
  }
} finally {
  if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction Stop }
}

$report = [ordered]@{
  verdict = 'MOZKEY_SIGNPATH_TEST_MSI_VALIDATED'
  production_approved = $false
  signing_policy = 'test-signing'
  artifact_configuration = 'windows-msi'
  source_sha = [string]$manifest.source_sha
  github_run_id = [string]$manifest.github_run_id
  github_run_attempt = [string]$manifest.github_run_attempt
  release_version = [string]$manifest.release_version
  architecture = [string]$manifest.target_architecture
  filename = $ExpectedFilename
  unsigned_sha256 = (Get-FileHash -LiteralPath $UnsignedMsi -Algorithm SHA256).Hash
  signed_sha256 = (Get-FileHash -LiteralPath $signed.FullName -Algorithm SHA256).Hash
  certificate_sha256 = $actualPin
  certificate_subject = $certificate.Subject
  windows_authenticode_status_with_temporary_test_root = 'Valid'
  temporary_test_root_removed = $true
  packaged_files_unchanged = $true
  packaged_file_count = $unsignedFiles.Count
}
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($ReportPath)) -Force | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Host "MOZKEY_SIGNPATH_TEST_MSI_VALIDATION=PASS file=$ExpectedFilename signedSHA256=$($report.signed_sha256)"
Write-Host "Report=$ReportPath"
