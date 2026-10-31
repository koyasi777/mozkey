#!/bin/zsh

set -eu
setopt pipe_fail
setopt null_glob

export PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:${PATH:-}"

EXPECTED_FORMAT="mozkey-macos-zenz-runtime-v1"
EXPECTED_LLAMA_REPOSITORY="https://github.com/ggml-org/llama.cpp.git"
EXPECTED_LLAMA_TAG="v0.7.0"
EXPECTED_LLAMA_COMMIT="620d2225cc436ab0b001e3200e590e3b286ef9ab"
EXPECTED_PATCH_SHA="6db5c11b2da8415d6b37200ec6aa4f3fdcdde8efb1e7a71375fe331a7b0e829a"
EXPECTED_PATCHED_SOURCE_SHA="3a65249edbcaf4a21742e44f89a2be810a75e53c892bb5905e1a5dc71227184d"
EXPECTED_DUAL_LIVENESS_PATCH_SHA="3267e978f61456c32dfb83b15bf0f3a75393c5f98c946ee7a2846f7121b36dda"
EXPECTED_PATCHED_LLAMA_MAIN_SHA="2197ffc83775b865b97b35528a99f1daa692cf25eb4e952509e46e2cfebbf976"
EXPECTED_MODEL_REPOSITORY="Miwa-Keita/zenz-v3.2-small-gguf"
EXPECTED_MODEL_REVISION="c67e03e07d215c869f591b274c1631170d3e11fe"
EXPECTED_MODEL_SHA="29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673"
EXPECTED_MINIMUM_MACOS="12.0"
EXPECTED_ARCHITECTURES="x86_64,arm64"

script_dir="${0:A:h}"
src_root="${script_dir:h:h:h}"
builder="$script_dir/build_universal_runtime.zsh"

pkg="${1:-}"
output_root="${2:-}"

if [[ -z "$pkg" ]]; then
  print -u2 -- "Usage:"
  print -u2 -- "  $0 /path/to/Mozc.pkg [/path/to/audit-output]"
  exit 2
fi

pkg="${pkg:A}"

if [[ ! -f "$pkg" ]]; then
  print -u2 -- "ERROR: Package missing: $pkg"
  exit 1
fi

if [[ -z "$output_root" ]]; then
  output_root="${pkg:h}/verify-$(date '+%Y%m%d_%H%M%S')"
fi

output_root="${output_root:A}"

if [[ -e "$output_root" ]]; then
  print -u2 -- "ERROR: Verification output already exists: $output_root"
  exit 1
fi

/bin/mkdir -p "$output_root"

expanded="$output_root/expanded"
audit="$output_root/audit.txt"
scorer_log="$output_root/packaged-scorer.log"

die() {
  print -u2 -- "ERROR: $*"
  exit 1
}

sha256_file() {
  /usr/bin/shasum -a 256 "$1" |
    /usr/bin/awk '{print $1}'
}

contract_value() {
  local contract="$1"
  local key="$2"

  local count
  count="$(
    /usr/bin/awk \
      -F= \
      -v key="$key" \
      '$1 == key {count++} END {print count + 0}' \
      "$contract"
  )"

  [[ "$count" == "1" ]] ||
    die "BUILD-CONTRACT key must appear exactly once: $key"

  /usr/bin/awk \
    -F= \
    -v key="$key" \
    '$1 == key {
      print substr($0, index($0, "=") + 1)
      exit
    }' \
    "$contract"
}

extract_minos() {
  local binary="$1"
  local arch="$2"

  /usr/bin/otool \
    -arch "$arch" \
    -l "$binary" |
    /usr/bin/awk '
      /LC_BUILD_VERSION/ {
        found = 1
        next
      }

      found && $1 == "minos" {
        print $2
        exit
      }
    '
}

verify_universal() {
  local binary="$1"
  local label="$2"

  [[ -f "$binary" ]] ||
    die "$label missing: $binary"

  local archs
  archs="$(/usr/bin/lipo -archs "$binary")"

  echo "$label architectures = $archs"

  print -- "$archs" |
    /usr/bin/grep -qw arm64 ||
    die "$label has no arm64 slice"

  print -- "$archs" |
    /usr/bin/grep -qw x86_64 ||
    die "$label has no x86_64 slice"

  for arch in arm64 x86_64; do
    local minos
    minos="$(extract_minos "$binary" "$arch")"

    echo "$label $arch minimum macOS = $minos"

    [[ "$minos" == "$EXPECTED_MINIMUM_MACOS" ]] ||
      die "$label $arch minimum macOS mismatch"
  done
}

