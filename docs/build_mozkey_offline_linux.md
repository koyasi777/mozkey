# Native Linux Bazel vendor archives (experimental)

This optional tool adapts upstream Mozc commits `3be596609c`, `a62b8e39d6`
and `3823701961`. It does not change regular builds. It prepares external
Bazel repositories for the OSS Linux package and the debug test targets.
It is not a complete offline installation bundle or a reproducible-build
claim. Windows, macOS, cross-architecture builds, and Zenz runtime staging
are outside this initial scope.

## Prepare online

Use a dedicated Git checkout, not a checkout being used for another build.
Install the normal Linux build prerequisites (including Qt 6, IBus,
`pkg-config`, Git and Python 3.10+). Use the actual preinstalled Bazel
executable, not a launcher that may download Bazel. Use the same executable
and host architecture to consume the archive. Prepare the daily dictionary
using the existing `tools/dictionary/prepare_daily_dictionary.ps1` workflow.
See `src/data/dictionary_koyasi/README.md`. The four daily generated files
must be present; the vendor archive does not include them. Keep the
checked-in daily profile. The local OS SDK and other build prerequisites
must also be available on the consuming machine.

From the Git checkout root (replace the example Bazel path with your actual
Bazel executable):

```sh
BAZEL=/absolute/path/to/bazel
OUT="$HOME/mozkey-vendor-$(date +%Y%m%d-%H%M%S)"
python3 src/build_tools/create_bazel_vendor.py \
  --bazel_bin "$BAZEL" --output_dir "$OUT"
```

`OUT` must not exist and must be outside the checkout. The tool uses its own
Bazel output root and temporarily regenerates `src/MODULE.bazel.lock`, then
restores the previous contents (or absence) on normal completion or an
exception. Abrupt termination (e.g. SIGKILL or a power failure) can prevent
restoration; retain a backup of important local files. No existing output
or Bazel cache is deleted. An incomplete run leaves diagnostic files.

## Verify and consume

Only use an archive from a trusted source. Transfer its SHA-256 via a trusted
channel. A checksum stored next to an untrusted archive is not authentication.
Verify the archive before extracting to a fresh directory. For example:

```sh
cd "$OUT"
sha256sum -c bazel_vendor.tar.gz.sha256
EXTRACT=$(mktemp -d "$HOME/mozkey-vendor-extracted.XXXXXX")
tar -xzf bazel_vendor.tar.gz -C "$EXTRACT"
```

From the Git checkout root:

```sh
VENDOR="$EXTRACT/vendor_dir"
python3 src/build_tools/create_bazel_vendor.py \
  --bazel_bin "$BAZEL" --verify_vendor "$VENDOR"
```

This verifies the exact recorded build-definition file set and SHA-256 values,
Bazel executable/version, host OS/architecture, and the vendored lockfile.
It checks that the daily inputs exist, not their data provenance or content.
It does not hash all extracted repository contents. Keep the verified archive
and extracted tree under your control. The input manifest is conservative:
changes to build definitions can require re-vendoring. Other source or
host-environment changes can also affect repository resolution and builds.

To test consumption, use a separate disposable checkout with the same build
inputs and prepared daily files. Copy the vendored lockfile into that checkout's
`src/` (do not overwrite a lockfile in your active development checkout).
Use a fresh output root and repository cache. Disconnect the disposable
Linux VM from external networking or use an appropriate network namespace
before running the following commands. The Bazel flags alone do not prevent
repository rules or build tools from running network-capable subprocesses.

```sh
cd src
cp "$VENDOR/MODULE.bazel.lock" MODULE.bazel.lock
export ANDROID_NDK_HOME=
OFFLINE_ROOT=$(mktemp -d "$HOME/mozkey-offline-bazel.XXXXXX")
"$BAZEL" --batch --nosystem_rc --nohome_rc --output_user_root="$OFFLINE_ROOT" \
  build --config=oss_linux --config=release_build --vendor_dir="$VENDOR" \
  --lockfile_mode=error --repository_disable_download \
  --repository_cache="$OFFLINE_ROOT/repo-cache" //:package
"$BAZEL" --batch --nosystem_rc --nohome_rc --output_user_root="$OFFLINE_ROOT" \
  test --config=oss_linux -c dbg --vendor_dir="$VENDOR" \
  --lockfile_mode=error --repository_disable_download \
  --repository_cache="$OFFLINE_ROOT/repo-cache" --cache_test_results=no //...
```

The above is the intended validation procedure; actual offline success must
be demonstrated for the selected host and Bazel version. No claim of native
Linux offline-build success is made until the isolated build/test passes.
