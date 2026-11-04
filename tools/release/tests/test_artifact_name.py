"""Regression coverage for the canonical public asset naming contract."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artifact_name import ReleaseTarget, artifact_name


class ArtifactNamingTests(unittest.TestCase):
    def test_all_approved_targets(self):
        cases = (
            ("windows", "x64", "msi", "Mozkey_v1.0.0_windows_x64.msi"),
            ("windows", "arm64", "msi", "Mozkey_v1.0.0_windows_arm64.msi"),
            ("macos", "universal", "pkg", "Mozkey_v1.0.0_macos_universal.pkg"),
        )
        for os_name, arch, package_format, expected in cases:
            with self.subTest(target=expected):
                self.assertEqual(
                    artifact_name("1.0.0", ReleaseTarget(os_name, arch, package_format)),
                    expected,
                )

    def test_version_is_canonical_and_safe(self):
        target = ReleaseTarget("windows", "x64", "msi")
        for version in (
            "", "v1.0.0", "01.0.0", "1.00.0", "1.0.00", "1.0", "1.0.0-rc1",
            "1.0.0\n", "1.0.0/evil", "1.0.0\\evil", "1.0.0;echo hi",
            "１.０.０", "1.0.0" + "0" * 40,
        ):
            with self.subTest(version=version), self.assertRaises(ValueError):
                artifact_name(version, target)
        self.assertEqual(artifact_name("0.0.0", target), "Mozkey_v0.0.0_windows_x64.msi")
        self.assertEqual(artifact_name("12.34.567", target), "Mozkey_v12.34.567_windows_x64.msi")

    def test_unapproved_combinations_fail_closed(self):
        for target in (
            ReleaseTarget("windows", "universal", "msi"),
            ReleaseTarget("macos", "x64", "pkg"),
            ReleaseTarget("linux", "x64", "deb"),
            ReleaseTarget("linux", "arm64", "rpm"),
            ReleaseTarget("windows", "x64", "exe"),
            ReleaseTarget("Windows", "x64", "msi"),
            ReleaseTarget("macos", "universal", "msi"),
        ):
            with self.subTest(target=target), self.assertRaises(ValueError):
                artifact_name("1.0.0", target)

    def test_cli_exact_stdout_and_error_status(self):
        script = Path(__file__).resolve().parents[1] / "artifact_name.py"
        command = [sys.executable, str(script), "--version", "1.0.0", "--os", "windows",
                   "--arch", "x64", "--format", "msi"]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "Mozkey_v1.0.0_windows_x64.msi\n")
        self.assertEqual(result.stderr, "")
        command[-1] = "pkg"
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
