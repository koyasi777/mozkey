# Secure Offline Release Checklist

Use this checklist before publishing a Windows release.

## Source revision

- [ ] Release branch:
- [ ] Commit SHA:
- [ ] Upstream base commit:
- [ ] Build machine:
- [ ] Build date:

## Build command

Expected command:

```powershell
cd C:\Users\Makoto\dev\mozc\src

bazelisk --output_user_root=C:/bzl build `
  --config oss_windows `
  --config release_build `
  package
```

## Source checks

- [ ] No obvious runtime networking API usage

```powershell
cd C:\Users\Makoto\dev\mozc
python tools\check_no_network_symbols.py
```

## Tests

- [ ] StatsConfigUtil test passed
- [ ] ConfigHandler test passed

```powershell
cd C:\Users\Makoto\dev\mozc\src

bazelisk --output_user_root=C:/bzl test `
  --config oss_windows `
  --config release_build `
  --test_output=errors `
  //config:stats_config_util_test //config:config_handler_test
```

## Build artifact checks

- [ ] Bazel output Mozc core runtime binaries do not import prohibited networking DLLs

```powershell
cd C:\Users\Makoto\dev\mozc
python tools\check_no_network_imports.py --root src\bazel-bin
```

- [ ] Bazel output binaries do not contain hard-deny telemetry / updater / crash-upload / usage-statistics markers
- [ ] Report-only URL-like markers are reviewed

```powershell
cd C:\Users\Makoto\dev\mozc
python tools\check_no_network_strings.py --root src\bazel-bin
```

## MSI-extracted binary checks

Extract the MSI and check the files that will actually be installed.

```powershell
cd C:\Users\Makoto\dev\mozc

$extractDir = "msi_extract"
Remove-Item $extractDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $extractDir | Out-Null

msiexec.exe /a "src\bazel-bin\win32\installer\Mozc64.msi" /qn TARGETDIR="$PWD\$extractDir"
```

- [ ] MSI-extracted Mozc core runtime binaries do not import prohibited networking DLLs

```powershell
python tools\check_no_network_imports.py --root msi_extract
```

- [ ] MSI-extracted Mozc core runtime binaries do not contain hard-deny telemetry / updater / crash-upload / usage-statistics markers
- [ ] Report-only URL-like markers are reviewed

```powershell
python tools\check_no_network_strings.py --root msi_extract
```

## Installed binary checks

Use the actual install directory. On many Windows systems, Mozc is installed
under `C:\Program Files (x86)\Mozc`.

- [ ] Installed Mozc core runtime binaries do not import prohibited networking DLLs

```powershell
cd C:\Users\Makoto\dev\mozc

python tools\check_no_network_imports.py `
  "C:\Program Files (x86)\Mozc\mozc_server.exe" `
  "C:\Program Files (x86)\Mozc\mozc_tool.exe" `
  "C:\Program Files (x86)\Mozc\mozc_renderer.exe" `
  "C:\Program Files (x86)\Mozc\mozc_broker.exe" `
  "C:\Program Files (x86)\Mozc\mozc_cache_service.exe" `
  "C:\Program Files (x86)\Mozc\mozc_tip64.dll"
```

- [ ] Installed Mozc core runtime binaries do not contain hard-deny telemetry / updater / crash-upload / usage-statistics markers
- [ ] Report-only URL-like markers are reviewed

```powershell
cd C:\Users\Makoto\dev\mozc

python tools\check_no_network_strings.py `
  "C:\Program Files (x86)\Mozc\mozc_server.exe" `
  "C:\Program Files (x86)\Mozc\mozc_tool.exe" `
  "C:\Program Files (x86)\Mozc\mozc_renderer.exe" `
  "C:\Program Files (x86)\Mozc\mozc_broker.exe" `
  "C:\Program Files (x86)\Mozc\mozc_cache_service.exe" `
  "C:\Program Files (x86)\Mozc\mozc_tip64.dll"
```

## Zenz localhost transport checks

For Zenz-bundled Windows builds:

- [ ] `mozc_zenz_scorer.exe` release build does not contain release-disabled runtime/model/port override markers

```powershell
$Scorer = "C:\Users\Makoto\dev\mozc\src\bazel-bin\zenz_scorer\mozc_zenz_scorer.exe"

function Test-BinaryString2 {
  param(
    [string]$Path,
    [string]$Needle
  )

  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $utf8 = [System.Text.Encoding]::UTF8.GetString($bytes)
  $utf16 = [System.Text.Encoding]::Unicode.GetString($bytes)

  [pscustomobject]@{
    Needle = $Needle
    FoundUtf8 = $utf8.Contains($Needle)
    FoundUtf16 = $utf16.Contains($Needle)
    FoundAny = ($utf8.Contains($Needle) -or $utf16.Contains($Needle))
  }
}

