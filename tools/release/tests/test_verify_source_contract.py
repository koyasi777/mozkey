"""Contract regression tests; stdlib only, no checkout mutation."""
from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import verify_source_contract as c


class ReleaseContractTests(unittest.TestCase):
    def test_version_valid(self):
        self.assertEqual((1, 0, 0), c.parse_version("1.0.0"))
        self.assertEqual((0, 7, 7), c.parse_version("0.7.7"))

    def test_version_rejects_noncanonical_and_injection(self):
        for text in ("v1.0.0", "1.00.0", "01.0.0", "1.0", "1.0.0;echo", "1.0.0\n", "-1.0.0"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                c.parse_version(text)

    def test_version_source_requires_exact_assignments(self):
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / "version.bzl"
            f.write_text("\n".join(f"{k} = {i}" for k, i in zip(c.VERSION_KEYS, (1, 0, 0))), encoding="utf-8")
            self.assertEqual((1, 0, 0), c.load_release_parts(f))
            f.write_text(f.read_text() + "\n" + "MOZKEY_RELEASE_VERSION_MAJOR = 2\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                c.load_release_parts(f)

    def test_version_limits(self):
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / "version.bzl"
            f.write_text("\n".join(f"{k} = {i}" for k, i in zip(c.VERSION_KEYS, (156, 0, 0))), encoding="utf-8")
            with self.assertRaises(ValueError):
                c.load_release_parts(f)

    def test_origin_restriction(self):
        for ok in ("https://github.com/koyasi777/mozkey.git", "git@github.com:koyasi777/mozkey.git"):
            self.assertTrue(c.EXPECTED_ORIGIN.fullmatch(ok))
        for no in ("https://github.com/other/mozkey", "https://evil.test/koyasi777/mozkey", "https://github.com/koyasi777/mozkey.evil"):
            self.assertFalse(c.EXPECTED_ORIGIN.fullmatch(no))

    def test_contract_with_synthetic_source(self):
        import json
        with tempfile.TemporaryDirectory() as d:
            repo = pathlib.Path(d)
            fake = {
                'src/version.bzl': '\n'.join(f'{key} = {num}' for key, num in zip(c.VERSION_KEYS, (1, 0, 0))),
                'src/config.bzl': 'BRANDING = "Mozc"\n',
                'src/win32/installer/BUILD.bazel': (
                    '_MSI_FILE = "Mozc64.msi" if BRANDING == "Mozc"\n'
                    '"//win32/installer/zenz_runtime:llama-server.exe"\n'
                    '"//win32/installer/zenz_runtime:models/zenz-v3.2-small-Q5_K_M.gguf"\n'),
                'src/win32/installer/installer_oss_64bit.wxs': '<Package Name="Mozkey" Manufacturer="koyasi777" />\n',
                'src/win32/installer/build_installer.py': f'{c.EXPECTED_UPGRADE_CODE}\nversion.GetMozkeyMsiVersion()\n',
                'src/zenz_runtime/llama_runtime.lock.json': json.dumps({
                    'model': {'sha256': 'a'*64,
                              'windows_file': 'src/win32/installer/zenz_runtime/models/zenz-v3.2-small-Q5_K_M.gguf'},
                    'windows': {
                        'x64': {'pe_machine': '0x8664', 'sha256': 'b'*64,
                                'path': 'src/win32/installer/zenz_runtime/x64/llama-server.exe'},
                        'arm64': {'pe_machine': '0xAA64', 'sha256': 'c'*64,
                                  'path': 'src/win32/installer/zenz_runtime/arm64/llama-server.exe'},
                    }}),
            }
            for rel, contents in fake.items():
                path = repo / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding='utf-8')
            info = c.check_contract(repo, '1.0.0')
            self.assertEqual('101.0.0', info['expected_msi_product_version'])
            self.assertEqual('C' * 64, info['llama_arm64_sha256'])
            with self.assertRaises(ValueError):
                c.check_contract(repo, '0.7.7')
            (repo / 'src/config.bzl').write_text('BRANDING = "GoogleJapaneseInput"', encoding='utf-8')
            with self.assertRaises(ValueError):
                c.check_contract(repo, '1.0.0')

    def test_workflow_does_not_publish_or_sign(self):
        workflow = (pathlib.Path(__file__).resolve().parents[3] /
                    ".github/workflows/windows-release-candidate.yml").read_text(encoding="utf-8")
        self.assertIn("contents: read", workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn("if-no-files-found: error", workflow)
        self.assertNotIn("signpath/", workflow.lower())
        self.assertNotIn("SIGNPATH_API_TOKEN", workflow)
        self.assertNotIn("action-gh-release", workflow)
        self.assertNotIn("contents: write", workflow)
        self.assertNotIn("actions/cache@", workflow)
        self.assertIn('windows-11-arm', workflow)
        self.assertIn('windows-2025', workflow)
        self.assertIn('matrix.arch', workflow)
        self.assertIn('fail-fast: false', workflow)
        self.assertNotIn('win_universal_installer', workflow)
        self.assertIn('if-no-files-found: error', workflow)


if __name__ == "__main__":
    unittest.main()
