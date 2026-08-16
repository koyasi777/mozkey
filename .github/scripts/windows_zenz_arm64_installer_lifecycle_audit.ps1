#requires -Version 7.0
param(
  [Parameter(Mandatory)][string]$BaselineArtifactDir,
  [Parameter(Mandatory)][string]$CurrentArtifactDir,
  [Parameter(Mandatory)][string]$EvidenceDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ExpectedBaselineCommit = "c986db2c611c7255ba92b1d44fc6ac3fb6b098bf"
$ExpectedBaselineLlamaSha = "A826D6BC47645A9E9907FEEA49109C0FF25D49CFD268D3349AC5F9BD6DBAAEC0"
$ExpectedCurrentLlamaSha = "0ACD17D6EE5E361AFC06A7DCCD06EF012A00669546535889B874D5C3BB3DE81B"
$ExpectedModelSha = "29C223D4C23327B80FD13EBB5AB2555057A46317997D5DA391584FFBEF0DB673"
$ExpectedUpgradeCode = "{DD94B570-B5E2-4100-9D42-61930C611D8A}"

$LegacyDllHashes = [ordered]@{
  "ggml.dll" = "31E1386B1BEF31960ABB1A4EA0B69E96A55C2B6D52F020B66A975FC5502D3923"
  "ggml-base.dll" = "A7B2422409ABDA540542159A70A718CBF79C74D07694760CB0BFE890AC24E596"
  "ggml-cpu.dll" = "F90C7A8C0FB170A60FA5B9FBA2C8BCF4E8C25C5BD26FD9A3C022A1DB4103D73A"
  "llama.dll" = "05DAFB583D5924163C78AC8F2E0D7A1838A8258CD77EE846E20A7AB40B214D61"
}

$ExpectedFirewallRules = @(
  "Mozc Offline - Block mozc_server outbound"
  "Mozc Offline - Block mozc_tool outbound"
  "Mozc Offline - Block mozc_renderer outbound"
  "Mozc Offline - Block mozc_broker outbound"
  "Mozc Offline - Block mozc_cache_service outbound"
  "Mozc Offline - Block mozc_zenz_scorer outbound"
  "Mozc Offline - Block llama-server outbound"
)

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

function Get-Utf8Text {
  param([Parameter(Mandatory)][string]$Base64)
  return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Base64))
}


function Get-OneMsi {
  param([Parameter(Mandatory)][string]$Root)
  $items = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter *.msi)
  if ($items.Count -ne 1) {
    throw "Expected exactly one MSI under ${Root}; found $($items.Count)"
  }
  return $items[0].FullName
}

function Get-MsiProperty {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Name
  )

  $type = [type]::GetTypeFromProgID("WindowsInstaller.Installer")
  if (-not $type) {
    throw "WindowsInstaller.Installer COM type is unavailable."
  }

  $installer = [Activator]::CreateInstance($type)
  $database = $null
  $view = $null
  $record = $null

  try {
    $database = $installer.OpenDatabase($Path, 0)
    $query = "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$Name'"
    $view = $database.OpenView($query)
    $view.Execute()
    $record = $view.Fetch()
    if (-not $record) {
      throw "MSI property not found: $Name"
    }
    return [string]$record.StringData(1)
  }
  finally {
    foreach ($obj in @($record, $view, $database, $installer)) {
      if ($obj) {
        try { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($obj) } catch {}
      }
    }
  }
}

