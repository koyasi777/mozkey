#!/usr/bin/env python3
"""Record hashes of generated dictionary inputs for unsigned release rehearsal.

This is an inventory, not verification of remote provenance or hermeticity.
Writes only the explicitly selected evidence file, never the source tree.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

# Match current windows.yaml dictionary artifact inputs. Explicit roots, no globbing
# across credentials, build outputs or the whole repository.
ROOTS = (
    "src/data/dictionary_koyasi/generated",
    "dist/dictionary",
)


def digest(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest().upper()


def collect(repo: pathlib.Path) -> dict:
    repo = repo.resolve(strict=True)
    result = []
    for name in ROOTS:
        folder = repo / name
        if not folder.is_dir() or folder.is_symlink():
            raise ValueError(f"Required generated dictionary directory absent: {name}")
        for file in sorted(folder.rglob("*")):
            if file.is_symlink():
                raise ValueError(f"Generated dictionary contains symlink: {file}")
            if not file.is_file():
                continue
            if not file.resolve().is_relative_to(repo):
                raise ValueError(f"Generated dictionary escapes repository: {file}")
            result.append({
                "path": file.relative_to(repo).as_posix(),
                "bytes": file.stat().st_size,
                "sha256": digest(file),
            })
    if not result:
        raise ValueError("No generated dictionary files were produced")
    changes = subprocess.run(
        ["git", "-C", str(repo), "diff", "--name-only", "--no-ext-diff", "HEAD", "--"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
    )
    if changes.returncode != 0:
        raise ValueError(f"git diff failed: {changes.stderr}")
    return {
        "schema_version": 1,
        "status": "EVIDENCE_ONLY_NOT_RELEASE_APPROVAL",
        "upstream_input_provenance_verified": False,
        "dictionary_file_count": len(result),
        "generated_files": result,
        "tracked_files_changed_since_checkout": sorted(x for x in changes.stdout.splitlines() if x),
    }


def verify_inventory(actual: dict, reference: dict) -> dict:
    """Fail closed on missing, changed, or extra generated dictionary files.

    This proves integrity across jobs in one workflow run, not the authenticity
    of third-party source inputs or determinism of dictionary generation.
    """
    if reference.get("schema_version") != 1 or reference.get("status") != "EVIDENCE_ONLY_NOT_RELEASE_APPROVAL":
        raise ValueError("Unexpected dictionary provenance schema or status")
    expected = reference.get("generated_files")
    if not isinstance(expected, list) or not expected:
        raise ValueError("Missing reference dictionary file inventory")
    if reference.get("dictionary_file_count") != len(expected):
        raise ValueError("Reference dictionary count mismatch")
    if actual["generated_files"] != expected:
        actual_files = {row["path"]: row for row in actual["generated_files"]}
        expected_files = {row["path"]: row for row in expected}
        missing = sorted(set(expected_files) - set(actual_files))
        extra = sorted(set(actual_files) - set(expected_files))
        changed = sorted(
            k for k in (set(actual_files) & set(expected_files))
            if actual_files[k] != expected_files[k]
        )
        raise ValueError(
            f"Dictionary artifact differs from provenance: missing={missing[:8]}, "
            f"extra={extra[:8]}, changed={changed[:8]}"
        )
    return {
        "schema_version": 1,
        "status": "DICTIONARY_INPUT_MATCH_NOT_RELEASE_APPROVAL",
        "generated_file_count": actual["dictionary_file_count"],
        "generated_files_match_reference": True,
        "upstream_input_provenance_verified": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--verify-against", type=pathlib.Path,
                        help="Verify exact generated file names, lengths and SHA-256 against a prior run inventory")
    args = parser.parse_args()
    try:
        result = collect(args.repo)
        if args.verify_against is not None:
            reference = json.loads(args.verify_against.read_text(encoding="utf-8-sig"))
            result = verify_inventory(result, reference)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if args.verify_against is not None:
            print("MOZKEY_DICTIONARY_INPUT_VERIFICATION=PASS")
            print(f"GeneratedFiles={result['generated_file_count']}")
        else:
            print("MOZKEY_DICTIONARY_PROVENANCE_INVENTORY=PASS")
            print(f"GeneratedFiles={result['dictionary_file_count']}")
        print("UpstreamProvenanceVerified=NO (rehearsal only)")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"MOZKEY_DICTIONARY_PROVENANCE_INVENTORY=FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
