# Windows Zenz Runtime Contract

## Source identity

- llama.cpp repository: https://github.com/ggml-org/llama.cpp
- tag: `b10437`
- commit: `16d222fc5ead59d20039501a37251c9ed457a454`
- tokenizer compatibility patch SHA256: `6DB5C11B2DA8415D6B37200EC6AA4F3FDCDDE8EFB1E7A71375FE331A7B0E829A`
- patched `src/llama-vocab.cpp` SHA256: `7E07061170256C9F5A5ACF6AE30D5C6E5A0CBB89D706EEDAE95AC5F9B46230F1`

## Model identity

- file: `models/zenz-v3.2-small-Q5_K_M.gguf`
- SHA256: `29C223D4C23327B80FD13EBB5AB2555057A46317997D5DA391584FFBEF0DB673`

## Runtime assets

### x64

- source-tree file: `x64/llama-server.exe`
- PE: x64 / 0x8664
- SHA256: `FE9015591099ACDA45A37D8D8D83C1B1EABA9305CE3FFAA6B3808F9FA2953251`
- W1 canonical Zenz-v3 semantic differential: 12/12 tokenizer, first-token, cache-off, and production-like equality against the previous runtime.

### ARM64

- source-tree file: `arm64/llama-server.exe`
- PE: ARM64 / 0xAA64
- SHA256: `0ACD17D6EE5E361AFC06A7DCCD06EF012A00669546535889B874D5C3BB3DE81B`
- build basis: upstream `arm64-windows-llvm+static-release` preset with Clang target `arm64-pc-windows-msvc`
- local cross-build structural gate: PASS
- equivalent-contract native GitHub ARM64 gate: PASS
  - workflow run: https://github.com/koyasi777/mozkey/actions/runs/31893874478
  - workflow commit: `daa13a4df3c4b2ce4da5a51dcb2a29aa3f7f248f`
- exact final-MSI native validation: PASS
  - CI job: `Native ARM64 installed MSI Zenz audit`
  - the exact same-run ARM64 MSI is installed on native `windows-11-arm`
  - installed `llama-server.exe` is ARM64 and matches the fixed runtime SHA256
  - installed `mozc_zenz_scorer.exe` is ARM64
  - the installed model matches the fixed model SHA256
  - native `llama-server --version`, the canonical 12-case semantic gate, and the scorer named-pipe context gate pass from installed files

## Build/runtime policy

- CPU only
- `BUILD_SHARED_LIBS=OFF`
- `GGML_OPENMP=OFF`
- `GGML_NATIVE=OFF`
- `GGML_BLAS=OFF`
- `GGML_CPU_KLEIDIAI=OFF`
- `LLAMA_BUILD_UI=OFF`
- `LLAMA_OPENSSL=OFF`
- scorer launches `llama-server` with `--parallel 1`
- server binds to `127.0.0.1`
- scorer supplies a generated API key
- the Windows installer installs enabled outbound/block firewall rules for `mozc_zenz_scorer.exe` and `llama-server.exe`, in addition to the existing Mozc runtime rules
- the firewall rules must not break scorer-to-llama loopback inference and must be removed during uninstall
- no `ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`, or `llama.dll` is packaged
- `MSVCP140.dll` / `VCRUNTIME140*.dll` remain dynamic dependencies and are supplied through the existing architecture-specific MSI CRT payload.

## MSI architecture contract

- x64 MSI: x64 Mozkey + x64 scorer + x64 llama-server + x64 CRT
- ARM64 MSI: ARM64 Mozkey + ARM64 scorer + ARM64 llama-server + ARM64 CRT
- the Zenz model is architecture-independent and has one fixed SHA256.
- universal MSI is not treated as a dual-native whole-product package in this phase.

## Installed security gate

The final ARM64 MSI is also validated on native `windows-11-arm` with the
installed payload, not source-tree binaries:

- the seven Mozc outbound/block Windows Firewall rules are installed and point
  to the actual installed executables
- the Zenz scorer-to-llama path continues to pass with the firewall rules enabled
- scorer-owned `llama-server.exe` listens on loopback only
- scorer / llama have no non-loopback TCP connection during the audit
- uninstall removes the Zenz payload and the Mozc firewall rules

## Installer lifecycle gate

The native ARM64 lifecycle audit rebuilds the W2-predecessor package from the
audited baseline commit `c986db2c611c7255ba92b1d44fc6ac3fb6b098bf` and verifies
a normal MSI lifecycle on native `windows-11-arm`:

- the predecessor contains the historical x64 `llama-server.exe` and the four
  legacy ggml / llama DLLs
- installing the current MSI over that predecessor replaces the ProductCode
  while preserving the Mozc UpgradeCode
- the upgraded payload contains the native ARM64 llama/scorer runtime
- the four legacy runtime DLLs are removed
- the post-upgrade scorer context E2E gate passes
- a second install of the exact current MSI does not duplicate firewall rules
- final uninstall removes product registration, Zenz payload, and firewall rules
- forced `REINSTALL=ALL REINSTALLMODE=amus` is not part of the normal lifecycle
  contract

Development CI artifacts can have the same ProductVersion while validating the
upgrade mechanics. A published release must increase ProductVersion relative to
the previous published release, use a new ProductCode, and preserve the Mozc
UpgradeCode.

## Current install-directory compatibility note

The current WiX layout keeps `MozcDir` under `ProgramFilesFolder\Mozc` for
compatibility with the existing product layout. On 64-bit Windows this can
resolve to `C:\Program Files (x86)\Mozc`. Runtime architecture is therefore
verified from PE identity and hashes, not inferred from the directory name.

Changing this directory contract to `ProgramFiles64Folder` is outside this
runtime-architecture phase because it affects installer compatibility and must
be handled with its own upgrade/lifecycle audit.
