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

"""Create and validate a native Linux Bazel vendor archive for Mozkey.

Run in a dedicated Git checkout with the daily dictionary prepared and real
Linux development packages installed. Use the same preinstalled Bazel binary
and architecture for generation and consumption. This is not a standalone
source/SDK/dictionary archive or proof of an offline build.
"""

import argparse
from collections.abc import Sequence
import gzip
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import tarfile


ABS_MOZC_SRC_DIR = pathlib.Path(__file__).resolve().parents[1]
VENDOR_INPUTS_SHA256_FILENAME = 'vendor_inputs.sha256'
METADATA_FILENAME = 'mozkey_vendor.json'
DAILY_FILES = (
    'dic-nico-pixiv-delta.txt',
    'koyasi-syntax-guard.txt',
    'mozcdic-ut-daily.txt',
    'mozcdic-ut-personal-names-daily.txt',
)

DEFAULT_VENDOR_BAZEL_HEADER = (
    '#######################################################################'
    '########\n'
    '# This file is used to configure how external repositories are handled '
    'in vendor mode.\n'
    '# ONLY the two following functions can be used:\n'
    '#\n'
    "# ignore('@@<canonical repo name>', ...) is used to completely ignore "
    'this repo from vendoring.\n'
    '# Bazel will use the normal external cache and fetch process for this '
    'repo.\n'
    '#\n'
    "# pin('@@<canonical repo name>', ...) is used to pin the contents of "
    'this repo under the vendor\n'
    '# directory as if there is a --override_repository flag for this repo.\n'
    '# Note that Bazel will NOT update the vendored source for this repo '
    'while running vendor command\n'
    "# unless it's unpinned. The user can modify and maintain the vendored "
    'source for this repo manually.\n'
    '#######################################################################'
    '########'
)

COMMON_EXTRA_REPOS = (
    '@material_icon_dictionary_png',
    '@material_icon_dictionary_svg',
    '@material_icon_properties_png',
    '@material_icon_properties_svg',
    '@material_icon_tool_png',
    '@material_icon_tool_svg',
    '@ja_usage_dict',
    '@zip_code_jigyosyo',
    '@zip_code_ken_all',
)

def sha256(path: pathlib.Path) -> str:
  digest = hashlib.sha256()
  with path.open('rb') as file:
    for chunk in iter(lambda: file.read(1024 * 1024), b''):
      digest.update(chunk)
  return digest.hexdigest()


def git_root(src: pathlib.Path) -> pathlib.Path:
  root = pathlib.Path(subprocess.check_output(
      ['git', 'rev-parse', '--show-toplevel'], cwd=src, text=True).strip())
  if (root / 'src').resolve() != src:
    raise ValueError('Expected the Mozkey Git checkout src directory')
  return root


def input_checksums(src: pathlib.Path) -> str:
  """Hash the exact set of build definitions, including candidate additions."""
  root = git_root(src)
  raw = subprocess.check_output(
      ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
      cwd=root)
  paths = {os.fsdecode(p) for p in raw.split(b'\0') if p}
  selected = []
  for name in sorted(paths):
    p = pathlib.PurePosixPath(name)
    if (name in ('.bazelversion', 'src/.bazelversion', 'src/.bazelrc',
                 'src/build_tools/create_bazel_vendor.py') or
        (name.startswith('src/') and
         (p.suffix in ('.bazel', '.bzl', '.patch') or
          p.name in ('BUILD', 'WORKSPACE') or name.startswith('src/bazel/')))):
      path = root / name
      if '\n' in name or '\r' in name or path.is_symlink() or not path.is_file():
        raise ValueError(f'Invalid or missing build input: {name}')
      selected.append(f'{sha256(path)}  {name}\n')
  if 'src/MODULE.bazel' not in paths or 'src/.bazelrc' not in paths:
    raise ValueError('Missing required build inputs')
  return ''.join(selected)


def check_daily(src: pathlib.Path) -> None:
  build = (src / 'data/dictionary_oss/BUILD.bazel').read_text(encoding='utf-8')
  if '"//data/dictionary_koyasi:mozcdic_ut_daily_local"' not in build:
    raise ValueError('Only the checked-in daily profile is supported')
  directory = src / 'data/dictionary_koyasi/generated/profiled'
  for name in DAILY_FILES:
    path = directory / name
    if not path.is_file():
      raise FileNotFoundError(f'Prepare daily dictionary first: {path}')


