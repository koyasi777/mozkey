# macOS Zenz runtime assets

The macOS package embeds the following runtime assets under
`Mozc.app/Contents/Resources/ZenzRuntime`:

```text
ZenzRuntime/
├── mozc_zenz_scorer
├── llama-server
├── models/
│   └── zenz-v3.2-small-Q5_K_M.gguf
└── licenses/
```

`mozc_zenz_scorer` is built by Bazel. The following large redistributable
assets are intentionally excluded from Git:

```text
llama-server
models/zenz-v3.2-small-Q5_K_M.gguf
```

## Required source layout

Prepare a source directory with this structure:

```text
mozkey-macos-zenz-runtime/
├── llama-server
└── models/
    └── zenz-v3.2-small-Q5_K_M.gguf
```

The currently pinned assets are:

```text
llama-server
SHA-256:
e24e6b0928bd06fdefe68ea7c013acff1e5067a9318cf619c8b50e4712f7f931

zenz-v3.2-small-Q5_K_M.gguf
SHA-256:
29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673
```

The pinned `llama-server` is an arm64 Mach-O executable with a minimum
deployment target of macOS 12.0. It must depend only on macOS system
frameworks and libraries.

## Stage the assets

From the repository root:

```bash
src/mac/installer/zenz_runtime/stage_runtime_assets.zsh \
  "$HOME/dev/mozkey-macos-zenz-runtime"
```

The source directory can also be supplied through an environment variable:

```bash
MOZKEY_ZENZ_RUNTIME_SOURCE="$HOME/dev/mozkey-macos-zenz-runtime" \
  src/mac/installer/zenz_runtime/stage_runtime_assets.zsh
```

The script verifies the pinned hashes, architecture, deployment target and
dynamic-library dependencies before copying the files.

It also verifies that the staged large files remain excluded from Git.

## Build the macOS package

After staging the assets:

```bash
cd src

bazelisk \
  --output_user_root="$HOME/.cache/bazel_mozkey_macos_zenz_package" \
  build \
  --config oss_macos \
  --config release_build \
  //mac:package \
  --verbose_failures
```

The current package is arm64-only because the pinned `llama-server` runtime
is arm64-only.

This staging script does not download runtime artifacts. Release engineering
must provide the approved source directory containing the exact pinned files.
