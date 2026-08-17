param(
  [Parameter(Mandatory = $true)]
  [string]$NativeArtifactDir,

  [Parameter(Mandatory = $true)]
  [string]$CrossArtifactDir,

  [Parameter(Mandatory = $true)]
  [string]$EvidenceDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$EvidenceDir = [System.IO.Path]::GetFullPath($EvidenceDir)

function Resolve-SingleMsi {
  param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [Parameter(Mandatory = $true)][string]$Label
  )

  $items = @(
    Get-ChildItem -Path $Directory -Recurse -File -Filter *.msi
  )
  if ($items.Count -ne 1) {
    throw "${Label}: expected exactly one MSI; found $($items.Count)"
  }
  return $items[0].FullName
}

function Release-ComObject {
  param([object]$Object)
  if ($null -ne $Object -and [System.Runtime.InteropServices.Marshal]::IsComObject($Object)) {
    [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($Object)
  }
}

function Invoke-MsiQuery {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Sql,
    [Parameter(Mandatory = $true)][int]$FieldCount
  )

  $installer = $null
  $database = $null
  $view = $null
  $rows = @()

  try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.GetType().InvokeMember(
      "OpenDatabase",
      [System.Reflection.BindingFlags]::InvokeMethod,
      $null,
      $installer,
      @($Path, 0)
    )
    $view = $database.GetType().InvokeMember(
      "OpenView",
      [System.Reflection.BindingFlags]::InvokeMethod,
      $null,
      $database,
      @($Sql)
    )
    [void]$view.GetType().InvokeMember(
      "Execute",
      [System.Reflection.BindingFlags]::InvokeMethod,
      $null,
      $view,
      $null
    )

    while ($true) {
      $record = $view.GetType().InvokeMember(
        "Fetch",
        [System.Reflection.BindingFlags]::InvokeMethod,
        $null,
        $view,
        $null
      )
      if ($null -eq $record) {
        break
      }

      try {
        $fields = @()
        for ($i = 1; $i -le $FieldCount; ++$i) {
          $value = $record.GetType().InvokeMember(
            "StringData",
            [System.Reflection.BindingFlags]::GetProperty,
            $null,
            $record,
            @($i)
          )
          $fields += [string]$value
        }
        $rows += [pscustomobject]@{
          Fields = $fields
        }
      }
      finally {
        Release-ComObject $record
      }
    }
  }
  finally {
    Release-ComObject $view
    Release-ComObject $database
    Release-ComObject $installer
  }

  return $rows
}

function Get-MsiProperty {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Name
  )

  $sql = "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$Name'"
  $rows = @(Invoke-MsiQuery -Path $Path -Sql $sql -FieldCount 1)
  if ($rows.Count -eq 0) {
    return ""
  }
  if ($rows.Count -ne 1) {
    throw "MSI property '$Name' has unexpected row count: $($rows.Count)"
  }
  return [string]$rows[0].Fields[0]
}

function Get-MsiFileTable {
  param([Parameter(Mandatory = $true)][string]$Path)

  $rows = @(
    Invoke-MsiQuery `
      -Path $Path `
      -Sql 'SELECT `File`, `Component_`, `FileName` FROM `File`' `
      -FieldCount 3
  )
  return @(
    $rows |
      ForEach-Object {
        "$($_.Fields[0])|$($_.Fields[1])|$($_.Fields[2])"
      } |
      Sort-Object
  )
}

function Invoke-AdministrativeExtract {
  param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [Parameter(Mandatory = $true)][string]$Log
  )

  if (Test-Path $TargetDir) {
    Remove-Item -Recurse -Force $TargetDir
  }
  New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null

  $process = Start-Process msiexec.exe -Wait -PassThru -ArgumentList @(
    "/a",
    "`"$Msi`"",
    "/qn",
    "TARGETDIR=`"$TargetDir`"",
    "/l*v",
    "`"$Log`""
  )

  Write-Host "Administrative extract exit = $($process.ExitCode)"
  if ($process.ExitCode -notin @(0, 3010)) {
    throw "Administrative extraction failed: $Msi (exit $($process.ExitCode))"
  }
}