verify_system_only() {
  local binary="$1"
  local label="$2"

  for arch in arm64 x86_64; do
    local unexpected
    unexpected="$(
      /usr/bin/otool \
        -arch "$arch" \
        -L "$binary" |
        /usr/bin/awk 'NR > 1 {print $1}' |
        /usr/bin/grep -Ev '^(/System/Library/|/usr/lib/)' ||
        true
    )"

    [[ -z "$unexpected" ]] || {
      print -u2 -- "Unexpected non-system dylibs in $label ($arch):"
      print -u2 -- "$unexpected"
      exit 1
    }
  done
}

echo "===== PACKAGE IDENTITY ====="

pkg_sha="$(sha256_file "$pkg")"
pkg_size="$(/usr/bin/stat -f '%z' "$pkg")"

echo "Package = $pkg"
echo "Size    = $pkg_size"
echo "SHA256  = $pkg_sha"

/usr/sbin/pkgutil --check-signature "$pkg" || true

echo
echo "===== EXPAND PACKAGE ====="

/usr/sbin/pkgutil \
  --expand-full \
  "$pkg" \
  "$expanded"

distribution_candidates=(
  "$expanded"/**/Distribution(N)
)

(( ${#distribution_candidates[@]} == 1 )) ||
  die "Expected exactly one Distribution"

distribution="${distribution_candidates[1]}"

app_candidates=(
  "$expanded"/**/Mozc.app(N/)
)

(( ${#app_candidates[@]} == 1 )) ||
  die "Expected exactly one Mozc.app"

app="${app_candidates[1]%/}"
app_exec="$app/Contents/MacOS/Mozc"
runtime="$app/Contents/Resources/ZenzRuntime"
llama="$runtime/llama-server"
scorer="$runtime/mozc_zenz_scorer"
guard="$runtime/mozc_zenz_guard"
model="$runtime/models/zenz-v3.2-small-Q5_K_M.gguf"
contract="$runtime/BUILD-CONTRACT.txt"
license_dir="$runtime/licenses"

echo "Distribution = $distribution"
echo "Mozc.app     = $app"
echo "ZenzRuntime  = $runtime"

echo
echo "===== VERIFY DISTRIBUTION ====="

/usr/bin/python3 - "$distribution" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()

mins = [
    node.attrib.get("min")
    for node in root.findall(".//allowed-os-versions/os-version")
]

if "12.0" not in mins:
    raise SystemExit(f"minimum macOS 12.0 not found: {mins}")

titles = [
    (node.text or "").strip()
    for node in root.findall("./title")
]

if titles != ["Mozkey"]:
    raise SystemExit(f"unexpected installer title: {titles}")

host_architectures = [
    node.attrib["hostArchitectures"]
    for node in root.findall(".//options")
    if "hostArchitectures" in node.attrib
]

if len(host_architectures) != 1:
    raise SystemExit(
        "expected exactly one hostArchitectures declaration: "
        f"{host_architectures}"
    )

architectures = [
    value.strip()
    for value in host_architectures[0].split(",")
    if value.strip()
]

if len(architectures) != 2 or set(architectures) != {"arm64", "x86_64"}:
    raise SystemExit(
        "unexpected hostArchitectures declaration: "
        f"{host_architectures[0]!r}"
    )

print("Minimum macOS     = 12.0")
print("Installer title   = Mozkey")
print("hostArchitectures = arm64,x86_64")
PY

echo
echo "===== VERIFY REQUIRED PAYLOAD ====="

required_payload=(
  "$app_exec"
  "$llama"
  "$scorer"
  "$guard"
  "$model"
  "$contract"
  "$app/Contents/Resources/DICTIONARY_NOTICES.md"
  "$app/Contents/Resources/SudachiDict-LEGAL.txt"
  "$license_dir/Apache-2.0.txt"
  "$license_dir/llama.cpp-MIT.txt"
  "$license_dir/THIRD_PARTY_NOTICES.md"
  "$license_dir/zenz-v3.2-small-gguf.txt"
)

for payload_file in "${required_payload[@]}"; do
  [[ -f "$payload_file" ]] ||
    die "Required final payload missing: $payload_file"

  echo "$payload_file"
done

echo
echo "===== VERIFY EMBEDDED BUILD CONTRACT ====="

[[ -f "$builder" ]] ||
  die "Current tracked formal builder missing"

builder_sha="$(sha256_file "$builder")"

format="$(contract_value "$contract" format)"
contract_builder_sha="$(
  contract_value "$contract" builder_script_sha256
)"
llama_repository="$(
  contract_value "$contract" llama_cpp_repository
)"
llama_tag="$(contract_value "$contract" llama_cpp_tag)"
llama_commit="$(contract_value "$contract" llama_cpp_commit)"
patch_sha="$(contract_value "$contract" zenz_patch_sha256)"
patched_source_sha="$(
  contract_value "$contract" patched_llama_vocab_sha256
)"
dual_liveness_patch_sha="$(
  contract_value "$contract" mozkey_dual_liveness_patch_sha256
)"
patched_llama_main_sha="$(
  contract_value "$contract" patched_llama_server_main_sha256
)"
model_repository="$(
  contract_value "$contract" model_repository
)"
model_revision="$(contract_value "$contract" model_revision)"
contract_model_sha="$(contract_value "$contract" model_sha256)"
minimum_macos="$(contract_value "$contract" minimum_macos)"
architectures="$(contract_value "$contract" architectures)"
unsigned_universal_sha="$(
  contract_value "$contract" universal_unsigned_sha256
)"
signing="$(contract_value "$contract" signing)"

