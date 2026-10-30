# Windows x64 / ARM64 unsigned MSI release rehearsal verifier. Reads MSI database and performs
# admin extraction on a disposable GitHub-hosted runner; does not install MSI.
# Emits a machine-readable evidence JSON and stages exactly one MSI candidate (no signing / public release).
# This does not assert reproducibility, release readiness, or trusted origin.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Repo,
  [Parameter(Mandatory=$true)][string]$SourceMsi,
  [Parameter(Mandatory=$true)][ValidateSet('x64','arm64')][string]$Architecture,
  [Parameter(Mandatory=$true)][string]$ReleaseVersion,
  [Parameter(Mandatory=$true)][string]$SourceSha,
  [Parameter(Mandatory=$true)][string]$OutputDirectory,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function Get-MsiMetadata([string]$FilePath) {
  $installer = $null; $db = $null; $view = $null
  try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $db = $installer.OpenDatabase($FilePath, 0)  # read-only
    $properties = @{}
    $view = $db.OpenView('SELECT `Property`, `Value` FROM `Property`')
    $view.Execute()
    while ($true) {
      $record = $view.Fetch()
      if ($null -eq $record) { break }
      $properties[[string]$record.StringData(1)] = [string]$record.StringData(2)
      [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
    }
    $view.Close(); [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view); $view = $null
    $files = New-Object 'System.Collections.Generic.List[string]'
    $view = $db.OpenView('SELECT `FileName` FROM `File`')
    $view.Execute()
    while ($true) {
      $record = $view.Fetch()
      if ($null -eq $record) { break }
      $files.Add((([string]$record.StringData(1) -split '\|')[-1]))
      [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
    }
    $view.Close(); [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view); $view = $null
    return [pscustomobject]@{ Properties=$properties; Files=@($files.ToArray()) }
  } finally {
    if ($null -ne $view) { try { $view.Close() } catch {}; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view) }
    if ($null -ne $db) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($db) }
    if ($null -ne $installer) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer) }
  }
}

function Find-UniqueExtracted([string]$Root, [string]$Name) {
  $found = @(Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction Stop |
    Where-Object { $_.Name -ieq $Name })
  Assert-Condition ($found.Count -eq 1) "Expected exactly one extracted $Name; found $($found.Count)"
  return $found[0].FullName
}

function Assert-Sha([string]$File, [string]$Sha) {
  $actual = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash
  Assert-Condition ($actual -ieq $Sha) "SHA256 mismatch for $File. Expected=$Sha Actual=$actual"
  return $actual.ToUpperInvariant()
}

function Get-PeMachine([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  $r = New-Object IO.BinaryReader($stream)
  try {
    Assert-Condition ($r.ReadUInt16() -eq 0x5A4D) "Not a PE binary: $Path"
    $stream.Seek(0x3C,[IO.SeekOrigin]::Begin) | Out-Null
    $pe = $r.ReadInt32()
    Assert-Condition ($pe -ge 0x40 -and $pe -lt ($stream.Length - 6)) "Invalid PE offset: $Path"
    $stream.Seek($pe,[IO.SeekOrigin]::Begin) | Out-Null
    Assert-Condition ($r.ReadUInt32() -eq 0x00004550) "Invalid PE header: $Path"
    return ('0x{0:X4}' -f $r.ReadUInt16())
  } finally { $r.Dispose(); $stream.Dispose() }
}

$Repo = (Resolve-Path -LiteralPath $Repo).ProviderPath
$SourceMsi = (Resolve-Path -LiteralPath $SourceMsi).ProviderPath
$versionMatch = [regex]::Match($ReleaseVersion, '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')
Assert-Condition $versionMatch.Success 'Invalid canonical release version'
Assert-Condition ([bool]($SourceSha -match '^[a-fA-F0-9]{40}$')) 'Invalid source SHA'

$major = [int]$versionMatch.Groups[1].Value
$minor = [int]$versionMatch.Groups[2].Value
$patch = [int]$versionMatch.Groups[3].Value
Assert-Condition ($major -le 155 -and $minor -le 255 -and $patch -le 65535) 'MSI version out of range'
$expectedMsiVersion = "$(100+$major).$minor.$patch"

$lockPath = Join-Path $Repo 'src\zenz_runtime\llama_runtime.lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$archSpecs = @{
  x64 = @{ machine = '0x8664'; locked = $lock.windows.x64 }
  arm64 = @{ machine = '0xAA64'; locked = $lock.windows.arm64 }
}
$archContract = $archSpecs[$Architecture]
Assert-Condition ($null -ne $archContract) "Unsupported architecture: $Architecture"
$metadata = Get-MsiMetadata $SourceMsi
$props = $metadata.Properties
foreach ($p in @('ProductName','ProductVersion','ProductCode','UpgradeCode','Manufacturer')) {
  Assert-Condition ($props.ContainsKey($p) -and -not [string]::IsNullOrEmpty($props[$p])) "Missing MSI Property: $p"
}
Assert-Condition ($props.ProductName -ceq 'Mozkey') 'Unexpected MSI ProductName'
Assert-Condition ($props.Manufacturer -ceq 'koyasi777') 'Unexpected MSI Manufacturer'
Assert-Condition ($props.ProductVersion -ceq $expectedMsiVersion) "MSI ProductVersion mismatch: $($props.ProductVersion), expected $expectedMsiVersion"
Assert-Condition ($props.UpgradeCode.Trim('{}') -ieq 'DD94B570-B5E2-4100-9D42-61930C611D8A') 'Unexpected UpgradeCode (upgrade compatibility risk)'
Assert-Condition ([bool]($props.ProductCode -match '^\{?[A-Fa-f0-9]{8}-(?:[A-Fa-f0-9]{4}-){3}[A-Fa-f0-9]{12}\}?$')) 'Invalid ProductCode'
Assert-Condition ($props.ProductCode -ine $props.UpgradeCode) 'ProductCode equals UpgradeCode'