function Get-RelativeFileSet {
  param([Parameter(Mandatory = $true)][string]$Root)

  $fullRoot = [System.IO.Path]::GetFullPath($Root)
  return @(
    Get-ChildItem -Path $fullRoot -Recurse -File |
      ForEach-Object {
        [System.IO.Path]::GetRelativePath($fullRoot, $_.FullName).Replace("\", "/")
      } |
      Sort-Object
  )
}

function Get-PEMachine {
  param([Parameter(Mandatory = $true)][string]$Path)

  $stream = $null
  $reader = $null
  try {
    $stream = [System.IO.File]::Open(
      $Path,
      [System.IO.FileMode]::Open,
      [System.IO.FileAccess]::Read,
      [System.IO.FileShare]::ReadWrite
    )
    $reader = [System.IO.BinaryReader]::new($stream)

    if ($stream.Length -lt 64) { return $null }
    if ($reader.ReadUInt16() -ne 0x5A4D) { return $null }

    $stream.Seek(0x3C, [System.IO.SeekOrigin]::Begin) | Out-Null
    $offset = $reader.ReadUInt32()
    if ($offset + 6 -gt $stream.Length) { return "INVALID_PE" }

    $stream.Seek($offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    if ($reader.ReadUInt32() -ne 0x00004550) { return "INVALID_PE" }

    $machine = $reader.ReadUInt16()
    switch ($machine) {
      0xAA64 { return "ARM64" }
      0x8664 { return "x64" }
      0x014c { return "x86" }
      default { return ("0x{0:X4}" -f $machine) }
    }
  }
  finally {
    if ($reader) { $reader.Dispose() }
    elseif ($stream) { $stream.Dispose() }
  }
}

function Get-PEManifest {
  param([Parameter(Mandatory = $true)][string]$Root)

  $fullRoot = [System.IO.Path]::GetFullPath($Root)
  $rows = @()

  foreach ($file in Get-ChildItem -Path $fullRoot -Recurse -File) {
    if ($file.Extension -notin @(".exe", ".dll")) {
      continue
    }

    $machine = Get-PEMachine $file.FullName
    if ($null -eq $machine) {
      continue
    }

    $relative = [System.IO.Path]::GetRelativePath(
      $fullRoot,
      $file.FullName
    ).Replace("\", "/")
    $rows += "$relative|$machine"
  }

  return @($rows | Sort-Object)
}

function Get-SinglePayload {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$LeafName
  )

  $items = @(
    Get-ChildItem -Path $Root -Recurse -File |
      Where-Object { $_.Name -eq $LeafName }
  )
  if ($items.Count -ne 1) {
    throw "Expected exactly one '$LeafName' in extracted payload; found $($items.Count)"
  }
  return $items[0].FullName
}

function Assert-TextSetEqual {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][object[]]$Native,
    [Parameter(Mandatory = $true)][object[]]$Cross,
    [Parameter(Mandatory = $true)][string]$DiffPath
  )

  $diff = @(Compare-Object -ReferenceObject $Native -DifferenceObject $Cross)
  if ($diff.Count -ne 0) {
    $diff | Format-Table -AutoSize | Out-String | Set-Content -Encoding utf8 $DiffPath
    throw "$Name differs between native and x64-host cross packages."
  }
  "MATCH" | Set-Content -Encoding utf8 $DiffPath
  Write-Host "$Name = MATCH"
}

$nativeMsi = Resolve-SingleMsi -Directory $NativeArtifactDir -Label "native"
$crossMsi = Resolve-SingleMsi -Directory $CrossArtifactDir -Label "cross"

$nativeSha = (Get-FileHash $nativeMsi -Algorithm SHA256).Hash
$crossSha = (Get-FileHash $crossMsi -Algorithm SHA256).Hash
$nativeSize = (Get-Item $nativeMsi).Length
$crossSize = (Get-Item $crossMsi).Length

Write-Host "===== MSI CONTAINER INFO ====="
Write-Host "Native MSI = $nativeMsi"
Write-Host "Native SHA = $nativeSha"
Write-Host "Native size = $nativeSize"
Write-Host "Cross MSI = $crossMsi"
Write-Host "Cross SHA = $crossSha"
Write-Host "Cross size = $crossSize"
Write-Host "Whole-MSI SHA equality is intentionally NOT required."

$propertyNames = @(
  "ProductName",
  "ProductVersion",
  "ProductCode",
  "UpgradeCode",
  "Manufacturer",
  "ProductLanguage"
)

$nativeProps = [ordered]@{}
$crossProps = [ordered]@{}
foreach ($name in $propertyNames) {
  $nativeProps[$name] = Get-MsiProperty -Path $nativeMsi -Name $name
  $crossProps[$name] = Get-MsiProperty -Path $crossMsi -Name $name
}

$nativeProps | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "native-msi-properties.json")
$crossProps | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "cross-msi-properties.json")

Write-Host ""
Write-Host "===== MSI PROPERTY PARITY ====="
foreach ($name in @("ProductName", "ProductVersion", "UpgradeCode", "Manufacturer", "ProductLanguage")) {
  Write-Host "$name native = $($nativeProps[$name])"
  Write-Host "$name cross  = $($crossProps[$name])"
  if ($nativeProps[$name] -ne $crossProps[$name]) {
    throw "MSI property mismatch: $name"
  }
}
Write-Host "Required MSI property parity = PASS"

Write-Host "ProductCode native = $($nativeProps.ProductCode)"
Write-Host "ProductCode cross  = $($crossProps.ProductCode)"
if ($nativeProps.ProductCode -ne $crossProps.ProductCode) {
  Write-Warning "ProductCode differs. This is recorded but not gated until determinism is separately audited."
}

Write-Host ""
Write-Host "===== MSI FILE TABLE PARITY ====="
$nativeFileTable = @(Get-MsiFileTable $nativeMsi)
$crossFileTable = @(Get-MsiFileTable $crossMsi)
$nativeFileTable | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "native-file-table.txt")
$crossFileTable | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "cross-file-table.txt")
Assert-TextSetEqual `
  -Name "MSI File table logical rows" `
  -Native $nativeFileTable `
  -Cross $crossFileTable `
  -DiffPath (Join-Path $EvidenceDir "file-table-diff.txt")