[[ "$format" == "$EXPECTED_FORMAT" ]] ||
  die "Unexpected BUILD-CONTRACT format"

[[ "$contract_builder_sha" == "$builder_sha" ]] ||
  die "Embedded BUILD-CONTRACT was produced by a different builder"

[[ "$llama_repository" == "$EXPECTED_LLAMA_REPOSITORY" ]] ||
  die "Unexpected llama.cpp repository"

[[ "$llama_tag" == "$EXPECTED_LLAMA_TAG" ]] ||
  die "Unexpected llama.cpp tag"

[[ "$llama_commit" == "$EXPECTED_LLAMA_COMMIT" ]] ||
  die "Unexpected llama.cpp commit"

[[ "$patch_sha" == "$EXPECTED_PATCH_SHA" ]] ||
  die "Unexpected patch identity"

[[ "$patched_source_sha" == "$EXPECTED_PATCHED_SOURCE_SHA" ]] ||
  die "Unexpected patched source identity"

[[ "$dual_liveness_patch_sha" == "$EXPECTED_DUAL_LIVENESS_PATCH_SHA" ]] ||
  die "Unexpected dual-liveness patch identity"

[[ "$patched_llama_main_sha" == "$EXPECTED_PATCHED_LLAMA_MAIN_SHA" ]] ||
  die "Unexpected patched llama-server main.cpp identity"

[[ -f "$script_dir/patches/llama-server-mozkey-dual-liveness.patch" ]] ||
  die "Dual-liveness patch missing from verifier source tree"

[[ "$(sha256_file "$script_dir/patches/llama-server-mozkey-dual-liveness.patch")" == "$dual_liveness_patch_sha" ]] ||
  die "Current dual-liveness patch differs from embedded BUILD-CONTRACT"

[[ "$model_repository" == "$EXPECTED_MODEL_REPOSITORY" ]] ||
  die "Unexpected model repository"

[[ "$model_revision" == "$EXPECTED_MODEL_REVISION" ]] ||
  die "Unexpected model revision"

[[ "$contract_model_sha" == "$EXPECTED_MODEL_SHA" ]] ||
  die "Unexpected model SHA in contract"

[[ "$minimum_macos" == "$EXPECTED_MINIMUM_MACOS" ]] ||
  die "Unexpected minimum macOS contract"

[[ "$architectures" == "$EXPECTED_ARCHITECTURES" ]] ||
  die "Unexpected architecture contract"

[[ "$signing" == "downstream_after_lipo" ]] ||
  die "Unexpected signing contract"

if [[ -n "${MOZKEY_ZENZ_EXPECTED_CONTRACT:-}" ]]; then
  expected_contract="${MOZKEY_ZENZ_EXPECTED_CONTRACT:A}"

  [[ -f "$expected_contract" ]] ||
    die "Expected contract file missing: $expected_contract"

  [[ "$(sha256_file "$expected_contract")" == "$(sha256_file "$contract")" ]] ||
    die "Embedded BUILD-CONTRACT differs from expected build input"

  echo "Expected BUILD-CONTRACT identity = EXACT"
fi

echo "Builder SHA256            = $builder_sha"
echo "Unsigned runtime SHA256   = $unsigned_universal_sha"
echo "Embedded contract SHA256  = $(sha256_file "$contract")"

echo
echo "===== VERIFY MODEL / LICENSE IDENTITIES ====="

actual_model_sha="$(sha256_file "$model")"

echo "Model SHA256 = $actual_model_sha"

