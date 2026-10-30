"""Unit tests for generated dictionary inventory (stdlib only)."""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import capture_dictionary_provenance as p


class DictionaryProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Test",
                        "-c", "user.email=test@example.invalid", "commit",
                        "--allow-empty", "-q", "-m", "initial"], check=True)

    def test_hash_and_required_roots(self):
        for dirname in p.ROOTS:
            d = self.root / dirname
            d.mkdir(parents=True)
            (d / "fixture.txt").write_bytes(b"test")
        report = p.collect(self.root)
        self.assertEqual(2, report["dictionary_file_count"])
        self.assertFalse(report["upstream_input_provenance_verified"])
        self.assertEqual(p.digest(self.root / p.ROOTS[0] / "fixture.txt"),
                         report["generated_files"][0]["sha256"])

    def test_verifies_same_inventory_and_rejects_changes(self):
        for dirname in p.ROOTS:
            d = self.root / dirname
            d.mkdir(parents=True)
            (d / "fixture.txt").write_bytes(b"original")
        expected = p.collect(self.root)
        actual = p.collect(self.root)
        verified = p.verify_inventory(actual, expected)
        self.assertTrue(verified["generated_files_match_reference"])
        self.assertFalse(verified["upstream_input_provenance_verified"])

        original_path = self.root / p.ROOTS[0] / "fixture.txt"
        original_path.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "differs"):
            p.verify_inventory(p.collect(self.root), expected)
        original_path.write_bytes(b"original")
        (self.root / p.ROOTS[1] / "new.txt").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "extra"):
            p.verify_inventory(p.collect(self.root), expected)
        (self.root / p.ROOTS[1] / "new.txt").unlink()
        original_path.unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            p.verify_inventory(p.collect(self.root), expected)

    def test_rejects_corrupt_manifest(self):
        for dirname in p.ROOTS:
            d = self.root / dirname
            d.mkdir(parents=True)
            (d / "fixture.txt").write_bytes(b"original")
        actual = p.collect(self.root)
        invalid = dict(actual)
        invalid["status"] = "RELEASE_APPROVED"
        with self.assertRaisesRegex(ValueError, "schema or status"):
            p.verify_inventory(actual, invalid)
        invalid = dict(actual)
        invalid["dictionary_file_count"] = 999
        with self.assertRaisesRegex(ValueError, "count mismatch"):
            p.verify_inventory(actual, invalid)

    def test_missing_root_rejected(self):
        (self.root / p.ROOTS[0]).mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "directory absent"):
            p.collect(self.root)

    def test_symlink_rejected(self):
        for dirname in p.ROOTS:
            (self.root / dirname).mkdir(parents=True)
        source = self.root / "outside.txt"
        source.write_bytes(b"source")
        link = self.root / p.ROOTS[0] / "link.txt"
        try:
            link.symlink_to(source)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unsupported")
        with self.assertRaisesRegex(ValueError, "symlink"):
            p.collect(self.root)


if __name__ == "__main__":
    unittest.main()
