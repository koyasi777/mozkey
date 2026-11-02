#!/usr/bin/env python3
"""Conservative macOS runtime network-source gate for Mozkey.

This is a regression gate, not a proof that third-party / linked code cannot
connect to the Internet. Build-time utilities are intentionally not scanned.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOTS = (
    "src/mac",
    "src/renderer/mac",
    "src/zenz_scorer",
    "src/zenz",
    "src/session",
    "src/config",
    "src/base/mac",
)
EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".m", ".mm"}
# These two files use AF_INET only for a random IPv4 loopback listener and its
# authenticated local HTTP client. No other macOS Mozkey runtime source may
# introduce IP socket creation without changing/reviewing this policy.
INET_ALLOWED = {
    "src/zenz_scorer/llama_server_process.cc",
    "src/zenz_scorer/llama_http_client.cc",
}
# main.cc is the Windows-only entry point. macOS uses main_posix.cc.
# The Bazel source mapping below is checked fail-closed to prevent this explicit
# exclusion from silently becoming unsafe after a platform mapping change.
WINDOWS_ONLY_SOURCES = {"src/zenz_scorer/main.cc"}
ENTRYPOINT_BUILD = "src/zenz_scorer/BUILD.bazel"
ENTRYPOINT_CONTRACT = re.compile(
    r'mozc_cc_binary\(\s*name\s*=\s*"mozc_zenz_scorer"\s*,\s*'
    r'srcs\s*=\s*select\(\{\s*'
    r'"@platforms//os:macos"\s*:\s*\[\s*"main_posix\.cc"\s*\]\s*,\s*'
    r'"@platforms//os:windows"\s*:\s*\[\s*"main\.cc"\s*,',
    re.MULTILINE,
)
# Disallow direct network client APIs. Core/renderer communication uses local
# IPC. Generic NSURL / CFURL strings are NOT prohibited: apps may provide
# user-clicked documentation/help URLs, which are not IME text exfiltration.
FORBIDDEN = {
    "network session": re.compile(
        r"\b(?:NSURLSession|NSURLConnection|NSURLDownload|NSURLRequest|"
        r"CFReadStreamCreateForHTTPRequest|CFHTTPMessageCreateRequest|"
        r"QNetworkAccessManager|QNetworkRequest|QTcpSocket|QUdpSocket|"
        r"QWebSocket|curl_easy_init|curl_easy_perform|"
        r"getaddrinfo|gethostbyname)\b"
    ),
    "nonloopback binding": re.compile(
        r"\b(?:INADDR_ANY|INADDR_BROADCAST|IN6ADDR_ANY_INIT|"
        r"AF_INET6|AI_PASSIVE)\b"
    ),
}
REQUIRED = {
    "src/zenz_scorer/llama_server_process.cc": (
        ("loopback listener allocation", r"sin_addr\.s_addr\s*=\s*htonl\(INADDR_LOOPBACK\)"),
        ("llama host option", r'arguments\.push_back\("--host"\);\s*arguments\.push_back\("127\.0\.0\.1"\);'),
        ("fixed API key option", r'arguments\.push_back\("--api-key"\);'),
        ("no host/port option overrides", r'network_endpoint_override_rejected'),
    ),
    "src/zenz_scorer/llama_http_client.cc": (
        ("loopback client", r"sin_addr\.s_addr\s*=\s*htonl\(INADDR_LOOPBACK\)"),
        ("HTTP request loopback host", r'Host: 127\.0\.0\.1:'),
    ),
    "src/session/zenz_client_factory.cc": (
        ("mac Unix socket client", r'std::make_unique<ZenzUnixSocketClient>'),
        ("mac socket path", r'GetZenzUnixSocketPath\('),
    ),
    "src/zenz_scorer/posix_server.cc": (
        ("Unix socket server", r'::socket\(AF_UNIX,\s*SOCK_STREAM'),
        ("private socket file", r'::chmod\(socket_path_\.c_str\(\),\s*0600\)'),
    ),
    "src/config/stats_config_util.cc": (
        ("null statistics implementation", r'typedef\s+NullStatsConfigUtilImpl\s+DefaultConfigUtilImpl\s*;'),
    ),
    "src/mac/mozc_imk_input_controller.mm": (
        ("mac secure event input", r'IsSecureEventInputEnabled\(\)'),
        ("mac password context", r'context\.set_input_field_type\(mozc::commands::Context::PASSWORD\)'),
    ),
}


def is_runtime_file(path: Path) -> bool:
    return (
        path.suffix in EXTENSIONS
        and not path.stem.endswith("_test")
        and "testdata" not in path.parts
        and not path.stem.endswith("_test_helper")
        and "testing" not in path.parts
    )


def check_source(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    files = 0
    for directory in ROOTS:
        base = root / directory
        if not base.is_dir():
            violations.append(f"missing source directory: {directory}")
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or not is_runtime_file(path):
                continue
            rel = path.relative_to(root).as_posix()
            if rel in WINDOWS_ONLY_SOURCES:
                continue
            files += 1
            source = path.read_text(encoding="utf-8", errors="replace")
            for label, rule in FORBIDDEN.items():
                for match in rule.finditer(source):
                    line = source.count("\n", 0, match.start()) + 1
                    violations.append(f"{rel}:{line}: forbidden {label}: {match.group(0)}")
            if rel not in INET_ALLOWED:
                for match in re.finditer(r"\b(?:AF_INET|sockaddr_in)\b", source):
                    line = source.count("\n", 0, match.start()) + 1
                    violations.append(f"{rel}:{line}: IP networking outside reviewed loopback boundary")
    # Fail closed if Bazel ever changes which source implements the macOS
    # scorer. This is tied to the one explicit Windows-only exclusion above.
    build = root / ENTRYPOINT_BUILD
    if not build.is_file() or ENTRYPOINT_CONTRACT.search(
        build.read_text(encoding="utf-8", errors="replace")
    ) is None:
        violations.append(
            f"{ENTRYPOINT_BUILD}: macOS/Windows scorer entrypoint contract missing"
        )
    if files == 0:
        violations.append("no runtime sources inspected")
    for rel, checks in REQUIRED.items():
        file = root / rel
        if not file.is_file():
            violations.append(f"missing critical source: {rel}")
            continue
        source = file.read_text(encoding="utf-8", errors="replace")
        for description, pattern in checks:
            if not re.search(pattern, source, flags=re.MULTILINE):
                violations.append(f"{rel}: security contract missing: {description}")
    return violations, files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    problems, count = check_source(args.root.resolve())
    print(f"MACOS_OFFLINE_SOURCE_FILES={count}")
    for line in problems:
        print(f"MACOS_OFFLINE_SOURCE_VIOLATION={line}")
    print("MACOS_OFFLINE_SOURCE_AUDIT=" + ("FAIL" if problems else "PASS"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
