# macOS Zenz runtime assets

The Zenz-enabled macOS package embeds the following runtime under
`Mozc.app/Contents/Resources/ZenzRuntime`:

```text
ZenzRuntime/
├── mozc_zenz_scorer
├── llama-server
├── BUILD-CONTRACT.txt
├── models/
│   └── zenz-v3.2-small-Q5_K_M.gguf
└── licenses/
```

`mozc_zenz_scorer` is built by Bazel as one Universal Mach-O executable.
`llama-server` is also one same-path Universal executable containing both
`arm64` and `x86_64` slices. The GGUF model is architecture-neutral.

The large runtime assets and generated build contract are staged locally and
are intentionally excluded from Git:

```text
llama-server
BUILD-CONTRACT.txt
models/zenz-v3.2-small-Q5_K_M.gguf
```

## Source contract

The runtime is built from the exact llama.cpp source contract below:

```text
Repository:
https://github.com/ggml-org/llama.cpp.git

Tag:
b10268

Commit:
6b5224cfccdb9caf4c0a0a87692fddad22c7e969
```

Mozkey applies the tracked compatibility patch:

```text
patches/llama-vocab-gpt2-small-japanese-char.patch
SHA-256:
6db5c11b2da8415d6b37200ec6aa4f3fdcdde8efb1e7a71375fe331a7b0e829a
```

The patched `src/llama-vocab.cpp` identity is:

```text
36d8d6db0603a511ea4a490a8f0da28e2054327e3d4fe9b9e1481a2150eb1317
```

The patch classifies the model tokenizer pre-tokenizer
`gpt2-small-japanese-char` as the llama.cpp GPT-2 vocabulary pre-type.

The pinned model source is:

```text
Repository:
Miwa-Keita/zenz-v3.2-small-gguf

Revision:
c67e03e07d215c869f591b274c1631170d3e11fe

Remote filename:
ggml-model-Q5_K_M.gguf

Packaged filename:
zenz-v3.2-small-Q5_K_M.gguf

SHA-256:
29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673
```

## Formal Universal runtime builder

`build_universal_runtime.zsh` is the source-of-truth runtime builder. It:

1. clones the exact llama.cpp tag and verifies the exact commit;
2. configures both arm64 and x86_64 build directories while the source is
   still clean so generated build-info records the pinned commit;
3. applies the tracked Zenz tokenizer patch;
4. builds arm64 natively and x86_64 through the validated cross-build path;
5. validates architecture, macOS 12.0 deployment target, system-only dynamic
   libraries, and the tokenizer marker on each thin binary;
6. combines the generated thin binaries with `lipo`;
7. verifies that the Universal slices preserve the generated thin binaries;
8. acquires or accepts the exact pinned GGUF model and verifies its hash;
9. performs a native Apple Silicon model-load and completion smoke test on a
   temporary ad-hoc-signed copy; and
10. writes `BUILD-CONTRACT.txt` containing the exact generated binary hashes,
    builder identity, source identity, patch identity, model identity, and
    build-environment provenance.

The recovered build policy remains:

```text
CMAKE_BUILD_TYPE=Release
CMAKE_OSX_DEPLOYMENT_TARGET=12.0
BUILD_SHARED_LIBS=OFF
GGML_ACCELERATE=ON
GGML_BLAS=OFF
GGML_CCACHE=OFF
GGML_CPU_KLEIDIAI=OFF
GGML_METAL=ON
GGML_METAL_EMBED_LIBRARY=ON
GGML_NATIVE=OFF
GGML_OPENMP=OFF
LLAMA_BUILD_COMMON=ON
LLAMA_BUILD_EXAMPLES=OFF
LLAMA_BUILD_MTMD=OFF
LLAMA_BUILD_SERVER=ON
LLAMA_BUILD_TESTS=OFF
LLAMA_BUILD_TOOLS=ON
LLAMA_BUILD_UI=OFF
LLAMA_LLGUIDANCE=OFF
LLAMA_OPENSSL=OFF
LLAMA_SUBPROCESS=ON
LLAMA_USE_PREBUILT_UI=OFF
LLAMA_USE_SYSTEM_GGML=OFF
```

For the x86_64 build on an Apple Silicon host, the builder explicitly enters
CMake cross-compiling mode with `CMAKE_SYSTEM_NAME=Darwin`,
`CMAKE_SYSTEM_PROCESSOR=x86_64`,
`CMAKE_OSX_ARCHITECTURES=x86_64`, and
`HOST_CXX_COMPILER=/usr/bin/c++`. Build-time helpers therefore execute as host
arm64 binaries while the final thin runtime is x86_64.

Create a fresh runtime output directory:

```bash
src/mac/installer/zenz_runtime/build_universal_runtime.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"
```

If the exact pinned model already exists locally, avoid downloading it again:

