param(
    [ValidateSet("sample", "daily", "rich", "max")]
    [string]$Profile = "sample"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$DictionaryOssBuild = Join-Path $RepoRoot "src\data\dictionary_oss\BUILD.bazel"
$KoyasiBuild = Join-Path $RepoRoot "src\data\dictionary_koyasi\BUILD.bazel"

$ProfileTargets = @{
    sample = "//data/dictionary_koyasi:mozcdic_ut_sample"
    daily  = "//data/dictionary_koyasi:mozcdic_ut_daily_local"
    rich   = "//data/dictionary_koyasi:mozcdic_ut_rich_local"
    max    = "//data/dictionary_koyasi:mozcdic_ut_max_local"
}

$RequiredFiles = @{
    sample = Join-Path $RepoRoot "src\data\dictionary_koyasi\sample\mozcdic-ut-sample.txt"
    daily  = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\mozcdic-ut-daily.txt"
    rich   = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\mozcdic-ut-rich.txt"
    max    = Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\mozcdic-ut-max.txt"
}

$Target = $ProfileTargets[$Profile]
$RequiredFile = $RequiredFiles[$Profile]

Write-Host "Repo root: $RepoRoot"
Write-Host "Profile:   $Profile"
Write-Host "Target:    $Target"
Write-Host "Required:  $RequiredFile"

if (-not (Test-Path $DictionaryOssBuild)) {
    throw "BUILD.bazel was not found: $DictionaryOssBuild"
}

if (-not (Test-Path $KoyasiBuild)) {
    throw "BUILD.bazel was not found: $KoyasiBuild"
}

$AdditionalRequiredFiles = @()

if ($Profile -eq "daily") {
    $AdditionalRequiredFiles += Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\dic-nico-pixiv-delta.txt"
    $AdditionalRequiredFiles += Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\koyasi-syntax-guard.txt"
    $AdditionalRequiredFiles += Join-Path $RepoRoot "src\data\dictionary_koyasi\generated\profiled\mozcdic-ut-personal-names-daily.txt"
}

if ($AdditionalRequiredFiles.Count -gt 0) {
    Write-Host "Additional required files:"
    foreach ($AdditionalRequiredFile in $AdditionalRequiredFiles) {
        Write-Host "  $AdditionalRequiredFile"
    }
}

foreach ($AdditionalRequiredFile in $AdditionalRequiredFiles) {
    if (-not (Test-Path $AdditionalRequiredFile)) {
        Write-Host ""
        Write-Host "Additional required dictionary file does not exist:"
        Write-Host "  $AdditionalRequiredFile"
        Write-Host ""
        Write-Host "Generate all daily auxiliary dictionary files with:"
        Write-Host "  .\tools\dictionary\prepare_daily_dictionary.ps1"
        Write-Host ""
        Write-Host "If downloads already exist, run:"
        Write-Host "  .\tools\dictionary\prepare_daily_dictionary.ps1 -SkipDownload"
        throw "Cannot switch to profile '$Profile' because an additional dictionary file is missing."
    }
}

if (-not (Test-Path $RequiredFile)) {
    Write-Host ""
    Write-Host "Required dictionary file does not exist:"
    Write-Host "  $RequiredFile"
    Write-Host ""

    if ($Profile -eq "daily") {
        Write-Host "Generate it with:"
        Write-Host "  .\tools\dictionary\import_merge_ut.ps1 -Profile sample -SampleLines 5000"
        Write-Host "  python tools/dictionary/profile_merge_ut.py --profile daily"
    } elseif ($Profile -eq "rich") {
        Write-Host "Generate it with:"
        Write-Host "  .\tools\dictionary\import_merge_ut.ps1 -Profile rich -SampleLines 5000"
        Write-Host "  python tools/dictionary/profile_merge_ut.py --profile rich"
    } elseif ($Profile -eq "max") {
        Write-Host "Generate it with:"
        Write-Host "  .\tools\dictionary\import_merge_ut.ps1 -Profile max -SampleLines 5000"
        Write-Host "  python tools/dictionary/profile_merge_ut.py --profile max"
    }

    throw "Cannot switch to profile '$Profile' because the required dictionary file is missing."
}

$Content = Get-Content -Raw -Encoding UTF8 $DictionaryOssBuild

$KnownTargets = @(
    "//data/dictionary_koyasi:mozcdic_ut_sample",
    "//data/dictionary_koyasi:mozcdic_ut_daily_local",
    "//data/dictionary_koyasi:mozcdic_ut_rich_local",
    "//data/dictionary_koyasi:mozcdic_ut_max_local"
)

$Matched = $false

foreach ($KnownTarget in $KnownTargets) {
    $QuotedKnownTarget = '"' + $KnownTarget + '"'
    if ($Content.Contains($QuotedKnownTarget)) {
        $Content = $Content.Replace($QuotedKnownTarget, '"' + $Target + '"')
        $Matched = $true
    }
}

if (-not $Matched) {
    throw "No known merge-ut dictionary target was found in $DictionaryOssBuild"
}

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($DictionaryOssBuild, $Content, $Utf8NoBom)

Write-Host ""
Write-Host "Updated:"
Write-Host "  $DictionaryOssBuild"
Write-Host ""
Write-Host "Current merge-ut profile target:"
Select-String -Path $DictionaryOssBuild -Pattern "mozcdic_ut_"
