#!/bin/zsh

set -eu
setopt pipe_fail

export PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:${PATH:-}"

expected_llama_hash="e24e6b0928bd06fdefe68ea7c013acff1e5067a9318cf619c8b50e4712f7f931"
expected_model_hash="29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673"

die() {
  print -u2 -- "ERROR: $*"
  exit 1
}

sha256() {
  /usr/bin/shasum -a 256 "$1" |
    /usr/bin/awk '{print $1}'
}

script_dir="${0:A:h}"
source_root="${1:-${MOZKEY_ZENZ_RUNTIME_SOURCE:-}}"

if [[ -z "$source_root" ]]; then
  print -u2 -- "Usage:"
  print -u2 -- "  $0 /path/to/mozkey-macos-zenz-runtime"
  print -u2 -- ""
  print -u2 -- "Alternatively set MOZKEY_ZENZ_RUNTIME_SOURCE."
  exit 2
fi

source_root="${source_root:A}"

source_llama="$source_root/llama-server"
source_model="$source_root/models/zenz-v3.2-small-Q5_K_M.gguf"

destination_llama="$script_dir/llama-server"
destination_model="$script_dir/models/zenz-v3.2-small-Q5_K_M.gguf"

required_tools=(
  /usr/bin/shasum
  /usr/bin/awk
  /usr/bin/install
  /usr/bin/file
  /usr/bin/lipo
  /usr/bin/otool
  /usr/bin/grep
  /usr/bin/stat
  /usr/bin/xattr
  /usr/bin/git
  /bin/mkdir
)

echo "===== TOOL CHECK ====="

for tool_file in "${required_tools[@]}"; do
  [[ -x "$tool_file" ]] ||
    die "Missing or non-executable tool: $tool_file"

  echo "$tool_file"
done

echo "===== SOURCE FILE CHECK ====="

[[ -f "$source_llama" ]] ||
  die "Missing llama-server: $source_llama"

[[ -f "$source_model" ]] ||
  die "Missing model: $source_model"

/usr/bin/stat -f '%Sp %z %N' \
  "$source_llama" \
  "$source_model"

echo "===== SOURCE HASH CHECK ====="

actual_llama_hash="$(sha256 "$source_llama")"
actual_model_hash="$(sha256 "$source_model")"

echo "llama-server"
echo "  Actual   = $actual_llama_hash"
echo "  Expected = $expected_llama_hash"

echo "model"
echo "  Actual   = $actual_model_hash"
echo "  Expected = $expected_model_hash"

[[ "$actual_llama_hash" == "$expected_llama_hash" ]] ||
  die "llama-server SHA-256 mismatch"

[[ "$actual_model_hash" == "$expected_model_hash" ]] ||
  die "model SHA-256 mismatch"

echo "===== LLAMA BINARY AUDIT ====="

/usr/bin/file "$source_llama"

architectures="$(/usr/bin/lipo -archs "$source_llama")"

echo "Architectures = $architectures"

[[ "$architectures" == "arm64" ]] ||
  die "llama-server must be arm64"

minimum_macos="$(
  /usr/bin/otool -l "$source_llama" |
    /usr/bin/awk '
      /LC_BUILD_VERSION/ {
        in_build = 1
        next
      }

      in_build && $1 == "minos" {
        print $2
        exit
      }
    '
)"

echo "Minimum macOS = $minimum_macos"

[[ "$minimum_macos" == "12.0" ]] ||
  die "Unexpected deployment target: $minimum_macos"

unexpected_dylibs="$(
  /usr/bin/otool -L "$source_llama" |
    /usr/bin/awk 'NR > 1 {print $1}' |
    /usr/bin/grep -Ev '^(/System/Library/|/usr/lib/)' ||
    true
)"

if [[ -n "$unexpected_dylibs" ]]; then
  print -u2 -- "Unexpected non-system dynamic libraries:"
  print -u2 -- "$unexpected_dylibs"
  exit 1
fi

echo "Dynamic libraries = system libraries only"

echo "===== STAGE RUNTIME ASSETS ====="

/bin/mkdir -p "$script_dir/models"

/usr/bin/install \
  -m 0755 \
  "$source_llama" \
  "$destination_llama"

/usr/bin/install \
  -m 0644 \
  "$source_model" \
  "$destination_model"

/usr/bin/xattr -c "$destination_llama" 2>/dev/null || true
/usr/bin/xattr -c "$destination_model" 2>/dev/null || true

echo "===== DESTINATION VERIFICATION ====="

destination_llama_hash="$(sha256 "$destination_llama")"
destination_model_hash="$(sha256 "$destination_model")"

[[ "$destination_llama_hash" == "$expected_llama_hash" ]] ||
  die "Staged llama-server SHA-256 mismatch"

[[ "$destination_model_hash" == "$expected_model_hash" ]] ||
  die "Staged model SHA-256 mismatch"

/usr/bin/stat -f '%Sp %z %N' \
  "$destination_llama" \
  "$destination_model"

repo_root="$(
  /usr/bin/git \
    -C "$script_dir" \
    rev-parse \
    --show-toplevel \
    2>/dev/null ||
    true
)"

if [[ -n "$repo_root" ]]; then
  llama_relative="${destination_llama#$repo_root/}"
  model_relative="${destination_model#$repo_root/}"

  /usr/bin/git \
    -C "$repo_root" \
    check-ignore \
    -q \
    "$llama_relative" ||
    die "Staged llama-server is not ignored by Git"

  /usr/bin/git \
    -C "$repo_root" \
    check-ignore \
    -q \
    "$model_relative" ||
    die "Staged model is not ignored by Git"

  echo "Git ignore check = passed"
fi

echo "===== RESULT ====="

echo "llama-server SHA256 = $destination_llama_hash"
echo "model SHA256        = $destination_model_hash"
echo
echo "macOS Zenz runtime assets staged successfully"