def bazel_identity(binary: pathlib.Path) -> dict:
  return {
      'machine': platform.machine(),
      'system': platform.system(),
      'bazel_sha256': sha256(binary),
      'bazel_version': subprocess.check_output(
          [str(binary), '--version'], text=True).strip(),
      'config': 'oss_linux',
  }


def run_vendor(binary: pathlib.Path, src: pathlib.Path, output: pathlib.Path,
               vendor_dir: pathlib.Path) -> None:
  # Native SDKs are required; no placeholder repo overrides are used.
  subprocess.run(['pkg-config', '--exists', 'glib-2.0', 'gobject-2.0',
                  'ibus-1.0', 'Qt6Core', 'Qt6Gui', 'Qt6Widgets'], check=True)
  env = dict(os.environ, ANDROID_NDK_HOME='')
  command = [str(binary), '--batch', '--nosystem_rc', '--nohome_rc',
             f'--output_user_root={output / "bazel_root"}', 'vendor',
             f'--vendor_dir={vendor_dir}', '--symlink_prefix=/',
             '--lockfile_mode=update', '--config=oss_linux']
  for flags in (['--config=release_build', '//:package'],
                ['--compilation_mode=dbg', '--build_tests_only', '//...'],
                [f'--repo={repo}' for repo in COMMON_EXTRA_REPOS]):
    subprocess.run([*command, *flags], cwd=src, env=env, check=True)


def reset_tarinfo(tarinfo: tarfile.TarInfo) -> tarfile.TarInfo:
  """Strips host user, group, and timestamp metadata from tar entries."""
  tarinfo.uid = 0
  tarinfo.gid = 0
  tarinfo.uname = ''
  tarinfo.gname = ''
  tarinfo.mtime = 0
  return tarinfo


def pin_vendored_repos(vendor_dir: pathlib.Path) -> Sequence[str]:
  """Pins all vendored repositories in VENDOR.bazel and cleans host markers.

  Adapted from upstream Mozc. Marker validation depends on host and repository
  environment. Pinning uses the vendored directories without marker validation.
  Only use a trusted archive after verifying the recorded input set. Local SDK
  repositories are resolved on the consumer host, not pinned. Cross-platform
  or cross-architecture consumption is not supported by this tool.

  Args:
    vendor_dir: Path to the populated Bazel vendor directory.

  Returns:
    Sorted sequence of pinned canonical repository directory names.
  """
  for entry in list(vendor_dir.iterdir()):
    if (
        entry.is_file()
        and not entry.is_symlink()
        and entry.name.startswith('@')
        and entry.name.endswith('.marker')
    ):
      entry.unlink()
    elif entry.is_symlink() and entry.name == 'bazel-external':
      entry.unlink()

  repo_names = tuple(
      sorted(
          entry.name
          for entry in vendor_dir.iterdir()
          if entry.is_dir()
          and not entry.is_symlink()
          and entry.name != '_registries'
      )
  )

  vendor_bazel_path = vendor_dir / 'VENDOR.bazel'
  if vendor_bazel_path.is_file():
    header_lines = [
        line
        for line in vendor_bazel_path.read_text(encoding='utf-8').splitlines()
        if line.strip().startswith('#')
    ]
    header = (
        '\n'.join(header_lines) if header_lines else DEFAULT_VENDOR_BAZEL_HEADER
    )
  else:
    header = DEFAULT_VENDOR_BAZEL_HEADER

  lines = [header]
  if repo_names:
    lines.append('')
    lines.append('pin(')
    for repo_name in repo_names:
      lines.append(f'    "@@{repo_name}",')
    lines.append(')')
  vendor_bazel_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
  return repo_names


def check_local_repos(vendor_dir: pathlib.Path) -> None:
  """Fail rather than pin known host-specific repositories."""
  for entry in vendor_dir.iterdir():
    name = entry.name
    if ('local_config_cc' in name or
        'local_config_xcode' in name or name.endswith((
            '+ibus', '+qt_linux', '+qt_mac', '+qt_win', '+windows_sdk', '+wix',
            '+androidndk'))):
      raise ValueError(f'Host-specific repository in vendor output: {name}')


