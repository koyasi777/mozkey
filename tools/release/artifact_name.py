#!/usr/bin/env python3
"""Canonical public Mozkey release filenames; independent of build output names.

Only release-approved OS / architecture / format combinations are accepted.
Linux targets can be added here when their package contracts are finalized.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import re


@dataclass(frozen=True)
class ReleaseTarget:
    os: str
    arch: str
    format: str


SUPPORTED_TARGETS = {
    ReleaseTarget("windows", "x64", "msi"): ".msi",
    ReleaseTarget("windows", "arm64", "msi"): ".msi",
    ReleaseTarget("macos", "universal", "pkg"): ".pkg",
}

_VERSION = re.compile(
    r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)",
    re.ASCII,
)


def artifact_name(version: str, target: ReleaseTarget) -> str:
    """Return a safe filename, or raise ValueError for an unsupported contract."""
    if len(version) > 32 or _VERSION.fullmatch(version) is None:
        raise ValueError(f"Noncanonical release version: {version!r}")
    try:
        extension = SUPPORTED_TARGETS[target]
    except KeyError:
        raise ValueError(f"Unsupported release target: {target!r}") from None
    return f"Mozkey_v{version}_{target.os}_{target.arch}{extension}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--os", required=True)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--format", required=True)
    args = parser.parse_args()
    try:
        print(artifact_name(args.version, ReleaseTarget(args.os, args.arch, args.format)))
    except ValueError as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
