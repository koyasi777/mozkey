param(
  [switch]$SkipDownload
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\personal_names"
$Bz2 = Join-Path $OutDir "mozcdic-ut-personal-names.txt.bz2"
$Txt = Join-Path $OutDir "mozcdic-ut-personal-names.txt"
$Url = "https://raw.githubusercontent.com/utuhiro78/mozcdic-ut-personal-names/main/mozcdic-ut-personal-names.txt.bz2"

New-Item -ItemType Directory -Force $OutDir | Out-Null

if (-not $SkipDownload) {
  Write-Host "Downloading mozcdic-ut personal names dictionary..."
  Write-Host "  $Url"
  Invoke-WebRequest -Uri $Url -OutFile $Bz2
} else {
  Write-Host "Skipping download."
}

if (-not (Test-Path $Bz2)) {
  throw "bz2 file does not exist: $Bz2"
}

$env:MOZKEY_PERSONAL_NAMES_BZ2 = (Resolve-Path $Bz2).Path
$env:MOZKEY_PERSONAL_NAMES_TXT = $Txt

$PythonCode = "import bz2, os, pathlib; src = pathlib.Path(os.environ['MOZKEY_PERSONAL_NAMES_BZ2']); dst = pathlib.Path(os.environ['MOZKEY_PERSONAL_NAMES_TXT']); dst.write_bytes(bz2.decompress(src.read_bytes())); print(f'wrote: {dst}'); print(f'bytes: {dst.stat().st_size}')"

python -c $PythonCode

if ($LASTEXITCODE -ne 0) {
  throw "failed to decompress mozcdic-ut personal names dictionary."
}

Write-Host ""
Write-Host "Personal names dictionary is ready:"
Write-Host "  $Txt"
