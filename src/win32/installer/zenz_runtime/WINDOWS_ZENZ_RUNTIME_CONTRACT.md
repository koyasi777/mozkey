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
- exact final-MSI native validation: pending package gate.

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
- no `ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`, or `llama.dll` is packaged
- `MSVCP140.dll` / `VCRUNTIME140*.dll` remain dynamic dependencies and are supplied through the existing architecture-specific MSI CRT payload.

## MSI architecture contract

- x64 MSI: x64 Mozkey + x64 scorer + x64 llama-server + x64 CRT
- ARM64 MSI: ARM64 Mozkey + ARM64 scorer + ARM64 llama-server + ARM64 CRT
- the Zenz model is architecture-independent and has one fixed SHA256.
- universal MSI is not treated as a dual-native whole-product package in this phase.
