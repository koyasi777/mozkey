#!/usr/bin/env python3
"""Fail-closed, read-only Mozkey Windows release source identity checks.

No remote API calls, source writes, or dependency installation.
This is not a cryptographic provenance proof, and not a release approval.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

EXPECTED_ORIGIN = re.compile(
    r"^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)"
    r"koyasi777/mozkey(?:\.git)?/?$", re.I
)
EXPECTED_UPGRADE_CODE = "DD94B570-B5E2-4100-9D42-61930C611D8A"
VERSION_KEYS = (
    "MOZKEY_RELEASE_VERSION_MAJOR",
    "MOZKEY_RELEASE_VERSION_MINOR",
    "MOZKEY_RELEASE_VERSION_PATCH",
)


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def load_release_parts(version_file: pathlib.Path) -> tuple[int, int, int]:
    contents = version_file.read_text(encoding="utf-8-sig")
    parts: list[int] = []
    for key in VERSION_KEYS:
        vals = re.findall(rf"^{re.escape(key)}[ \t]*=[ \t]*(\d+)[ \t]*$", contents, re.M)
        require(len(vals) == 1, f"Expected exactly one simple {key} assignment")
        require(str(int(vals[0])) == vals[0], f"Noncanonical value for {key}")
        parts.append(int(vals[0]))
    major, minor, patch = parts
    require(0 <= major <= 155 and 0 <= minor <= 255 and 0 <= patch <= 65535,
            "Release version exceeds Mozkey MSI version limits")
    return tuple(parts)  # type: ignore[return-value]


def parse_version(value: str) -> tuple[int, int, int]:
    require(bool(re.fullmatch(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", value)),
            "Expected canonical X.Y.Z version, with no v prefix or leading zeros")
    return tuple(int(n) for n in value.split("."))  # type: ignore[return-value]


def check_contract(repo: pathlib.Path, requested_version: str) -> dict:
    parts = load_release_parts(repo / "src/version.bzl")
    expected = parse_version(requested_version)
    require(parts == expected, f"Source version {parts} differs from requested {expected}")
    msi_version = f"{100 + parts[0]}.{parts[1]}.{parts[2]}"

    config = (repo / "src/config.bzl").read_text(encoding="utf-8-sig")
    require(bool(re.search(r'^BRANDING\s*=\s*"Mozc"\s*$', config, re.M)),
            "Installer Bazel branding has changed; review before release")
    build = (repo / "src/win32/installer/BUILD.bazel").read_text(encoding="utf-8-sig")
    require('_MSI_FILE = "Mozc64.msi" if BRANDING == "Mozc"' in build,
            "Installer output contract changed")
    require('"//win32/installer/zenz_runtime:llama-server.exe"' in build,
            "Installer no longer declares llama-server input")
    require('"//win32/installer/zenz_runtime:models/zenz-v3.2-small-Q5_K_M.gguf"' in build,
            "Installer no longer declares Zenz model input")

    wxs = (repo / "src/win32/installer/installer_oss_64bit.wxs").read_text(encoding="utf-8-sig")
    require('<Package Name="Mozkey"' in wxs and 'Manufacturer="koyasi777"' in wxs,
            "WiX product identity changed")
    install_script = (repo / "src/win32/installer/build_installer.py").read_text(encoding="utf-8-sig")
    require(EXPECTED_UPGRADE_CODE in install_script,
            "WiX UpgradeCode source identity changed")
    require("version.GetMozkeyMsiVersion()" in install_script,
            "WiX installer no longer uses Mozkey MSI version mapping")
    lock = json.loads((repo / "src/zenz_runtime/llama_runtime.lock.json").read_text(encoding="utf-8-sig"))
    require(bool(re.fullmatch(r"[0-9a-fA-F]{64}", lock["model"]["sha256"])),
            "Missing pinned model hash")
    for arch in ("x64", "arm64"):
        require(bool(re.fullmatch(r"[0-9a-fA-F]{64}", lock["windows"][arch]["sha256"])),
                f"Missing pinned Windows {arch} llama-server hash")
    require(lock["model"]["windows_file"] ==
            "src/win32/installer/zenz_runtime/models/zenz-v3.2-small-Q5_K_M.gguf",
            "Pinned model path changed")
    for arch in ("x64", "arm64"):
        require(lock["windows"][arch]["path"] ==
                f"src/win32/installer/zenz_runtime/{arch}/llama-server.exe",
                f"Pinned {arch} runtime path changed")
        require(lock["windows"][arch].get("pe_machine", "").lower() ==
                {"x64": "0x8664", "arm64": "0xaa64"}[arch],
                f"Pinned {arch} PE architecture changed")
    return {
        "release_version": requested_version,
        "expected_msi_product_version": msi_version,
        "product_name": "Mozkey",
        "manufacturer": "koyasi777",
        "upgrade_code": EXPECTED_UPGRADE_CODE,
        "model_sha256": lock["model"]["sha256"].upper(),
        "llama_x64_sha256": lock["windows"]["x64"]["sha256"].upper(),
        "llama_arm64_sha256": lock["windows"]["arm64"]["sha256"].upper(),
    }


def git_run(repo: pathlib.Path, *args: str) -> str:
    p = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", check=False)
    require(p.returncode == 0, f"git {' '.join(args)} failed: {p.stderr.strip()}")
    return p.stdout.strip()


def check_git(repo: pathlib.Path, expected_sha: str) -> dict:
    require(bool(re.fullmatch(r"[0-9a-fA-F]{40}", expected_sha)), "Invalid expected SHA")
    origin = git_run(repo, "remote", "get-url", "origin")
    require(bool(EXPECTED_ORIGIN.fullmatch(origin)), f"Unexpected Git remote: {origin}")
    sha = git_run(repo, "rev-parse", "HEAD")
    require(sha.lower() == expected_sha.lower(), "Git checkout does not match workflow source SHA")
    # Only tracked changes: untracked/generated dictionary inputs will be checked separately.
    require(not git_run(repo, "status", "--porcelain", "--untracked-files=no"),
            "Tracked source modifications detected before release build")
    return {"source_sha": sha, "remote": origin}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--repo", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
    p.add_argument("--version", required=True)
    p.add_argument("--expected-sha", required=True)
    p.add_argument("--output", type=pathlib.Path)
    args = p.parse_args(argv)
    try:
        repo = args.repo.resolve(strict=True)
        info = check_contract(repo, args.version)
        info.update(check_git(repo, args.expected_sha))
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("MOZKEY_RELEASE_SOURCE_CONTRACT=PASS")
        print(f"SourceSHA={info['source_sha']}")
        print(f"ReleaseVersion={info['release_version']}")
        print(f"MsiProductVersion={info['expected_msi_product_version']}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"MOZKEY_RELEASE_SOURCE_CONTRACT=FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
