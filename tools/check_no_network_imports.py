#!/usr/bin/env python3
# Copyright 2026
#
# Fails if Mozc Windows runtime binaries import networking-related DLLs.
#
# This script does not require llvm-objdump or dumpbin. It parses the PE import
# table directly with Python.
#
# Usage:
#   python tools/check_no_network_imports.py --root src/bazel-bin
#   python tools/check_no_network_imports.py path/to/mozc_server.exe ...

from __future__ import annotations

import argparse
from pathlib import Path
import re
import struct
import sys


FORBIDDEN_DLLS = {
    "ws2_32.dll",
    "mswsock.dll",
    "winhttp.dll",
    "wininet.dll",
    "urlmon.dll",
    "websocket.dll",
    "webio.dll",
    "dnsapi.dll",
    "iphlpapi.dll",
    "rasapi32.dll",
    "rasman.dll",
    "rtutils.dll",
}

ALLOWED_FORBIDDEN_DLLS_BY_BINARY = {
    # mozc_zenz_scorer.exe uses WinHTTP only to talk to the bundled
    # llama-server.exe through a localhost endpoint for local Zenz inference.
    # This is not telemetry, updater, crash upload, or external network access.
    "mozc_zenz_scorer.exe": {
        "winhttp.dll",
    },
}

RUNTIME_BINARY_PATTERN = re.compile(r"^mozc.*\.(exe|dll)$", re.IGNORECASE)


class PEFormatError(Exception):
    pass


