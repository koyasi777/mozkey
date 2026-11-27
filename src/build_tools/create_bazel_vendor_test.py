# -*- coding: utf-8 -*-
# Copyright 2010-2021, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import hashlib
import pathlib
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

from build_tools import create_bazel_vendor as vendor


class CreateBazelVendorTest(unittest.TestCase):

  def setUp(self):
    self.temp = tempfile.TemporaryDirectory()
    self.addCleanup(self.temp.cleanup)
    self.root = pathlib.Path(self.temp.name).resolve() / 'repo'
    self.src = self.root / 'src'
    self.src.mkdir(parents=True)
    subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
    self.write('src/MODULE.bazel', 'module(name = "mozc")\n')
    self.write('src/.bazelrc', '# test\n')
    self.write('src/bazel/BUILD.qt.bazel', '# overlay\n')
    self.write('src/bazel/repo.bzl', '# rule\n')
    self.write('src/bazel/repo.patch', '# patch\n')
    self.write('src/data/dictionary_oss/BUILD.bazel',
               '"//data/dictionary_koyasi:mozcdic_ut_daily_local"\n')
    for name in vendor.DAILY_FILES:
      self.write('src/data/dictionary_koyasi/generated/profiled/' + name, 'data\n')
    self.write('.gitignore', 'src/MODULE.bazel.lock\n')
    self.output = self.root.parent / 'vendor-output'
    self.binary = self.root.parent / 'bazel'
    self.binary.write_bytes(b'fake-bazel')
    self.identity = {'system': 'Linux', 'machine': 'x86_64',
                     'bazel_version': 'bazel test', 'bazel_sha256': 'mock',
                     'config': 'oss_linux'}

  def write(self, relpath, content):
    path = self.root / relpath
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')
    return path

  def fake_vendor(self, binary, src, output, directory):
    del binary, output
    (directory / 'rules_cc+').mkdir(parents=True)
    (directory / '_registries').mkdir()
    (directory / '@rules_cc+.marker').write_text('marker', encoding='utf-8')
    (src / 'MODULE.bazel.lock').write_bytes(b'generated lock')

  def generate(self):
    with mock.patch.object(vendor.platform, 'system', return_value='Linux'), \
         mock.patch.object(vendor, 'bazel_identity', return_value=dict(self.identity)), \
         mock.patch.object(vendor, 'run_vendor', side_effect=self.fake_vendor):
      return vendor.create_vendor_archive(self.src, self.output, self.binary)

  def verify(self):
    with mock.patch.object(vendor, 'bazel_identity', return_value=dict(self.identity)):
      vendor.verify_vendor(self.src, self.output / 'vendor_dir', self.binary)

  def test_pin_cleans_markers_and_excludes_registry(self):
    directory = self.output
    directory.mkdir()
    (directory / 'rules_cc+').mkdir()
    (directory / '_registries').mkdir()
    (directory / '@rules_cc+.marker').write_text('hash', encoding='utf-8')
    self.assertEqual(vendor.pin_vendored_repos(directory), ('rules_cc+',))
    self.assertFalse((directory / '@rules_cc+.marker').exists())
    pins = (directory / 'VENDOR.bazel').read_text(encoding='utf-8')
    self.assertIn('"@@rules_cc+"', pins)
    self.assertNotIn('_registries', pins)

  def test_checksum_set_includes_new_build_files_and_repo_rules(self):
    baseline = vendor.input_checksums(self.src)
    for name in ('src/MODULE.bazel', 'src/.bazelrc', 'src/bazel/repo.bzl',
                 'src/bazel/repo.patch', 'src/bazel/BUILD.qt.bazel'):
      self.assertIn(name + '\n', baseline)
    self.write('src/new/BUILD.bazel', '# new\n')
    self.assertNotEqual(baseline, vendor.input_checksums(self.src))

  def test_generate_preserves_lock_and_verifies(self):
    original = self.write('src/MODULE.bazel.lock', 'original lock')
    data = original.read_bytes()
    archive = self.generate()
    self.assertEqual(original.read_bytes(), data)
    self.assertEqual(archive.name, 'bazel_vendor.tar.gz')
    with tarfile.open(archive) as stream:
      self.assertIn('vendor_dir/MODULE.bazel.lock', stream.getnames())
      self.assertNotIn('vendor_dir/@rules_cc+.marker', stream.getnames())
    expected = hashlib.sha256(archive.read_bytes()).hexdigest()
    self.assertTrue((self.output / 'bazel_vendor.tar.gz.sha256').read_text().startswith(expected))
    self.verify()

  def test_generate_restores_absent_lock(self):
    self.generate()
    self.assertFalse((self.src / 'MODULE.bazel.lock').exists())

  def test_failure_restores_original_lock(self):
    original = self.write('src/MODULE.bazel.lock', 'original lock')
    data = original.read_bytes()
    def fail(*args):
      original.write_bytes(b'partial lock')
      raise RuntimeError('mock failure')
    with mock.patch.object(vendor.platform, 'system', return_value='Linux'), \
         mock.patch.object(vendor, 'bazel_identity', return_value=dict(self.identity)), \
         mock.patch.object(vendor, 'run_vendor', side_effect=fail):
      with self.assertRaisesRegex(RuntimeError, 'mock failure'):
        vendor.create_vendor_archive(self.src, self.output, self.binary)
    self.assertEqual(original.read_bytes(), data)
    self.assertFalse((self.output / 'bazel_vendor.tar.gz').exists())

  def test_existing_output_is_not_deleted(self):
    self.output.mkdir()
    sentinel = self.output / 'keep.txt'
    sentinel.write_text('keep', encoding='utf-8')
    with self.assertRaises(FileExistsError):
      self.generate()
    self.assertEqual(sentinel.read_text(encoding='utf-8'), 'keep')

  def test_missing_daily_file_rejected(self):
    (self.src / 'data/dictionary_koyasi/generated/profiled' / vendor.DAILY_FILES[0]).unlink()
    with self.assertRaises(FileNotFoundError):
      self.generate()
    self.assertFalse(self.output.exists())

  def test_verify_rejects_changed_inputs(self):
    self.generate()
    self.write('src/bazel/repo.bzl', '# modified\n')
    with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
      self.verify()

  def test_verify_rejects_modified_lock(self):
    self.generate()
    (self.output / 'vendor_dir/MODULE.bazel.lock').write_bytes(b'changed')
    with self.assertRaisesRegex(ValueError, 'lockfile mismatch'):
      self.verify()

  def test_host_repo_rejected(self):
    self.output.mkdir()
    (self.output / '+pkg_config_repository+qt_linux').mkdir()
    with self.assertRaisesRegex(ValueError, 'Host-specific'):
      vendor.check_local_repos(self.output)


if __name__ == '__main__':
  unittest.main()
