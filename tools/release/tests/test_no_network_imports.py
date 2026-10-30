"""Regression guards for the narrow Windows Zenz scorer loopback DLL exception."""
from __future__ import annotations

import pathlib
import sys
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[2]
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import check_no_network_imports as policy


class ZenzLoopbackImportsTests(unittest.TestCase):
    def test_only_scorer_can_import_local_transport_dlls(self):
        self.assertEqual([], policy.forbidden_imports(
            "mozc_zenz_scorer.exe", {"winhttp.dll", "ws2_32.dll"}))
        self.assertEqual([], policy.forbidden_imports(
            "MOZC_ZENZ_SCORER.EXE", {"ws2_32.dll"}))

    def test_core_runtime_still_forbids_both(self):
        for binary in ("mozc_server.exe", "mozc_tool.exe", "mozc_renderer.exe",
                       "mozc_broker.exe", "mozc_tip64.dll", "mozc_other.exe"):
            with self.subTest(binary=binary):
                self.assertEqual(["winhttp.dll", "ws2_32.dll"],
                                 policy.forbidden_imports(
                                     binary, {"winhttp.dll", "ws2_32.dll"}))

    def test_scorer_must_reject_all_other_network_dlls(self):
        forbidden_other = policy.FORBIDDEN_DLLS - {"winhttp.dll", "ws2_32.dll"}
        self.assertTrue(forbidden_other)
        self.assertEqual(sorted(forbidden_other),
                         policy.forbidden_imports("mozc_zenz_scorer.exe",
                                                  forbidden_other))

    def test_loopback_source_contract(self):
        src = (ROOT / "src/zenz_scorer/main.cc").read_text(encoding="utf-8")
        for required in (
            'constexpr wchar_t kDefaultHost[] = L"127.0.0.1";',
            "std::wstring host = kDefaultHost;",
            "address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);",
            "address.sin_port = ::htons(0);",
            'cmd += L" --host 127.0.0.1 --port ";',
            "WINHTTP_ACCESS_TYPE_NO_PROXY",
        ):
            with self.subTest(required=required):
                self.assertIn(required, src)


if __name__ == "__main__":
    unittest.main()
