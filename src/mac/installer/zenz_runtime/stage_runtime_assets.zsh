#!/bin/zsh

set -eu
setopt pipe_fail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:${PATH:-}"

EXPECTED_FORMAT="mozkey-macos-zenz-runtime-v1"

EXPECTED_LLAMA_REPOSITORY="https://github.com/ggml-org/llama.cpp.git"
EXPECTED_LLAMA_TAG="b10268"
EXPECTED_LLAMA_COMMIT="6b5224cfccdb9caf4c0a0a87692fddad22c7e969"
EXPECTED_PATCH_SHA256="6db5c11b2da8415d6b37200ec6aa4f3fdcdde8efb1e7a71375fe331a7b0e829a"
EXPECTED_PATCHED_SOURCE_SHA256="36d8d6db0603a511ea4a490a8f0da28e2054327e3d4fe9b9e1481a2150eb1317"

EXPECTED_MODEL_REPOSITORY="Miwa-Keita/zenz-v3.2-small-gguf"
EXPECTED_MODEL_REVISION="c67e03e07d215c869f591b274c1631170d3e11fe"
EXPECTED_MODEL_REMOTE_FILE="ggml-model-Q5_K_M.gguf"
EXPECTED_MODEL_OUTPUT_FILE="zenz-v3.2-small-Q5_K_M.gguf"
EXPECTED_MODEL_SHA256="29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673"

EXPECTED_MINIMUM_MACOS="12.0"
EXPECTED_ARCHITECTURES="x86_64,arm64"

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
  /usr/bin/otool -l "$1" |
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

check_system_only() {
  local binary="$1"
  local unexpected

  unexpected="$(
    /usr/bin/otool -L "$binary" |
      /usr/bin/awk 'NR > 1 {print $1}' |
      /usr/bin/grep -Ev '^(/System/Library/|/usr/lib/)' ||
      true
  )"

  [[ -z "$unexpected" ]] || {
    print -u2 -- "Unexpected non-system dynamic libraries in $binary:"
    print -u2 -- "$unexpected"
    return 1
  }
}

check_tokenizer_marker() {
  /usr/bin/strings "$1" |
    /usr/bin/grep -F \
      'gpt2-small-japanese-char' \
      >/dev/null
}

script_path="${0:A}"
script_dir="${script_path:h}"
builder="$script_dir/build_universal_runtime.zsh"

source_root="${1:-${MOZKEY_ZENZ_RUNTIME_SOURCE:-}}"

if [[ -z "$source_root" ]]; then
  print -u2 -- "Usage:"
  print -u2 -- "  $0 /path/to/formal-runtime-output"
  print -u2 -- ""
  print -u2 -- "Alternatively set MOZKEY_ZENZ_RUNTIME_SOURCE."
  exit 2
fi

source_root="${source_root:A}"

source_llama="$source_root/llama-server"
source_model="$source_root/models/$EXPECTED_MODEL_OUTPUT_FILE"
source_contract="$source_root/BUILD-CONTRACT.txt"

destination_llama="$script_dir/llama-server"
destination_model="$script_dir/models/$EXPECTED_MODEL_OUTPUT_FILE"
destination_contract="$script_dir/BUILD-CONTRACT.txt"

for required_file in \
  "$builder" \
  "$source_llama" \
  "$source_model" \
  "$source_contract"; do

  [[ -f "$required_file" ]] ||
    die "Required file missing: $required_file"
done

echo "===== VERIFY FORMAL BUILD CONTRACT ====="