Test-BinaryString2 $Scorer "MOZC_ZENZ_LLAMA_SERVER"
Test-BinaryString2 $Scorer "MOZC_ZENZ_MODEL"
Test-BinaryString2 $Scorer "MOZC_ZENZ_PORT"
Test-BinaryString2 $Scorer "MOZC_ZENZ_CTX"
Test-BinaryString2 $Scorer "MOZC_ZENZ_THREADS"
Test-BinaryString2 $Scorer "MOZC_ZENZ_N_PREDICT"
```

Expected result:

```text
MOZC_ZENZ_LLAMA_SERVER  FoundAny = False
MOZC_ZENZ_MODEL         FoundAny = False
MOZC_ZENZ_PORT          FoundAny = False
MOZC_ZENZ_CTX           FoundAny = True
MOZC_ZENZ_THREADS       FoundAny = True
MOZC_ZENZ_N_PREDICT     FoundAny = True
```

- [ ] DebugView confirms local transport hardening without exposing generated secrets

Expected DebugView markers:

```text
[mozc-zenz-scorer] http_port_mode=random
[mozc-zenz-scorer] api_key_bytes=64
[mozc-zenz-scorer] launch llama-server port=random api_key_bytes=64
[mozc-zenz-scorer] ready probe succeeded
```

The following values must not appear in DebugView logs:

```text
port=18080
api_key=<actual value>
Authorization: Bearer <actual value>
```

- [ ] `llama-server.exe` listens only on `127.0.0.1`
- [ ] `llama-server.exe` does not listen on fixed port `18080`

```powershell
Get-Process llama-server -ErrorAction SilentlyContinue |
  ForEach-Object {
    Get-NetTCPConnection -OwningProcess $_.Id -State Listen |
      Select-Object LocalAddress, LocalPort, OwningProcess
  }
```

Expected result:

```text
LocalAddress: 127.0.0.1
LocalPort: not 18080
```

- [ ] Zenz correction request succeeds through the scorer

Expected DebugView markers:

```text
[zenz-pipe] Convert response status=0
[zenz] async response ok=true timeout=false
```

## Windows Firewall checks

- [ ] Outbound block rules exist for Mozc runtime executables

```powershell
Get-NetFirewallRule -DisplayName "Mozc Offline - Block *"
```

- [ ] Firewall rules target Mozc executable paths

```powershell
Get-NetFirewallRule -DisplayName "Mozc Offline - Block *" |
  Get-NetFirewallApplicationFilter |
  Select-Object Program
```

Expected programs:

```text
C:\Program Files (x86)\Mozc\mozc_server.exe
C:\Program Files (x86)\Mozc\mozc_tool.exe
C:\Program Files (x86)\Mozc\mozc_renderer.exe
C:\Program Files (x86)\Mozc\mozc_broker.exe
C:\Program Files (x86)\Mozc\mozc_cache_service.exe
```

- [ ] Firewall rules are outbound block rules

```powershell
Get-NetFirewallRule -DisplayName "Mozc Offline - Block *" |
  Select-Object DisplayName, Direction, Action, Enabled, Profile
```

Expected values:

```text
Direction: Outbound
Action: Block
Enabled: True
Profile: Any
```

- [ ] IME still works with the firewall rules enabled
- [ ] Uninstall removes Mozc firewall rules

```powershell
Get-NetFirewallRule -DisplayName "Mozc Offline - Block *" -ErrorAction SilentlyContinue
```

Expected after uninstall: no rules are returned.

## Runtime checks

Use a clean Windows VM.

- [ ] Install MSI
- [ ] Enable Mozc IME
- [ ] Type Japanese text
- [ ] Open config dialog
- [ ] Open dictionary tool
- [ ] Open administration dialog
- [ ] Confirm IME works with network disabled
- [ ] Confirm no outbound connection from Mozc core runtime processes
- [ ] For Zenz-bundled builds, confirm local Zenz correction works
- [ ] For Zenz-bundled builds, confirm no external listener is exposed by `llama-server.exe`

Recommended tools:

- Windows Defender Firewall log
- Resource Monitor
- Process Monitor
- pktmon
- Wireshark

## Release artifacts

- [ ] MSI file:
- [ ] SHA256:
- [ ] Source commit SHA included in release notes
- [ ] Build command included in release notes
- [ ] Known limitations documented

## Documentation

- [ ] README links to `docs/security/offline_guarantee.md`
- [ ] README links to `docs/security/release_checklist.md`
- [ ] README states that Windows installer adds outbound firewall block rules for Mozc runtime executables
- [ ] README describes Zenz localhost transport at a high level without exposing low-level implementation details
- [ ] Security docs document the `mozc_zenz_scorer.exe` local-only WinHTTP exception
- [ ] Release notes state that offline behavior means runtime behavior, not build-time dependency fetching
- [ ] Release notes state that local data protection requires OS-level disk encryption for stronger protection
