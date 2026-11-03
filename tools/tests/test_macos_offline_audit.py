#!/usr/bin/env python3
"""Pure-Python regression tests. Runs on Linux and macOS without Mozc build."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_macos_offline_source as source_gate
import macos_offline_socket_audit as sockets


class SocketParsingTest(unittest.TestCase):
    def test_process_tree_only_descendants(self):
        sample = (
            "11 1 /Applications/Mozc.app/Mozc\n"
            "80 11 /tmp/mozc_zenz_scorer\n"
            "81 80 /tmp/mozc_zenz_guard\n"
            "82 81 /tmp/llama-server\n"
            "83 1 /usr/bin/curl\n"
        )
        self.assertEqual(set(sockets.parse_process_tree(sample, 80)), {80, 81, 82})
        self.assertNotIn(83, sockets.parse_process_tree(sample, 80))

    def test_lsof_parsing(self):
        sample = (
            "p80\ncmozc_zenz_scorer\nf3\nn127.0.0.1:50100\n"
            "p82\ncllama-server\nf9\nn127.0.0.1:50100->127.0.0.1:50102\n"
        )
        self.assertEqual(sockets.parse_lsof_names(sample), [
            (80, "127.0.0.1:50100"),
            (82, "127.0.0.1:50100->127.0.0.1:50102"),
        ])

    def test_strict_loopback(self):
        for name in ("127.0.0.1:5090", "127.0.0.1:5090->127.0.0.1:5091",
                     "[::1]:5000", "[::1]:5000->[::1]:5001"):
            with self.subTest(name=name):
                self.assertIsNone(sockets.validate_loopback_name(name))

    def test_external_and_wildcard_fail_closed(self):
        for name in (
            "0.0.0.0:8080", "*:8080", "127.0.0.1:9000->8.8.8.8:443",
            "192.168.0.1:9000", "[::]:8080", "[::ffff:127.0.0.1]:8080",
            "not-an-ip", "127.0.0.1:80->example.com:443",
        ):
            with self.subTest(name=name):
                self.assertIsNotNone(sockets.validate_loopback_name(name))


class SourceGateTest(unittest.TestCase):
    def test_test_sources_not_considered_runtime(self):
        self.assertFalse(source_gate.is_runtime_file(Path("src/mac/foo_test.mm")))
        self.assertFalse(source_gate.is_runtime_file(Path("src/zenz_scorer/foo_test_helper.cc")))
        self.assertTrue(source_gate.is_runtime_file(Path("src/mac/foo.mm")))

    def test_windows_entrypoint_excluded_but_macos_entrypoint_scanned(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for folder in source_gate.ROOTS:
                (root / folder).mkdir(parents=True, exist_ok=True)
            windows = root / "src/zenz_scorer/main.cc"
            macos = root / "src/zenz_scorer/main_posix.cc"
            windows.write_text("::socket(AF_INET, SOCK_STREAM, 0);\n", encoding="utf-8")
            macos.write_text("::socket(AF_INET, SOCK_STREAM, 0);\n", encoding="utf-8")
            violations, _ = source_gate.check_source(root)
            self.assertFalse(any("src/zenz_scorer/main.cc:" in item for item in violations))
            self.assertTrue(any("src/zenz_scorer/main_posix.cc:" in item for item in violations))

    def test_platform_mapping_change_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for folder in source_gate.ROOTS:
                (root / folder).mkdir(parents=True, exist_ok=True)
            build = root / source_gate.ENTRYPOINT_BUILD
            build.write_text('''mozc_cc_binary(
    name = "mozc_zenz_scorer",
    srcs = select({
        "@platforms//os:macos": ["main_posix.cc"],
        "@platforms//os:windows": ["main.cc", "header.h"],
    }),
)
''', encoding="utf-8")
            violations, _ = source_gate.check_source(root)
            self.assertFalse(any("entrypoint contract missing" in v for v in violations))
            build.write_text(build.read_text(encoding="utf-8").replace(
                '"@platforms//os:macos": ["main_posix.cc"]',
                '"@platforms//os:macos": ["main.cc"]'), encoding="utf-8")
            violations, _ = source_gate.check_source(root)
            self.assertTrue(any("entrypoint contract missing" in v for v in violations))

    def test_contract_breaks_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for folder in source_gate.ROOTS:
                (root / folder).mkdir(parents=True, exist_ok=True)
            for rel, required in source_gate.REQUIRED.items():
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                # Test required-contract checks independently of the scan.
                path.write_text("// stub\n", encoding="utf-8")
            bad, count = source_gate.check_source(root)
            self.assertGreater(count, 0)
            self.assertTrue(any("security contract missing" in s for s in bad))
            extra = root / "src/mac/accidental_network.mm"
            extra.write_text("NSURLSession * session;\n", encoding="utf-8")
            bad, _ = source_gate.check_source(root)
            self.assertTrue(any("forbidden network session" in s for s in bad))
            extra.write_text("::socket(AF_INET, SOCK_STREAM, 0);\n", encoding="utf-8")
            bad, _ = source_gate.check_source(root)
            self.assertTrue(any("outside reviewed loopback boundary" in s for s in bad))


if __name__ == "__main__":
    unittest.main(verbosity=2)