format="$(contract_value "$source_contract" format)"
builder_sha="$(contract_value "$source_contract" builder_script_sha256)"
llama_repository="$(contract_value "$source_contract" llama_cpp_repository)"
llama_tag="$(contract_value "$source_contract" llama_cpp_tag)"
llama_commit="$(contract_value "$source_contract" llama_cpp_commit)"
patch_sha="$(contract_value "$source_contract" zenz_patch_sha256)"
patched_source_sha="$(
  contract_value "$source_contract" patched_llama_vocab_sha256
)"
model_repository="$(contract_value "$source_contract" model_repository)"
model_revision="$(contract_value "$source_contract" model_revision)"
model_remote_file="$(contract_value "$source_contract" model_remote_file)"
model_output_file="$(contract_value "$source_contract" model_output_file)"
contract_model_sha="$(contract_value "$source_contract" model_sha256)"
minimum_macos="$(contract_value "$source_contract" minimum_macos)"
architectures="$(contract_value "$source_contract" architectures)"
arm_sha="$(contract_value "$source_contract" arm64_thin_sha256)"
x86_sha="$(contract_value "$source_contract" x86_64_thin_sha256)"
universal_sha="$(
  contract_value "$source_contract" universal_unsigned_sha256
)"
signing="$(contract_value "$source_contract" signing)"

[[ "$format" == "$EXPECTED_FORMAT" ]] ||
  die "Unexpected BUILD-CONTRACT format"

[[ "$builder_sha" == "$(sha256_file "$builder")" ]] ||
  die "Runtime was not built by the current tracked builder script"

[[ "$llama_repository" == "$EXPECTED_LLAMA_REPOSITORY" ]] ||
  die "Unexpected llama.cpp repository"

[[ "$llama_tag" == "$EXPECTED_LLAMA_TAG" ]] ||
  die "Unexpected llama.cpp tag"

[[ "$llama_commit" == "$EXPECTED_LLAMA_COMMIT" ]] ||
  die "Unexpected llama.cpp commit"

[[ "$patch_sha" == "$EXPECTED_PATCH_SHA256" ]] ||
  die "Unexpected Zenz patch identity"

[[ "$patched_source_sha" == "$EXPECTED_PATCHED_SOURCE_SHA256" ]] ||
  die "Unexpected patched llama-vocab.cpp identity"

[[ "$model_repository" == "$EXPECTED_MODEL_REPOSITORY" ]] ||
  die "Unexpected model repository"

[[ "$model_revision" == "$EXPECTED_MODEL_REVISION" ]] ||
  die "Unexpected model revision"

[[ "$model_remote_file" == "$EXPECTED_MODEL_REMOTE_FILE" ]] ||
  die "Unexpected model source filename"

[[ "$model_output_file" == "$EXPECTED_MODEL_OUTPUT_FILE" ]] ||
  die "Unexpected model output filename"

[[ "$contract_model_sha" == "$EXPECTED_MODEL_SHA256" ]] ||
  die "Unexpected model SHA in BUILD-CONTRACT"

[[ "$minimum_macos" == "$EXPECTED_MINIMUM_MACOS" ]] ||
  die "Unexpected minimum macOS contract"

[[ "$architectures" == "$EXPECTED_ARCHITECTURES" ]] ||
  die "Unexpected architecture contract"

[[ "$signing" == "downstream_after_lipo" ]] ||
  die "Unexpected signing contract"

echo "Format             = $format"
echo "Builder SHA256     = $builder_sha"
echo "llama.cpp commit   = $llama_commit"
echo "Patch SHA256       = $patch_sha"
echo "Model revision     = $model_revision"
echo "arm64 thin SHA256  = $arm_sha"
echo "x86_64 thin SHA256 = $x86_sha"
echo "Universal SHA256   = $universal_sha"

echo
echo "===== VERIFY SOURCE UNIVERSAL RUNTIME ====="

actual_universal_sha="$(sha256_file "$source_llama")"
actual_model_sha="$(sha256_file "$source_model")"

[[ "$actual_universal_sha" == "$universal_sha" ]] ||
  die "Universal binary does not match BUILD-CONTRACT"

[[ "$actual_model_sha" == "$EXPECTED_MODEL_SHA256" ]] ||
  die "Model SHA-256 mismatch"

archs="$(/usr/bin/lipo -archs "$source_llama")"

print -- "$archs" |
  /usr/bin/grep -qw arm64 ||
  die "Universal runtime has no arm64 slice"

print -- "$archs" |
  /usr/bin/grep -qw x86_64 ||
  die "Universal runtime has no x86_64 slice"

