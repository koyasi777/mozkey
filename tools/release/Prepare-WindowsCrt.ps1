# Prepare the exact CRT staging directory expected by Mozkey's existing WiX build.
# Run on an ephemeral GitHub-hosted Windows 2025 runner only.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$destinationDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.50.35710\x64\Microsoft.VC145.CRT'
$sourceRoots = @(
  'C:\Program Files\Microsoft Visual Studio\2022',
  'C:\Program Files\Microsoft Visual Studio\17',
  'C:\Program Files\Microsoft Visual Studio\18',
  'C:\Program Files (x86)\Microsoft Visual Studio\2022',
  'C:\Program Files (x86)\Microsoft Visual Studio\17',
  'C:\Program Files (x86)\Microsoft Visual Studio\18'
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
if (@($sourceRoots).Count -eq 0) { throw 'No supported Visual Studio CRT source root.' }

$required = @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','vcruntime140_1.dll')
$selected = @{}
foreach ($name in $required) {
  $hits = @(Get-ChildItem -Path $sourceRoots -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC[0-9]+\.CRT\\' } |
    Sort-Object FullName -Descending)
  if ($hits.Count -lt 1) { throw "Missing Windows x64 CRT dependency: $name" }
  $selected[$name] = $hits[0].FullName
  Write-Host "CRT_SOURCE=$name $($selected[$name])"
}

New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
foreach ($name in $required) {
  $from = $selected[$name]
  $to = Join-Path $destinationDir $name
  if (([IO.Path]::GetFullPath($from)).Equals(([IO.Path]::GetFullPath($to)), [StringComparison]::OrdinalIgnoreCase)) {
    Write-Host "CRT_ALREADY_STAGED=$name"
  } else {
    Copy-Item -LiteralPath $from -Destination $to -Force
  }
  $sourceSha = (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
  $stagedSha = (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash
  if ($sourceSha -cne $stagedSha) { throw "CRT copy hash mismatch: $name" }
  Write-Host "CRT_SHA256=$name $stagedSha"
}
Write-Host 'MOZKEY_RELEASE_CRT_STAGING=PASS'