```bash
MOZKEY_ZENZ_MODEL_SOURCE="$HOME/path/to/zenz-v3.2-small-Q5_K_M.gguf" \
  src/mac/installer/zenz_runtime/build_universal_runtime.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"
```

The output is:

```text
mozkey-macos-zenz-runtime/
├── llama-server
├── BUILD-CONTRACT.txt
├── logs/
└── models/
    └── zenz-v3.2-small-Q5_K_M.gguf
```

The release output `llama-server` is intentionally left unsigned. Signing is
performed later by the Mozkey package pipeline after `lipo`, first on the raw
Zenz helpers and then on their parent app.

## BUILD-CONTRACT semantics

The generated binary SHA-256 values are build outputs, not globally fixed
release constants. The source identity, patch identity, build recipe, model
identity, architecture contract, and deployment target are fixed. The staging
step requires the runtime artifact to have been produced by the current
tracked builder and verifies the generated hashes recorded in its own
`BUILD-CONTRACT.txt`.

This avoids treating a one-machine prototype hash as the permanent release
definition while retaining byte-level validation within each concrete build.

For reference only, the implementation proof completed on 2026-08-14 used:

```text
Validated reference arm64 thin:
e24e6b0928bd06fdefe68ea7c013acff1e5067a9318cf619c8b50e4712f7f931

Validated reference x86_64 thin:
f18d5e851c5cfa11c5d3b07925a22d12a6e5e1279f6d9b894d9b8100b732802a

Validated reference unsigned Universal:
657f80a977cb6784fb6eab859e85d4ed51f523e3092c383075c8c0a48eb603e8
```

Those reference hashes are evidence from the validated prototype, not
mandatory hashes for future source builds.

## Stage the formal runtime

From the repository root:

```bash
src/mac/installer/zenz_runtime/stage_runtime_assets.zsh \
  "$HOME/Downloads/mozkey-macos-zenz-runtime"
```

The staging script verifies:

```text
BUILD-CONTRACT format
current tracked builder SHA-256
exact llama.cpp repository/tag/commit
exact tracked patch SHA-256
exact patched llama-vocab.cpp SHA-256
exact model repository/revision/SHA-256
generated Universal SHA-256
generated arm64 and x86_64 slice SHA-256 values
arm64 + x86_64 architecture presence
macOS 12.0 deployment target on both slices
system-only dynamic-library dependencies
Zenz tokenizer compatibility marker on both slices
Git-ignore state of llama-server, model, and BUILD-CONTRACT.txt
```

It then stages all three generated release inputs:

```text
llama-server
models/zenz-v3.2-small-Q5_K_M.gguf
BUILD-CONTRACT.txt
```

## Build a Zenz-enabled Universal macOS package

After staging:

```bash
cd src

bazelisk \
  --output_user_root="$HOME/.cache/bazel_mozkey_macos_zenz_package" \
  build \
  --config=oss_macos \
  --config=release_build \
  --macos_cpus=x86_64,arm64 \
  --define=macos_zenz_runtime=1 \
  //mac:package \
  --verbose_failures
```

`--define=macos_zenz_runtime=1` is required for a distributable Zenz-enabled
package. Without it, the generic clean-checkout package build intentionally
does not refer to the Git-excluded runtime/model/build-contract assets.

A successful Bazel build alone is not a release gate. The final `.pkg` must
still be expanded and validated for the actual packaged Universal executables,
deployment targets, model and license identities, embedded build contract,
nested code signatures, Distribution constraints, and native runtime
behavior on Apple Silicon and Intel.

## Verify the final package

`verify_release_package.zsh` is the package-level release verifier. It operates
on the expanded final `.pkg`, not on Bazel inputs.

It verifies:

```text
Distribution minimum macOS and architecture policy
embedded BUILD-CONTRACT provenance
model and license identities
Universal Mozc / mozc_zenz_scorer / llama-server executables
macOS 12.0 deployment target on both slices
system-only scorer and llama-server dependencies
tokenizer compatibility marker on both llama-server slices
nested code signatures
native mozc_zenz_scorer -> llama-server -> model request over the production
Unix-domain wire protocol
```

The verifier refuses the native runtime test if another Zenz scorer is already
accepting connections on the production socket. This prevents a false pass
against an installed or unrelated scorer process.

Example:

```bash
MOZKEY_ZENZ_EXPECTED_CONTRACT="$PWD/mac/installer/zenz_runtime/BUILD-CONTRACT.txt" \
  mac/installer/zenz_runtime/verify_release_package.zsh \
  bazel-bin/mac/Mozc.pkg \
  "$HOME/Downloads/mozkey-zenz-package-audit"
```

The same verifier is intended to run against the exact same package artifact
on both an Apple Silicon runner and a native Intel runner.