tmp_dir="$(
  /usr/bin/mktemp -d \
    "${TMPDIR:-/tmp}/mozkey-stage-runtime.XXXXXX"
)"

cleanup() {
  /bin/rm -rf "$tmp_dir"
}

trap cleanup EXIT

arm="$tmp_dir/llama-server.arm64"
x86="$tmp_dir/llama-server.x86_64"

/usr/bin/lipo \
  "$source_llama" \
  -thin arm64 \
  -output "$arm"

/usr/bin/lipo \
  "$source_llama" \
  -thin x86_64 \
  -output "$x86"

[[ "$(sha256_file "$arm")" == "$arm_sha" ]] ||
  die "arm64 slice does not match BUILD-CONTRACT"

[[ "$(sha256_file "$x86")" == "$x86_sha" ]] ||
  die "x86_64 slice does not match BUILD-CONTRACT"

for thin in "$arm" "$x86"; do
  [[ "$(extract_minos "$thin")" == "$EXPECTED_MINIMUM_MACOS" ]] ||
    die "Runtime slice deployment target mismatch: $thin"

  check_system_only "$thin" ||
    die "Runtime slice dynamic-library contract failed: $thin"

  check_tokenizer_marker "$thin" ||
    die "Runtime slice tokenizer marker missing: $thin"
done

echo "Source runtime identity = EXACT"
echo "Source model identity   = EXACT"
echo "Source runtime structure = PASS"

echo
echo "===== STAGE FORMAL RUNTIME ASSETS ====="

/bin/mkdir -p "$script_dir/models"

/usr/bin/install \
  -m 0755 \
  "$source_llama" \
  "$destination_llama"

/usr/bin/install \
  -m 0644 \
  "$source_model" \
  "$destination_model"

/usr/bin/install \
  -m 0644 \
  "$source_contract" \
  "$destination_contract"

/usr/bin/xattr -c "$destination_llama" 2>/dev/null || true
/usr/bin/xattr -c "$destination_model" 2>/dev/null || true
/usr/bin/xattr -c "$destination_contract" 2>/dev/null || true

echo
echo "===== VERIFY STAGED IDENTITIES ====="

[[ "$(sha256_file "$destination_llama")" == "$universal_sha" ]] ||
  die "Staged Universal binary mismatch"

[[ "$(sha256_file "$destination_model")" == "$EXPECTED_MODEL_SHA256" ]] ||
  die "Staged model mismatch"

[[ "$(sha256_file "$destination_contract")" == "$(sha256_file "$source_contract")" ]] ||
  die "Staged BUILD-CONTRACT mismatch"

repo_root="$(
  /usr/bin/git \
    -C "$script_dir" \
    rev-parse \
    --show-toplevel \
    2>/dev/null ||
    true
)"

if [[ -n "$repo_root" ]]; then
  for destination in \
    "$destination_llama" \
    "$destination_model" \
    "$destination_contract"; do

    relative="${destination#$repo_root/}"

    /usr/bin/git \
      -C "$repo_root" \
      check-ignore \
      -q \
      "$relative" ||
      die "Staged runtime file is not ignored by Git: $relative"
  done
fi

echo "Git ignore contract = PASS"

echo
echo "===== RESULT ====="
echo "Source root            = $source_root"
echo "llama.cpp commit       = $llama_commit"
echo "Builder SHA256         = $builder_sha"
echo "Universal SHA256       = $universal_sha"
echo "arm64 thin SHA256      = $arm_sha"
echo "x86_64 thin SHA256     = $x86_sha"
echo "Model SHA256           = $actual_model_sha"
echo "Architectures          = arm64 + x86_64"
echo "Minimum macOS          = $EXPECTED_MINIMUM_MACOS"
echo "Dynamic libraries      = SYSTEM ONLY"
echo "Tokenizer marker       = PRESENT BOTH SLICES"
echo "BUILD-CONTRACT staged  = YES"
echo
echo "Formal macOS Zenz Universal runtime assets staged successfully."