def read_u16(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        raise PEFormatError(f"u16 read out of range: {offset}")
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise PEFormatError(f"u32 read out of range: {offset}")
    return struct.unpack_from("<I", data, offset)[0]


def read_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise PEFormatError(f"string offset out of range: {offset}")

    end = data.find(b"\x00", offset)
    if end < 0:
        raise PEFormatError(f"unterminated string at offset: {offset}")

    return data[offset:end].decode("ascii", errors="ignore")


def parse_sections(
    data: bytes,
    section_table_offset: int,
    number_of_sections: int,
) -> list[dict[str, int]]:
    sections: list[dict[str, int]] = []

    for i in range(number_of_sections):
        offset = section_table_offset + i * 40
        if offset + 40 > len(data):
            raise PEFormatError("section table is truncated")

        virtual_size = read_u32(data, offset + 8)
        virtual_address = read_u32(data, offset + 12)
        size_of_raw_data = read_u32(data, offset + 16)
        pointer_to_raw_data = read_u32(data, offset + 20)

        sections.append(
            {
                "virtual_size": virtual_size,
                "virtual_address": virtual_address,
                "size_of_raw_data": size_of_raw_data,
                "pointer_to_raw_data": pointer_to_raw_data,
            }
        )

    return sections


def rva_to_file_offset(rva: int, sections: list[dict[str, int]]) -> int:
    for section in sections:
        va = section["virtual_address"]
        raw_size = section["size_of_raw_data"]
        virtual_size = section["virtual_size"]
        span = max(raw_size, virtual_size)

        if va <= rva < va + span:
            return section["pointer_to_raw_data"] + (rva - va)

    raise PEFormatError(f"RVA not found in sections: 0x{rva:x}")


def read_imported_dlls(path: Path) -> set[str]:
    data = path.read_bytes()

    if len(data) < 0x40:
        raise PEFormatError("file is too small")

    if data[0:2] != b"MZ":
        raise PEFormatError("missing MZ header")

    pe_offset = read_u32(data, 0x3C)

    if pe_offset + 4 > len(data):
        raise PEFormatError("PE header offset out of range")

    if data[pe_offset:pe_offset + 4] != b"PE\x00\x00":
        raise PEFormatError("missing PE signature")

    coff_offset = pe_offset + 4
    number_of_sections = read_u16(data, coff_offset + 2)
    size_of_optional_header = read_u16(data, coff_offset + 16)

    optional_header_offset = coff_offset + 20
    optional_header_end = optional_header_offset + size_of_optional_header

    if optional_header_end > len(data):
        raise PEFormatError("optional header is truncated")

    magic = read_u16(data, optional_header_offset)

    if magic == 0x10B:
        # PE32
        data_directory_offset = optional_header_offset + 96
    elif magic == 0x20B:
        # PE32+
        data_directory_offset = optional_header_offset + 112
    else:
        raise PEFormatError(f"unknown optional header magic: 0x{magic:x}")

    # IMAGE_DIRECTORY_ENTRY_IMPORT is index 1.
    import_directory_entry_offset = data_directory_offset + 8
    import_rva = read_u32(data, import_directory_entry_offset)
    import_size = read_u32(data, import_directory_entry_offset + 4)

    if import_rva == 0 or import_size == 0:
        return set()

    section_table_offset = optional_header_end
    sections = parse_sections(data, section_table_offset, number_of_sections)

    import_offset = rva_to_file_offset(import_rva, sections)

    imported: set[str] = set()

    # IMAGE_IMPORT_DESCRIPTOR is 20 bytes.
    descriptor_size = 20

    for i in range(0, import_size, descriptor_size):
        descriptor_offset = import_offset + i

        if descriptor_offset + descriptor_size > len(data):
            break

        original_first_thunk = read_u32(data, descriptor_offset)
        time_date_stamp = read_u32(data, descriptor_offset + 4)
        forwarder_chain = read_u32(data, descriptor_offset + 8)
        name_rva = read_u32(data, descriptor_offset + 12)
        first_thunk = read_u32(data, descriptor_offset + 16)

        if (
            original_first_thunk == 0
            and time_date_stamp == 0
            and forwarder_chain == 0
            and name_rva == 0
            and first_thunk == 0
        ):
            break

        if name_rva == 0:
            continue

        name_offset = rva_to_file_offset(name_rva, sections)
        imported.add(read_c_string(data, name_offset).lower())

    return imported


def collect_targets(root: Path, explicit_paths: list[Path]) -> list[Path]:
    if explicit_paths:
        return explicit_paths

    if not root.exists():
        return []

    targets: list[Path] = []

    for path in root.rglob("*"):
        if not path.is_file():
            continue

        # Skip Bazel runfiles duplicates such as:
        #   mozc_tool.exe.runfiles\_main\gui\tool\mozc_tool.exe
        if any(part.endswith(".runfiles") for part in path.parts):
            continue

        if not RUNTIME_BINARY_PATTERN.match(path.name):
            continue

        targets.append(path)

    return sorted(targets)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="Explicit .exe/.dll files to inspect.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("src") / "bazel-bin",
        help="Root directory to recursively scan when no explicit paths are given.",
    )
    args = parser.parse_args(argv[1:])

    targets = collect_targets(args.root, args.paths)

    if not targets:
        print(f"[NO_NETWORK_IMPORT_CHECK_SKIPPED] No targets found under {args.root}")
        return 0

    failed = False

    for path in targets:
        if not path.exists():
            print(f"[NO_NETWORK_IMPORT_MISSING] {path}", file=sys.stderr)
            failed = True
            continue

        try:
            imported = read_imported_dlls(path)
        except PEFormatError as e:
            print(f"[NO_NETWORK_IMPORT_ERROR] {path}: {e}", file=sys.stderr)
            failed = True
            continue
        except OSError as e:
            print(f"[NO_NETWORK_IMPORT_ERROR] {path}: {e}", file=sys.stderr)
            failed = True
            continue

        allowed_forbidden = ALLOWED_FORBIDDEN_DLLS_BY_BINARY.get(path.name.lower(), set())
        forbidden = sorted((imported & FORBIDDEN_DLLS) - allowed_forbidden)

        if forbidden:
            failed = True
            print(f"[NO_NETWORK_IMPORT_VIOLATION] {path}", file=sys.stderr)
            for dll in forbidden:
                print(f"  - {dll}", file=sys.stderr)
        else:
            print(f"[NO_NETWORK_IMPORT_OK] {path}")

    if failed:
        print("[NO_NETWORK_IMPORT_CHECK_FAILED]")
        return 1

    print("[NO_NETWORK_IMPORT_CHECK_PASSED]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