Write-Host ""
Write-Host "===== ADMINISTRATIVE EXTRACTION ====="
$nativeExtract = Join-Path $EvidenceDir "native-extract"
$crossExtract = Join-Path $EvidenceDir "cross-extract"
Invoke-AdministrativeExtract `
  -Msi $nativeMsi `
  -TargetDir $nativeExtract `
  -Log (Join-Path $EvidenceDir "native-admin-extract.log")
Invoke-AdministrativeExtract `
  -Msi $crossMsi `
  -TargetDir $crossExtract `
  -Log (Join-Path $EvidenceDir "cross-admin-extract.log")

$nativePayload = @(Get-RelativeFileSet $nativeExtract)
$crossPayload = @(Get-RelativeFileSet $crossExtract)
$nativePayload | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "native-payload.txt")
$crossPayload | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "cross-payload.txt")
Assert-TextSetEqual `
  -Name "Administrative extracted payload set" `
  -Native $nativePayload `
  -Cross $crossPayload `
  -DiffPath (Join-Path $EvidenceDir "payload-diff.txt")

Write-Host ""
Write-Host "===== PE MACHINE PARITY ====="
$nativePe = @(Get-PEManifest $nativeExtract)
$crossPe = @(Get-PEManifest $crossExtract)
$nativePe | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "native-pe-machines.txt")
$crossPe | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "cross-pe-machines.txt")
Assert-TextSetEqual `
  -Name "PE relative-path / machine manifest" `
  -Native $nativePe `
  -Cross $crossPe `
  -DiffPath (Join-Path $EvidenceDir "pe-machine-diff.txt")

Write-Host ""
Write-Host "===== PINNED PAYLOAD HASH PARITY ====="
$nativeLlama = Get-SinglePayload -Root $nativeExtract -LeafName "llama-server.exe"
$crossLlama = Get-SinglePayload -Root $crossExtract -LeafName "llama-server.exe"
$nativeModel = Get-SinglePayload -Root $nativeExtract -LeafName "zenz-v3.2-small-Q5_K_M.gguf"
$crossModel = Get-SinglePayload -Root $crossExtract -LeafName "zenz-v3.2-small-Q5_K_M.gguf"
$nativeScorer = Get-SinglePayload -Root $nativeExtract -LeafName "mozc_zenz_scorer.exe"
$crossScorer = Get-SinglePayload -Root $crossExtract -LeafName "mozc_zenz_scorer.exe"

$hashRows = @(
  [pscustomobject]@{
    Payload = "llama-server.exe"
    Native = (Get-FileHash $nativeLlama -Algorithm SHA256).Hash
    Cross = (Get-FileHash $crossLlama -Algorithm SHA256).Hash
    Gate = $true
  },
  [pscustomobject]@{
    Payload = "zenz-v3.2-small-Q5_K_M.gguf"
    Native = (Get-FileHash $nativeModel -Algorithm SHA256).Hash
    Cross = (Get-FileHash $crossModel -Algorithm SHA256).Hash
    Gate = $true
  },
  [pscustomobject]@{
    Payload = "mozc_zenz_scorer.exe"
    Native = (Get-FileHash $nativeScorer -Algorithm SHA256).Hash
    Cross = (Get-FileHash $crossScorer -Algorithm SHA256).Hash
    Gate = $false
  }
)

$hashRows | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $EvidenceDir "payload-hashes.csv")
foreach ($row in $hashRows) {
  Write-Host "$($row.Payload) native = $($row.Native)"
  Write-Host "$($row.Payload) cross  = $($row.Cross)"
  if ($row.Gate -and $row.Native -ne $row.Cross) {
    throw "Pinned payload hash mismatch: $($row.Payload)"
  }
}
Write-Host "Pinned llama/model hash parity = PASS"
Write-Host "Scorer hash equality = INFORMATIONAL (PE machine parity is gated)"

$summary = @(
  "ARM64 cross/native package parity = PASS",
  "Native host = windows-11-arm",
  "Cross host = windows-2025",
  "Native MSI SHA256 = $nativeSha",
  "Cross MSI SHA256 = $crossSha",
  "Native MSI size = $nativeSize",
  "Cross MSI size = $crossSize",
  "Whole-MSI SHA equality required = NO",
  "ProductName parity = PASS",
  "ProductVersion parity = PASS",
  "UpgradeCode parity = PASS",
  "Manufacturer parity = PASS",
  "ProductLanguage parity = PASS",
  "ProductCode equality = $($nativeProps.ProductCode -eq $crossProps.ProductCode)",
  "MSI File table parity = PASS",
  "Administrative payload set parity = PASS",
  "PE machine manifest parity = PASS",
  "llama-server hash parity = PASS",
  "model hash parity = PASS"
)
$summary | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "summary.txt")
$summary | ForEach-Object { Write-Host $_ }
