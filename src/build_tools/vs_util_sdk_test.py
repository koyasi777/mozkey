"""Regression tests for explicit Windows SDK selection in vs_util."""

import json
import pathlib
import unittest
from unittest import mock

from build_tools import vs_util


class VsUtilSdkTest(unittest.TestCase):

  def setUp(self):
    vcvars = mock.patch.object(
        vs_util, 'get_vcvarsall', return_value=pathlib.Path('vcvarsall.bat')
    )
    self.addCleanup(vcvars.stop)
    vcvars.start()
    popen = mock.patch.object(vs_util.subprocess, 'Popen')
    self.addCleanup(popen.stop)
    self.popen = popen.start()
    self.popen.return_value.wait.return_value = 0
    self.set_environment({'WindowsSDKVersion': '10.0.26100.0\\'})

  def set_environment(self, env):
    self.popen.return_value.communicate.return_value = (
        json.dumps(env).encode('ascii'), None
    )

  def test_default_selection_unchanged(self):
    for version in (None, ''):
      with self.subTest(version=version):
        result = vs_util.get_vs_env_vars(
            'x64_arm64', windows_sdk_version=version
        )
        self.assertIn(' x64_arm64>nul)', self.popen.call_args.args[0])
        self.assertEqual(result['WindowsSDKVersion'], '10.0.26100.0\\')

  def test_explicit_selection(self):
    for key in ('WindowsSDKVersion', 'WINDOWSSDKVERSION'):
      with self.subTest(key=key):
        self.set_environment({key: '10.0.26100.0\\'})
        result = vs_util.get_vs_env_vars(
            'x64_arm64', windows_sdk_version='10.0.26100.0'
        )
        self.assertIn(
            ' x64_arm64 10.0.26100.0>nul)', self.popen.call_args.args[0]
        )
        self.assertEqual(result[key], '10.0.26100.0\\')

  def test_invalid_version_rejected_before_shell(self):
    for version in ('10.0', '10.0.26100.0&echo bad', ' 10.0.26100.0'):
      with self.subTest(version=version):
        with self.assertRaises(ValueError):
          vs_util.get_vs_env_vars('x64_arm64', windows_sdk_version=version)
    self.popen.assert_not_called()

  def test_wrong_or_missing_sdk_rejected(self):
    for env in ({}, {'WindowsSDKVersion': '10.0.28000.0\\'}):
      with self.subTest(env=env):
        self.set_environment(env)
        with self.assertRaises(ChildProcessError):
          vs_util.get_vs_env_vars(
              'x64_arm64', windows_sdk_version='10.0.26100.0'
          )

  def test_vcvars_failure(self):
    self.popen.return_value.wait.return_value = 1
    with self.assertRaises(ChildProcessError):
      vs_util.get_vs_env_vars('x64_arm64', windows_sdk_version='10.0.26100.0')


if __name__ == '__main__':
  unittest.main()