$mustContain = @('mozc_server.exe','mozc_tool.exe','mozc_renderer.exe','mozc_tip64.dll','mozc_zenz_scorer.exe','llama-server.exe','zenz-v3.2-small-Q5_K_M.gguf')
foreach ($name in $mustContain) {
  $n = @($metadata.Files | Where-Object { $_ -ieq $name }).Count
  Assert-Condition ($n -eq 1) "MSI File table expected one $name; found $n"
}

# This rehearsal is UNSIGNED by design; a future signer is a separate adapter.
$signature = Get-AuthenticodeSignature -LiteralPath $SourceMsi
Assert-Condition ($signature.Status -eq [System.Management.Automation.SignatureStatus]::NotSigned) "Expected unsigned MSI before SignPath; status=$($signature.Status)"

# Administrative extraction is NOT an installation; use a disposable runner temp.
$scratch = Join-Path $env:RUNNER_TEMP ("mozkey-msi-candidate-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -ErrorAction Stop | Out-Null
try {
  $extract = Join-Path $scratch 'extracted'
  New-Item -ItemType Directory -Path $extract -ErrorAction Stop | Out-Null
  $arguments = @('/a', ('"' + $SourceMsi + '"'), '/qn', ('TARGETDIR="' + $extract + '"'))
  $process = Start-Process -FilePath "$env:WINDIR\System32\msiexec.exe" -ArgumentList $arguments -Wait -PassThru
  Assert-Condition ($process.ExitCode -eq 0) "Administrative MSI extraction failed: $($process.ExitCode)"
  $llama = Find-UniqueExtracted $extract 'llama-server.exe'
  $model = Find-UniqueExtracted $extract 'zenz-v3.2-small-Q5_K_M.gguf'
  $scorer = Find-UniqueExtracted $extract 'mozc_zenz_scorer.exe'
  $llamaHash = Assert-Sha $llama $archContract.locked.sha256
  $modelHash = Assert-Sha $model $lock.model.sha256
  Assert-Condition ((Get-PeMachine $llama) -eq $archContract.machine) "llama-server architecture does not match $Architecture"
  Assert-Condition ((Get-PeMachine $scorer) -eq $archContract.machine) "Zenz scorer architecture does not match $Architecture"

  New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
  New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
  $dest = Join-Path $OutputDirectory "Mozkey_v${ReleaseVersion}_${Architecture}.msi"
  Assert-Condition (-not (Test-Path -LiteralPath $dest)) "Release candidate already exists: $dest"
  Copy-Item -LiteralPath $SourceMsi -Destination $dest -ErrorAction Stop
  $originalHash = (Get-FileHash -LiteralPath $SourceMsi -Algorithm SHA256).Hash
  $stagedHash = Assert-Sha $dest $originalHash
  $identity = [ordered]@{
    status = 'UNSIGNED_CANDIDATE_NOT_FOR_RELEASE'
    distribution_approved = $false
    signing_provider = $null
    source_inputs_reproducible = $false
    target_architecture = $Architecture
    expected_pe_machine = $archContract.machine
    release_version = $ReleaseVersion
    msi_product_version = $props.ProductVersion
    msi_product_name = $props.ProductName
    msi_product_code = $props.ProductCode
    msi_upgrade_code = $props.UpgradeCode
    msi_manufacturer = $props.Manufacturer
    source_sha = $SourceSha
    github_run_id = $env:GITHUB_RUN_ID
    github_run_attempt = $env:GITHUB_RUN_ATTEMPT
    github_run_url = if ($env:GITHUB_RUN_ID) { "https://github.com/koyasi777/mozkey/actions/runs/$($env:GITHUB_RUN_ID)" } else { '' }
    unsigned_msi_name = [IO.Path]::GetFileName($dest)
    unsigned_msi_sha256 = $stagedHash
    unsigned_msi_size = (Get-Item -LiteralPath $dest).Length
    pinned_model_sha256 = $modelHash
    pinned_llama_sha256 = $llamaHash
    file_table_entry_count = @($metadata.Files).Count
  }
  $jsonPath = Join-Path $EvidenceDirectory 'candidate-manifest.json'
  $identity | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
  Write-Host 'MOZKEY_UNSIGNED_RELEASE_CANDIDATE=PASS'
  Write-Host "Candidate=$dest"
  Write-Host "SHA256=$stagedHash"
  Write-Host "Evidence=$jsonPath"
} finally {
  if (Test-Path -LiteralPath $scratch -PathType Container) {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
  }
}
