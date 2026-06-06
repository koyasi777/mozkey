# Secure Offline Guarantee

## Goal

This fork is designed to operate as an offline Japanese IME.

Mozc runtime processes should not initiate Internet communication during normal
IME operation.

## Runtime network policy

Mozc runtime binaries must not initiate outbound Internet communication.

This includes:

- cloud conversion
- usage statistics upload
- crash report upload
- auto-update
- online dictionary update
- online suggestion service
- remote configuration fetch
- telemetry
- opening external URLs from Mozc runtime processes

## Local-only design

This fork uses local resources for normal IME operation:

- local system dictionary
- local user dictionary
- local user history
- local settings
- bundled local Zenz scorer, runtime, and model when Zenz support is included

Input text is processed locally.

For Zenz-bundled builds, local Zenz correction is performed through
`mozc_zenz_scorer.exe`, the bundled `llama-server.exe`, and the bundled local
GGUF model. The scorer-to-runtime transport is localhost-only and must be bound
to `127.0.0.1`.

The Zenz localhost transport must not rely on a fixed public-facing endpoint.
The scorer chooses a random high port for `llama-server.exe` and protects
completion requests with a random API key. Generated port and API key values
must not be written to DebugView logs.

The API key is a defense-in-depth measure for stale, accidental, or mismatched
localhost endpoints. It is passed to `llama-server.exe` through the command line
because that is the interface exposed by llama.cpp server. Therefore it should
not be treated as a strong secret against same-user local malware. The primary
boundary is that the server is bound to `127.0.0.1`, uses a random high port, and
is launched and managed by `mozc_zenz_scorer.exe`.

## Usage statistics and crash reports

The usage-statistics and crash-report option inherited from upstream is removed
from the administration and configuration dialogs.

The default `StatsConfigUtil` implementation is fixed to the null implementation
in this fork. Usage statistics cannot be enabled through the normal runtime
path.

## Source checks

Obvious runtime networking APIs are rejected by:

```powershell
python tools\check_no_network_symbols.py
```

## Binary import checks

Windows release binaries should be checked so that Mozc core runtime executables
do not import common networking DLLs such as:

- ws2_32.dll
- winhttp.dll
- wininet.dll
- urlmon.dll
- webio.dll
- dnsapi.dll

`mozc_zenz_scorer.exe` is a narrow local-only exception. It may import
`winhttp.dll` only to call the bundled `llama-server.exe` through a
`127.0.0.1` localhost endpoint. It must not use WinHTTP for external network
access, must not use a release-build environment override for the runtime or
model path, and must not expose the local inference endpoint externally.

The check is implemented by:

```powershell
python tools\check_no_network_imports.py --root src\bazel-bin
```

For release validation, the installed or MSI-extracted runtime binaries should
also be checked.

## Binary string checks

Windows release binaries should be checked so that Mozc runtime executables do
not contain hard-deny telemetry, updater, crash-upload, or usage-statistics
markers.

Generic URL-like markers such as `http://`, `https://`, `googleapis.com`, and
protobuf field names such as `usage_stats` are reported for audit. They are not
treated as hard failures by themselves.

The check is implemented by:

```powershell
python tools\check_no_network_strings.py --root src\bazel-bin
```

For release validation, run the same check against installed or MSI-extracted
runtime binaries.

## Windows Firewall hardening

The Windows installer adds outbound Windows Firewall block rules for Mozc
runtime executables as an additional offline hardening layer.

The rules target Mozc executable files such as:

- mozc_server.exe
- mozc_tool.exe
- mozc_renderer.exe
- mozc_broker.exe
- mozc_cache_service.exe

The rules are outbound-only. They are removed during uninstall. Firewall rule
creation and removal are best-effort operations; installation and uninstall do
not fail solely because local policy rejects firewall changes.

The TIP DLL is not managed by a firewall rule because Windows Firewall program
rules are executable-oriented. Instead, TIP DLL network capability is checked by
source, import, and string audits.

## Runtime checks

Before publishing a release, install the MSI in a clean Windows VM and confirm
that no Mozc core runtime process initiates outbound communication during normal
IME operation.

For Zenz-bundled builds, also confirm that `llama-server.exe` listens only on
`127.0.0.1`, uses a non-fixed random high port, and does not expose an external
interface.

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

Recommended tools:

- Windows Defender Firewall log
- Resource Monitor
- Process Monitor
- pktmon
- Wireshark

## Build-time network access

Building from source may require network access to download build dependencies.

This is separate from runtime behavior of the installed IME.

## Local data protection

Offline operation means that the IME does not send input data to external
servers.

Local data protection is a separate concern. User dictionary, user history, and
settings are stored on the local machine. Users who need stronger local data
protection should enable OS-level disk encryption such as BitLocker.