function Get-PEMachine {
  param([Parameter(Mandatory)][string]$Path)

  $fs = $null
  $br = $null
  try {
    $fs = [System.IO.File]::Open(
      $Path,
      [System.IO.FileMode]::Open,
      [System.IO.FileAccess]::Read,
      [System.IO.FileShare]::ReadWrite
    )
    $br = [System.IO.BinaryReader]::new($fs)
    if ($br.ReadUInt16() -ne 0x5A4D) { return "NOT_PE" }

    $fs.Seek(0x3C, [System.IO.SeekOrigin]::Begin) | Out-Null
    $offset = $br.ReadUInt32()
    $fs.Seek($offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    if ($br.ReadUInt32() -ne 0x00004550) { return "INVALID_PE" }

    $machine = $br.ReadUInt16()
    switch ($machine) {
      0xAA64 { return "ARM64" }
      0x8664 { return "x64" }
      0x014c { return "x86" }
      default { return ("0x{0:X4}" -f $machine) }
    }
  }
  finally {
    if ($br) { $br.Dispose() }
    elseif ($fs) { $fs.Dispose() }
  }
}

if (-not ("NativeMsi" -as [type])) {
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class NativeMsi {
  [DllImport("msi.dll", CharSet = CharSet.Unicode)]
  public static extern int MsiQueryProductState(string szProduct);
}
"@
}

function Get-MsiProductState {
  param([Parameter(Mandatory)][string]$ProductCode)
  return [NativeMsi]::MsiQueryProductState($ProductCode)
}

function Invoke-Msi {
  param(
    [Parameter(Mandatory)][ValidateSet("install","uninstall")][string]$Mode,
    [Parameter(Mandatory)][string]$Msi,
    [Parameter(Mandatory)][string]$Log
  )

  $switch = if ($Mode -eq "install") { "/i" } else { "/x" }
  $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList @(
    $switch,
    "`"$Msi`"",
    "/qn",
    "/norestart",
    "/l*v",
    "`"$Log`""
  )

  Write-Host "msiexec $Mode exit = $($p.ExitCode)"
  if ($p.ExitCode -notin @(0, 3010)) {
    throw "msiexec $Mode failed: $($p.ExitCode)"
  }
  return $p.ExitCode
}

function Resolve-MozcInstallDir {
  $roots = @(
    [Environment]::GetEnvironmentVariable("ProgramFiles")
    [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    [Environment]::GetEnvironmentVariable("ProgramW6432")
  ) |
    Where-Object { $_ } |
    ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd("\") } |
    Select-Object -Unique

  $matches = @()
  foreach ($root in $roots) {
    $candidate = Join-Path $root "Mozc"
    if ((Test-Path (Join-Path $candidate "mozc_zenz_scorer.exe") -PathType Leaf) -and
        (Test-Path (Join-Path $candidate "llama-server.exe") -PathType Leaf) -and
        (Test-Path (Join-Path $candidate "models\zenz-v3.2-small-Q5_K_M.gguf") -PathType Leaf)) {
      $matches += $candidate
    }
  }

  if ($matches.Count -ne 1) {
    throw "Expected exactly one installed Mozc directory; found $($matches.Count)"
  }
  return $matches[0]
}

function Stop-InstalledZenzProcesses {
  param([Parameter(Mandatory)][string]$Root)

  foreach ($name in @("mozc_zenz_scorer", "llama-server")) {
    foreach ($p in @(Get-Process $name -ErrorAction SilentlyContinue)) {
      $path = ""
      try { $path = [string]$p.Path } catch {}
      if ($path -and $path.StartsWith(
          $Root,
          [System.StringComparison]::OrdinalIgnoreCase)) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
      }
    }
  }
  Start-Sleep -Seconds 2
}

function Read-Exactly {
  param(
    [Parameter(Mandatory)][System.IO.Stream]$Stream,
    [Parameter(Mandatory)][int]$Count
  )

  $buffer = [byte[]]::new($Count)
  $offset = 0
  while ($offset -lt $Count) {
    $read = $Stream.Read($buffer, $offset, $Count - $offset)
    if ($read -le 0) {
      throw "Unexpected EOF while reading named-pipe response."
    }
    $offset += $read
  }
  return ,$buffer
}

function Invoke-ZenzPipeRequest {
  param(
    [Parameter(Mandatory)][string]$Prompt,
    [uint32]$Generation,
    [uint32]$TimeoutMsec = 5000,
    [uint32]$MaxOutputChars = 32
  )

  $promptBytes = [Text.Encoding]::UTF8.GetBytes($Prompt)
  $pipe = [IO.Pipes.NamedPipeClientStream]::new(
    ".",
    "mozc_zenz_scorer",
    [IO.Pipes.PipeDirection]::InOut,
    [IO.Pipes.PipeOptions]::None
  )

  try {
    $pipe.Connect(7000)
    $headerStream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($headerStream, [Text.Encoding]::UTF8, $true)
    try {
      $writer.Write([uint32]0x315A4E5A)
      $writer.Write([uint16]1)
      $writer.Write([uint16]1)
      $writer.Write([uint32]$Generation)
      $writer.Write([uint32]$TimeoutMsec)
      $writer.Write([uint32]$MaxOutputChars)
      $writer.Write([uint32]$promptBytes.Length)
      $writer.Flush()
      $header = $headerStream.ToArray()
    }
    finally {
      $writer.Dispose()
      $headerStream.Dispose()
    }

    if ($header.Length -ne 24) {
      throw "Unexpected Zenz request header length: $($header.Length)"
    }

    $pipe.Write($header, 0, $header.Length)
    $pipe.Write($promptBytes, 0, $promptBytes.Length)
    $pipe.Flush()

    $responseHeader = Read-Exactly -Stream $pipe -Count 28
    $responseStream = [IO.MemoryStream]::new($responseHeader, $false)
    $reader = [IO.BinaryReader]::new($responseStream, [Text.Encoding]::UTF8, $true)
    try {
      $magic = $reader.ReadUInt32()
      $version = $reader.ReadUInt16()
      $kind = $reader.ReadUInt16()
      $responseGeneration = $reader.ReadUInt32()
      $status = $reader.ReadUInt32()
      $latency = $reader.ReadUInt32()
      $valueSize = $reader.ReadUInt32()
      $debugSize = $reader.ReadUInt32()
    }
    finally {
      $reader.Dispose()
      $responseStream.Dispose()
    }

    if ($magic -ne 0x315A4E5A -or $version -ne 1 -or $kind -ne 2) {
      throw "Invalid Zenz response header."
    }
    if ($responseGeneration -ne $Generation) {
      throw "Zenz generation mismatch."
    }
    if ($valueSize -gt 65536 -or $debugSize -gt 65536) {
      throw "Zenz response exceeds audit limit."
    }

    $valueBytes = if ($valueSize) {
      Read-Exactly -Stream $pipe -Count ([int]$valueSize)
    } else {
      [byte[]]::new(0)
    }
    $debugBytes = if ($debugSize) {
      Read-Exactly -Stream $pipe -Count ([int]$debugSize)
    } else {
      [byte[]]::new(0)
    }

    return [pscustomobject]@{
      Status = $status
      LatencyMsec = $latency
      Value = [Text.Encoding]::UTF8.GetString($valueBytes)
      Debug = [Text.Encoding]::UTF8.GetString($debugBytes)
    }
  }
  finally {
    $pipe.Dispose()
  }
}

function Assert-FirewallRuleCount {
  param([Parameter(Mandatory)][int]$Expected)

  $total = 0
  foreach ($name in $ExpectedFirewallRules) {
    $count = @(Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue).Count
    if ($Expected -eq 7) {
      if ($count -ne 1) {
        throw "Expected one firewall rule '$name'; found $count"
      }
      $total += $count
    } else {
      if ($count -ne 0) {
        throw "Firewall rule remains unexpectedly: $name"
      }
    }
  }

  if ($Expected -eq 7 -and $total -ne 7) {
    throw "Expected 7 firewall rules; counted $total"
  }
}

function Assert-CurrentPayload {
  param([Parameter(Mandatory)][string]$InstallDir)

  $llama = Join-Path $InstallDir "llama-server.exe"
  $scorer = Join-Path $InstallDir "mozc_zenz_scorer.exe"
  $model = Join-Path $InstallDir "models\zenz-v3.2-small-Q5_K_M.gguf"

  if ((Get-PEMachine $llama) -ne "ARM64") {
    throw "Current llama-server is not ARM64."
  }
  if ((Get-FileHash $llama -Algorithm SHA256).Hash -ne $ExpectedCurrentLlamaSha) {
    throw "Current llama-server hash mismatch."
  }
  if ((Get-PEMachine $scorer) -ne "ARM64") {
    throw "Current scorer is not ARM64."
  }
  if ((Get-FileHash $model -Algorithm SHA256).Hash -ne $ExpectedModelSha) {
    throw "Current model hash mismatch."
  }

  foreach ($legacy in $LegacyDllHashes.Keys) {
    if (Test-Path (Join-Path $InstallDir $legacy)) {
      throw "Legacy runtime DLL remains after upgrade: $legacy"
    }
  }
}

function Invoke-ScorerContextGate {
  param([Parameter(Mandatory)][string]$InstallDir)

  $scorer = Join-Path $InstallDir "mozc_zenz_scorer.exe"
  $stdout = Join-Path $EvidenceDir "upgrade-scorer.stdout.log"
  $stderr = Join-Path $EvidenceDir "upgrade-scorer.stderr.log"

  $proc = Start-Process $scorer -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr

  try {
    $context = [string][char]0xEE02
    $inputStart = [string][char]0xEE00
    $outputStart = [string][char]0xEE01

    $dentistContext = Get-Utf8Text "5q2v44GM55eb44GE44Gu44Gn"
    $haishaKana = Get-Utf8Text "44OP44Kk44K344Oj"
    $dentistExpected = Get-Utf8Text "5q2v5Yy76ICF"
    $scrapContext = Get-Utf8Text "6LuK44GM5aOK44KM44Gf44Gu44Gn"
    $scrapExpected = Get-Utf8Text "5buD6LuK"

    $vectors = @(
      [pscustomobject]@{
        Name = "dentist"
        Prompt = $context+$dentistContext+$inputStart+$haishaKana+$outputStart
        Expected = $dentistExpected
      }
      [pscustomobject]@{
        Name = "scrap_car"
        Prompt = $context+$scrapContext+$inputStart+$haishaKana+$outputStart
        Expected = $scrapExpected
      }
    )

    $generation = [uint32]500
    $rows = @()

    foreach ($v in $vectors) {
      $ok = $false
      $last = $null
      for ($attempt = 1; $attempt -le 12; $attempt++) {
        if ($proc.HasExited) {
          throw "Scorer exited unexpectedly: $($proc.ExitCode)"
        }
        try {
          $generation++
          $last = Invoke-ZenzPipeRequest -Prompt $v.Prompt -Generation $generation
          if ($last.Status -eq 0 -and $last.Value -ceq $v.Expected) {
            $ok = $true
            break
          }
        }
        catch {
          $last = [pscustomobject]@{
            Status = 999
            Value = ""
            Debug = $_.Exception.Message
          }
        }
        Start-Sleep -Seconds 2
      }

      $rows += [pscustomobject]@{
        Test = $v.Name
        Status = if ($last) { $last.Status } else { -1 }
        Expected = $v.Expected
        Actual = if ($last) { [string]$last.Value } else { "" }
        Debug = if ($last) { [string]$last.Debug } else { "" }
        Pass = $ok
      }

      if (-not $ok) {
        throw "Post-upgrade scorer E2E failed for $($v.Name)"
      }
    }

    $rows | Export-Csv (Join-Path $EvidenceDir "post-upgrade-scorer-e2e.csv") `
      -NoTypeInformation -Encoding utf8
  }
  finally {
    if (-not $proc.HasExited) {
      Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    Stop-InstalledZenzProcesses -Root $InstallDir
  }
}

if ($env:PROCESSOR_ARCHITECTURE -ne "ARM64") {
  throw "Lifecycle audit requires native ARM64 runner."
}

$baselineMsi = Get-OneMsi $BaselineArtifactDir
$currentMsi = Get-OneMsi $CurrentArtifactDir

$baselineSha = (Get-FileHash $baselineMsi -Algorithm SHA256).Hash
$currentSha = (Get-FileHash $currentMsi -Algorithm SHA256).Hash

$baselineProduct = Get-MsiProperty $baselineMsi "ProductCode"
$currentProduct = Get-MsiProperty $currentMsi "ProductCode"
$baselineVersion = Get-MsiProperty $baselineMsi "ProductVersion"
$currentVersion = Get-MsiProperty $currentMsi "ProductVersion"
$baselineUpgrade = Get-MsiProperty $baselineMsi "UpgradeCode"
$currentUpgrade = Get-MsiProperty $currentMsi "UpgradeCode"

Write-Host "===== W6 MSI METADATA ====="
Write-Host "Baseline MSI SHA256  = $baselineSha"
Write-Host "Current MSI SHA256   = $currentSha"
Write-Host "Baseline ProductCode = $baselineProduct"
Write-Host "Current ProductCode  = $currentProduct"
Write-Host "Baseline Version     = $baselineVersion"
Write-Host "Current Version      = $currentVersion"
Write-Host "Baseline UpgradeCode = $baselineUpgrade"
Write-Host "Current UpgradeCode  = $currentUpgrade"

if ($baselineProduct -eq $currentProduct) {
  throw "Baseline/current ProductCode must differ."
}
if ($baselineUpgrade -ne $currentUpgrade -or $currentUpgrade -ne $ExpectedUpgradeCode) {
  throw "UpgradeCode contract mismatch."
}

$baselineVersionObj = [version]$baselineVersion
$currentVersionObj = [version]$currentVersion
if ($currentVersionObj -lt $baselineVersionObj) {
  throw "Current ProductVersion is lower than baseline."
}
$sameVersionDevArtifacts = $currentVersionObj -eq $baselineVersionObj
Write-Host "Same-version development artifacts = $sameVersionDevArtifacts"
if ($sameVersionDevArtifacts) {
  Write-Host "NOTE: final release contract still requires ProductVersion to increase relative to the previous release."
}

$metadataRows = @(
  [pscustomobject]@{
    Role="baseline"; Sha256=$baselineSha; ProductCode=$baselineProduct;
    ProductVersion=$baselineVersion; UpgradeCode=$baselineUpgrade
  }
  [pscustomobject]@{
    Role="current"; Sha256=$currentSha; ProductCode=$currentProduct;
    ProductVersion=$currentVersion; UpgradeCode=$currentUpgrade
  }
)
$metadataRows | Export-Csv (Join-Path $EvidenceDir "msi-metadata.csv") `
  -NoTypeInformation -Encoding utf8

if ((Get-MsiProductState $baselineProduct) -eq 5 -or
    (Get-MsiProductState $currentProduct) -eq 5) {
  throw "Mozkey product unexpectedly installed before lifecycle audit."
}
Assert-FirewallRuleCount -Expected 0

$baselineLog = Join-Path $EvidenceDir "01-baseline-install.log"
$upgradeLog = Join-Path $EvidenceDir "02-current-upgrade.log"
$reinstallLog = Join-Path $EvidenceDir "03-current-second-install.log"
$uninstallLog = Join-Path $EvidenceDir "04-current-uninstall.log"

$installDir = $null
$currentInstalled = $false

try {
  Write-Host ""
  Write-Host "===== INSTALL W2-PREDECESSOR ARM64 MSI ====="
  [void](Invoke-Msi -Mode install -Msi $baselineMsi -Log $baselineLog)

  if ((Get-MsiProductState $baselineProduct) -ne 5) {
    throw "Baseline ProductCode is not registered after install."
  }
  if ((Get-MsiProductState $currentProduct) -eq 5) {
    throw "Current ProductCode unexpectedly registered during baseline install."
  }

  $installDir = Resolve-MozcInstallDir
  Write-Host "Baseline install directory = $installDir"

  $baselineLlama = Join-Path $installDir "llama-server.exe"
  if ((Get-PEMachine $baselineLlama) -ne "x64") {
    throw "Expected legacy baseline llama-server to be x64."
  }
  if ((Get-FileHash $baselineLlama -Algorithm SHA256).Hash -ne $ExpectedBaselineLlamaSha) {
    throw "Legacy baseline llama-server hash mismatch."
  }

  foreach ($legacy in $LegacyDllHashes.GetEnumerator()) {
    $path = Join-Path $installDir $legacy.Key
    if (-not (Test-Path $path -PathType Leaf)) {
      throw "Legacy baseline DLL missing: $($legacy.Key)"
    }
    if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $legacy.Value) {
      throw "Legacy baseline DLL hash mismatch: $($legacy.Key)"
    }
  }

  Write-Host "Legacy baseline runtime identity = PASS"

  Write-Host ""
  Write-Host "===== IN-PLACE UPGRADE TO CURRENT ARM64 MSI ====="
  [void](Invoke-Msi -Mode install -Msi $currentMsi -Log $upgradeLog)
  $currentInstalled = $true

  if ((Get-MsiProductState $baselineProduct) -eq 5) {
    throw "Baseline ProductCode remains registered after upgrade."
  }
  if ((Get-MsiProductState $currentProduct) -ne 5) {
    throw "Current ProductCode is not registered after upgrade."
  }

  $installDir = Resolve-MozcInstallDir
  Assert-CurrentPayload $installDir
  Assert-FirewallRuleCount -Expected 7

  $service = Get-Service -Name "MozcCacheService" -ErrorAction SilentlyContinue
  if (-not $service) {
    throw "MozcCacheService missing after upgrade."
  }

  Invoke-ScorerContextGate $installDir

  Write-Host "Post-upgrade native payload       = PASS"
  Write-Host "Legacy runtime removal            = PASS"
  Write-Host "ProductCode replacement           = PASS"
  Write-Host "Firewall rule transition          = PASS 7/7"
  Write-Host "Post-upgrade scorer context E2E   = PASS 2/2"

  Write-Host ""
  Write-Host "===== SECOND INSTALL OF EXACT CURRENT MSI ====="
  Stop-InstalledZenzProcesses $installDir
  [void](Invoke-Msi -Mode install -Msi $currentMsi -Log $reinstallLog)

  if ((Get-MsiProductState $currentProduct) -ne 5) {
    throw "Current ProductCode lost after second install."
  }
  Assert-CurrentPayload $installDir
  Assert-FirewallRuleCount -Expected 7
  Write-Host "Same-package second install       = PASS"
  Write-Host "Firewall rule duplication         = NONE"

  Write-Host ""
  Write-Host "===== UNINSTALL CURRENT MSI ====="
  Stop-InstalledZenzProcesses $installDir
  $uninstallExit = Invoke-Msi -Mode uninstall -Msi $currentMsi -Log $uninstallLog
  $currentInstalled = $false

  if ((Get-MsiProductState $baselineProduct) -eq 5) {
    throw "Baseline ProductCode unexpectedly registered after current uninstall."
  }
  if ((Get-MsiProductState $currentProduct) -eq 5) {
    throw "Current ProductCode remains registered after uninstall."
  }

  Assert-FirewallRuleCount -Expected 0

  foreach ($path in @(
    (Join-Path $installDir "llama-server.exe"),
    (Join-Path $installDir "mozc_zenz_scorer.exe"),
    (Join-Path $installDir "models\zenz-v3.2-small-Q5_K_M.gguf")
  )) {
    if (Test-Path $path) {
      throw "Zenz payload remains after uninstall: $path"
    }
  }

  $serviceAfter = Get-Service -Name "MozcCacheService" -ErrorAction SilentlyContinue
  if ($serviceAfter -and $uninstallExit -ne 3010) {
    throw "MozcCacheService remains after non-reboot uninstall."
  }

  $summary = @(
    "baseline_commit=$ExpectedBaselineCommit"
    "baseline_msi_sha256=$baselineSha"
    "current_msi_sha256=$currentSha"
    "baseline_product_code=$baselineProduct"
    "current_product_code=$currentProduct"
    "baseline_product_version=$baselineVersion"
    "current_product_version=$currentVersion"
    "upgrade_code=$currentUpgrade"
    "same_version_development_artifacts=$sameVersionDevArtifacts"
    "baseline_legacy_llama_x64=true"
    "baseline_legacy_dlls_present=true"
    "upgrade_product_code_replaced=true"
    "upgrade_current_llama_arm64=true"
    "upgrade_legacy_dlls_removed=true"
    "upgrade_firewall_rules=7"
    "upgrade_scorer_context_e2e=2/2"
    "same_package_second_install=true"
    "uninstall_product_registration_removed=true"
    "uninstall_zenz_payload_removed=true"
    "uninstall_firewall_rules_removed=true"
    "uninstall_exit=$uninstallExit"
  )
  $summary | Set-Content (Join-Path $EvidenceDir "summary.txt") -Encoding utf8

  Write-Host ""
  Write-Host "===== W6 INSTALLER LIFECYCLE RESULT ====="
  Write-Host "Baseline ProductCode              = $baselineProduct"
  Write-Host "Current ProductCode               = $currentProduct"
  Write-Host "UpgradeCode preserved             = PASS"
  Write-Host "ProductCode changed               = PASS"
  Write-Host "W2-predecessor legacy llama       = x64 exact"
  Write-Host "W2-predecessor legacy DLLs        = 4/4 exact"
  Write-Host "In-place upgrade                  = PASS"
  Write-Host "Current llama after upgrade       = ARM64 exact"
  Write-Host "Legacy DLL removal after upgrade  = PASS"
  Write-Host "Current scorer context E2E        = PASS 2/2"
  Write-Host "Firewall rules after upgrade      = PASS 7/7"
  Write-Host "Exact-current second install      = PASS"
  Write-Host "Uninstall product cleanup         = PASS"
  Write-Host "Uninstall Zenz payload cleanup    = PASS"
  Write-Host "Uninstall firewall cleanup        = PASS"
  Write-Host "W6 lifecycle gate                 = PASS"
}
finally {
  if ($installDir) {
    Stop-InstalledZenzProcesses $installDir
  }
  if ($currentInstalled) {
    try {
      [void](Invoke-Msi -Mode uninstall -Msi $currentMsi `
        -Log (Join-Path $EvidenceDir "99-emergency-current-uninstall.log"))
    } catch {}
  }
  if ((Get-MsiProductState $baselineProduct) -eq 5) {
    try {
      [void](Invoke-Msi -Mode uninstall -Msi $baselineMsi `
        -Log (Join-Path $EvidenceDir "99-emergency-baseline-uninstall.log"))
    } catch {}
  }
}