def create_vendor_archive(src: pathlib.Path, output: pathlib.Path,
                          binary: pathlib.Path) -> pathlib.Path:
  src, output, binary = src.resolve(), output.resolve(), binary.resolve()
  if platform.system() != 'Linux':
    raise ValueError('Only native Linux is supported')
  root = git_root(src)
  if output.is_relative_to(root):
    raise ValueError('Output must be outside the Git checkout')
  check_daily(src)
  identity = bazel_identity(binary)
  checksums = input_checksums(src)
  lock = src / 'MODULE.bazel.lock'
  if lock.is_symlink() or (lock.exists() and not lock.is_file()):
    raise ValueError('Lockfile must be a regular file')
  original = lock.read_bytes() if lock.exists() else None
  # Never remove existing user output. An interrupted run leaves its own
  # directory for diagnosis; choose another path for the next attempt.
  output.mkdir(parents=True, exist_ok=False)
  vendor_dir = output / 'vendor_dir'
  try:
    lock.unlink(missing_ok=True)
    run_vendor(binary, src, output, vendor_dir)
    shutil.copyfile(lock, vendor_dir / lock.name)
  finally:
    if original is None:
      lock.unlink(missing_ok=True)
    else:
      lock.write_bytes(original)
  if input_checksums(src) != checksums:
    raise ValueError('Build inputs changed during vendoring')
  check_local_repos(vendor_dir)
  pinned = pin_vendored_repos(vendor_dir)
  if not pinned or not (vendor_dir / '_registries').is_dir():
    raise ValueError('Missing vendored repositories or registry metadata')
  (vendor_dir / VENDOR_INPUTS_SHA256_FILENAME).write_text(checksums, encoding='utf-8')
  identity['lock_sha256'] = sha256(vendor_dir / lock.name)
  (vendor_dir / METADATA_FILENAME).write_text(
      json.dumps(identity, indent=2, sort_keys=True) + '\n', encoding='utf-8')
  archive = output / 'bazel_vendor.tar.gz'
  temp = output / '.bazel_vendor.tar.gz.tmp'
  try:
    with temp.open('xb') as raw:
      with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as gz:
        with tarfile.open(mode='w', fileobj=gz) as stream:
          stream.add(vendor_dir, arcname='vendor_dir', filter=reset_tarinfo)
    temp.replace(archive)
  finally:
    temp.unlink(missing_ok=True)
  (output / 'bazel_vendor.tar.gz.sha256').write_text(
      f'{sha256(archive)}  {archive.name}\n', encoding='utf-8')
  return archive


def verify_vendor(src: pathlib.Path, vendor_dir: pathlib.Path,
                  binary: pathlib.Path) -> None:
  """Check build input set and lockfile identity before using a trusted archive."""
  src, vendor_dir, binary = src.resolve(), vendor_dir.resolve(), binary.resolve()
  expected = json.loads((vendor_dir / METADATA_FILENAME).read_text(encoding='utf-8'))
  actual = bazel_identity(binary)
  if any(expected.get(key) != value for key, value in actual.items()):
    raise ValueError('Bazel binary or host configuration mismatch')
  if expected.get('lock_sha256') != sha256(vendor_dir / 'MODULE.bazel.lock'):
    raise ValueError('Vendor lockfile mismatch')
  if (vendor_dir / VENDOR_INPUTS_SHA256_FILENAME).read_text(
      encoding='utf-8') != input_checksums(src):
    raise ValueError('Vendor build input set or checksum mismatch')
  check_daily(src)
  check_local_repos(vendor_dir)
  if not (vendor_dir / '_registries').is_dir() or not (vendor_dir / 'VENDOR.bazel').is_file():
    raise ValueError('Missing vendor registry or pin file')


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--mozc_src_dir', type=pathlib.Path, default=ABS_MOZC_SRC_DIR)
  parser.add_argument('--bazel_bin', type=pathlib.Path, required=True,
                      help='Path to the actual preinstalled Bazel binary (not Bazelisk)')
  mode = parser.add_mutually_exclusive_group(required=True)
  mode.add_argument('--output_dir', type=pathlib.Path)
  mode.add_argument('--verify_vendor', type=pathlib.Path)
  args = parser.parse_args()
  if args.output_dir is not None:
    archive = create_vendor_archive(args.mozc_src_dir, args.output_dir, args.bazel_bin)
    print(f'MOZKEY_VENDOR_GENERATE=PASS Archive={archive}')
  else:
    verify_vendor(args.mozc_src_dir, args.verify_vendor, args.bazel_bin)
    print('MOZKEY_VENDOR_INPUTS=PASS')


if __name__ == '__main__':
  main()
