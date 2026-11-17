# Linux Zenz runtime patch contract (experimental)

This directory tracks a **Linux-only overlay** on the existing macOS
`llama-server-mozkey-dual-liveness.patch`. It does not install a runtime,
include a GGUF file, or enable Zenz in the Linux IBus Mozkey engine.

Pinned `llama.cpp` upstream identity:

- Tag: `v0.7.0`
- Commit: `620d2225cc436ab0b001e3200e590e3b286ef9ab`
- `tools/server/main.cpp` Git blob: `7f17c56a8c2989ba4071a50aa7828cfdcb50a08b`
- Original macOS dual-liveness patch SHA-256:
  `3267e978f61456c32dfb83b15bf0f3a75393c5f98c946ee7a2846f7121b36dda`

Apply the two patches **in order** to a separately checked-out and pinned
`llama.cpp` source tree. Set `MOZKEY_REPO` to the absolute Mozkey repository
root before running these commands **from the llama.cpp checkout**:

```bash
git apply "$MOZKEY_REPO/src/mac/installer/zenz_runtime/patches/llama-server-mozkey-dual-liveness.patch"
git apply --unidiff-zero "$MOZKEY_REPO/src/unix/zenz_runtime/patches/llama-server-mozkey-linux-dual-liveness.patch"
```

The Linux overlay uses zero-context hunks; apply it only after verifying
the upstream pinned commit and the original patch SHA. The original macOS
patch is intentionally unchanged. Linux activates the
same validated FD23 (scorer) and FD24 (guard) lifetime protocol only with the
`--mozkey-zenz-dual-liveness-v1` first argument, which the patched launcher
removes before invoking llama.cpp's option parser. The Linux Z1b guard passes
its sealed memfd authentication key via FD25 and `--api-key-file /proc/self/fd/25`.
This second patch **does not** change llama.cpp's model/tokenizer support.
The existing Japanese tokenizer compatibility patch must also be applied
before using a Zenz model.

Offline mock verification from the Mozkey repository root (Linux):

```bash
python3 src/unix/zenz_runtime/verify_linux_dual_liveness.py
```

This test reconstructs the exact pinned `tools/server/main.cpp`, verifies the
original patch identity, applies both patches in an isolated temporary tree,
compiles a stub in place of llama.cpp's real server, and probes: non-opt-in
startup, absent/wrong lifetime descriptors, scorer EOF, guard EOF, and FD25
key-file visibility. The only processes stopped are children owned by the test.
It downloads nothing and does not run a GGUF model.

**Not validated here:** a fully compiled pinned `llama-server`, model load,
completion correctness, tokenizer patch, CPU/ABI portability, real-model
orphan prevention, packaging, licensing, and macOS regression CI. All remain
separate gates. `PackageSafetyGate=NOT_PASSED_RUNTIME_COEXISTENCE`.