[[ "$actual_model_sha" == "$EXPECTED_MODEL_SHA" ]] ||
  die "Final model identity mismatch"

for license_name in \
  Apache-2.0.txt \
  llama.cpp-MIT.txt \
  THIRD_PARTY_NOTICES.md \
  zenz-v3.2-small-gguf.txt; do

  source_license="$script_dir/licenses/$license_name"
  payload_license="$license_dir/$license_name"

  [[ "$(sha256_file "$source_license")" == "$(sha256_file "$payload_license")" ]] ||
    die "License/notice identity mismatch: $license_name"
done

for dictionary_notice in DICTIONARY_NOTICES.md SudachiDict-LEGAL.txt; do
  source_notice="$src_root/data/installer/$dictionary_notice"
  packaged_notice="$app/Contents/Resources/$dictionary_notice"
  [[ "$(sha256_file "$source_notice")" == "$(sha256_file "$packaged_notice")" ]] ||
    die "Dictionary license notice mismatch: $dictionary_notice"
done
echo "Dictionary notice identities = EXACT"

echo "Model / license identity = EXACT"

echo
echo "===== VERIFY UNIVERSAL EXECUTABLES ====="

verify_universal "$app_exec" "Mozc"
verify_universal "$scorer" "mozc_zenz_scorer"
verify_universal "$guard" "mozc_zenz_guard"
verify_universal "$llama" "llama-server"

verify_system_only "$scorer" "mozc_zenz_scorer"
verify_system_only "$guard" "mozc_zenz_guard"
verify_system_only "$llama" "llama-server"

echo "Runtime dynamic libraries = SYSTEM ONLY"

echo
echo "===== VERIFY TOKENIZER MARKER ====="

tmp_slices="$output_root/llama-slices"
/bin/mkdir -p "$tmp_slices"

/usr/bin/lipo \
  "$llama" \
  -thin arm64 \
  -output "$tmp_slices/llama.arm64"

/usr/bin/lipo \
  "$llama" \
  -thin x86_64 \
  -output "$tmp_slices/llama.x86_64"

for thin in \
  "$tmp_slices/llama.arm64" \
  "$tmp_slices/llama.x86_64"; do

  /usr/bin/strings "$thin" |
    /usr/bin/grep -F \
      'gpt2-small-japanese-char' \
      >/dev/null ||
    die "Tokenizer marker missing: $thin"
done

echo "Tokenizer marker = PRESENT BOTH SLICES"

# The packaged worker must carry the opt-in protocol in both slices, not
# merely declare a BUILD-CONTRACT that was produced by a matching script.
for thin in \
  "$tmp_slices/llama.arm64" \
  "$tmp_slices/llama.x86_64"; do
  /usr/bin/strings "$thin" | /usr/bin/grep -F -- \
    '--mozkey-zenz-dual-liveness-v1' >/dev/null ||
    die "Dual-liveness worker marker missing: $thin"
done

echo "Dual-liveness worker marker = PRESENT BOTH SLICES"

echo
echo "===== VERIFY NESTED CODESIGN ====="

/usr/bin/codesign \
  --verify \
  --strict \
  --verbose=4 \
  "$llama"

/usr/bin/codesign \
  --verify \
  --strict \
  --verbose=4 \
  "$scorer"

/usr/bin/codesign \
  --verify \
  --strict \
  --verbose=4 \
  "$guard"

/usr/bin/codesign \
  --verify \
  --deep \
  --strict \
  --verbose=4 \
  "$app"

echo "Nested codesign = PASS"

echo
echo "===== BUILD NATIVE PACKAGE PROBE ====="

cd "$src_root"

bazelisk \
  --output_user_root="${MOZKEY_ZENZ_VERIFY_BAZEL_ROOT:-$HOME/.cache/bazel_mozkey_macos_zenz_pkg_verify}" \
  build \
  --config=oss_macos \
  --config=release_build \
  //zenz_scorer:zenz_runtime_package_probe \
  --verbose_failures

probe="$src_root/bazel-bin/zenz_scorer/zenz_runtime_package_probe"

[[ -x "$probe" ]] ||
  die "Package probe binary missing: $probe"

echo "Probe architectures = $(/usr/bin/lipo -archs "$probe")"
echo "Native host         = $(/usr/bin/uname -m)"

echo
echo "===== NATIVE PACKAGED SCORER END-TO-END ====="

"$probe" --assert-unavailable

scorer_pid=""

cleanup_scorer() {
  if [[ -n "$scorer_pid" ]] && /bin/kill -0 "$scorer_pid" 2>/dev/null; then
    /bin/kill -TERM "$scorer_pid" 2>/dev/null || true

    local i
    for i in {1..100}; do
      if ! /bin/kill -0 "$scorer_pid" 2>/dev/null; then
        break
      fi
      /bin/sleep 0.1
    done

    if /bin/kill -0 "$scorer_pid" 2>/dev/null; then
      /bin/kill -KILL "$scorer_pid" 2>/dev/null || true
    fi

    wait "$scorer_pid" 2>/dev/null || true
  fi
}

trap cleanup_scorer EXIT INT TERM

host_arch="$(/usr/bin/uname -m)"

if [[ "$host_arch" == "arm64" ]]; then
  /usr/bin/arch \
    -arm64 \
    "$scorer" \
    >"$scorer_log" \
    2>&1 &
else
  "$scorer" \
    >"$scorer_log" \
    2>&1 &
fi

scorer_pid="$!"

echo "Packaged scorer PID = $scorer_pid"

if ! "$probe" --request; then
  echo
  echo "===== PACKAGED SCORER LOG ====="
  /usr/bin/tail -n 300 "$scorer_log" 2>/dev/null || true
  die "Native packaged scorer end-to-end request failed"
fi

cleanup_scorer
scorer_pid=""
trap - EXIT INT TERM

echo "Native packaged scorer end-to-end = PASS"

echo
echo "===== WRITE VERIFIER AUDIT ====="

{
  echo "===== MOZKEY macOS ZENZ FINAL PACKAGE VERIFICATION ====="
  echo "Timestamp = $(date '+%Y-%m-%d %H:%M:%S %z')"
  echo "Package = $pkg"
  echo "Package SHA256 = $pkg_sha"
  echo "Package size = $pkg_size"
  echo "Native host = $host_arch"
  echo "Embedded BUILD-CONTRACT SHA256 = $(sha256_file "$contract")"
  echo "Embedded builder SHA256 = $contract_builder_sha"
  echo "Embedded unsigned runtime SHA256 = $unsigned_universal_sha"
  echo "Dual-liveness patch SHA256 = $dual_liveness_patch_sha"
  echo "Patched llama main SHA256 = $patched_llama_main_sha"
  echo "Final signed llama SHA256 = $(sha256_file "$llama")"
  echo "Final signed scorer SHA256 = $(sha256_file "$scorer")"
  echo "Final signed guard SHA256 = $(sha256_file "$guard")"
  echo "Model SHA256 = $actual_model_sha"
  echo "Mozc architectures = $(/usr/bin/lipo -archs "$app_exec")"
  echo "Scorer architectures = $(/usr/bin/lipo -archs "$scorer")"
  echo "Guard architectures = $(/usr/bin/lipo -archs "$guard")"
  echo "llama architectures = $(/usr/bin/lipo -archs "$llama")"
  echo
  echo "===== RESULT ====="
  echo "PKG expansion                    = PASS"
  echo "Distribution                     = PASS"
  echo "Embedded BUILD-CONTRACT          = PASS"
  echo "Model / licenses                 = EXACT"
  echo "Mozc Universal                   = PASS"
  echo "mozc_zenz_scorer Universal       = PASS"
  echo "mozc_zenz_guard Universal        = PASS"
  echo "llama-server Universal           = PASS"
  echo "All deployment targets           = 12.0"
  echo "Runtime dylibs                   = SYSTEM ONLY"
  echo "Tokenizer marker                 = PRESENT BOTH SLICES"
  echo "Dual-liveness worker marker      = PRESENT BOTH SLICES"
  echo "Nested codesign                  = PASS"
  echo "Native packaged scorer E2E       = PASS"
} > "$audit"

audit_sha="$(sha256_file "$audit")"

echo
echo "===== RESULT ====="
echo "Package                    = $pkg"
echo "Package SHA256             = $pkg_sha"
echo "Audit                      = $audit"
echo "Audit SHA256               = $audit_sha"
echo "Native host                = $host_arch"
echo "Distribution               = PASS"
echo "Embedded BUILD-CONTRACT    = PASS"
echo "Dual-liveness runtime       = SOURCE CONTRACT + MARKER PASS"
echo "Model / licenses           = EXACT"
echo "Mozc arches                = $(/usr/bin/lipo -archs "$app_exec")"
echo "Scorer arches              = $(/usr/bin/lipo -archs "$scorer")"
echo "Guard arches               = $(/usr/bin/lipo -archs "$guard")"
echo "llama arches               = $(/usr/bin/lipo -archs "$llama")"
echo "Nested codesign            = PASS"
echo "Native packaged scorer E2E = PASS"
