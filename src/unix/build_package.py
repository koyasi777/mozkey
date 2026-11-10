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

"""Script to make a zip file of Mozc built files."""

import argparse
import glob
import os
import shutil
import tempfile
from typing import List
import zipfile


class Packager:
  """Packager to locate the build targets to the install paths."""

  def __init__(self, args):
    self.mozc_icons_dir = args.mozc_icons_dir
    self.ibus_icons_dir = args.ibus_mozc_install_dir
    self.location_rules = {
        'ibus_mozc': args.ibus_mozc_path,
        'mozkey.xml': os.path.join(args.ibus_component_dir, 'mozkey.xml'),
        'mozc_server': os.path.join(args.mozc_dir, 'mozc_server'),
        'mozc_tool': os.path.join(args.mozc_dir, 'mozc_tool'),
        'mozc_renderer': os.path.join(args.mozc_dir, 'mozc_renderer'),
    }

  def PackageFiles(self, inputs: List[str], output: str) -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
      self.LocateFiles(tmp_dir, inputs)
      basename = os.path.splitext(output)[0]
      shutil.make_archive(basename, format='zip', root_dir=tmp_dir)

  def GetDestName(self, src: str) -> str:
    basename = os.path.basename(src)
    if basename not in self.location_rules:
      raise ValueError(f'Unexpected Linux package input: {basename}')
    return self.location_rules[basename]

  def JoinTopDir(self, top_dir: str, path: str) -> str:
    return os.path.join(top_dir, path.lstrip('/'))

  def LocateFiles(self, top_dir: str, files: List[str]) -> None:
    """Locate files to install directories."""
    # A missing or unexpected input must not silently change the install
    # manifest.  The Emacs integration is intentionally not shipped yet.
    expected = set(self.location_rules) | {'icons.zip'}
    basenames = [os.path.basename(src) for src in files]
    if len(basenames) != len(expected) or set(basenames) != expected:
      raise ValueError(f'Unexpected Linux package inputs: {basenames}')

    for src in files:
      # icons.zip is a build intermediate, not an installed payload file.
      if os.path.basename(src) == 'icons.zip':
        self.LocateIconFiles(top_dir, src)
        continue
      dest_path = self.JoinTopDir(top_dir, self.GetDestName(src))
      os.makedirs(os.path.dirname(dest_path), exist_ok=True)
      shutil.copy(src, dest_path)

  def LocateIconFiles(self, top_dir: str, icons_zip: str) -> None:
    """Locate icon files archived in icons.zip."""
    mozc_icons_dir = self.JoinTopDir(top_dir, self.mozc_icons_dir)
    ibus_icons_dir = self.JoinTopDir(top_dir, self.ibus_icons_dir)
    # Extract icons.zip to mozc_icons_dir.
    with zipfile.ZipFile(icons_zip) as icons:
      icons.extractall(mozc_icons_dir)

    # Copy mozc_icons_dir/*.png to ibus_icons_dir.
    os.makedirs(ibus_icons_dir, exist_ok=True)
    for icon in glob.glob(os.path.join(mozc_icons_dir, '*.png')):
      shutil.copy(icon, ibus_icons_dir)

    # Rename mozc.png to product_icon.png in ibus_icons_dir.
    shutil.move(os.path.join(ibus_icons_dir, 'mozc.png'),
                os.path.join(ibus_icons_dir, 'product_icon.png'))


def ParseArguments() -> argparse.Namespace:
  """Parse command line options."""
  parser = argparse.ArgumentParser()
  parser.add_argument('--inputs', nargs='+')
  # For mozc_server, mozc_tool, mozc_renderer
  parser.add_argument('--mozc_dir', default='/usr/lib/mozkey')
  # For icons
  parser.add_argument('--mozc_icons_dir', default='/usr/share/icons/mozkey')
  # For mozkey.xml
  parser.add_argument(
      '--ibus_component_dir', default='/usr/share/ibus/component'
  )
  # For ibus-engine-mozc. It's not dir but a file path.
  parser.add_argument(
      '--ibus_mozc_path', default='/usr/lib/ibus-mozkey/ibus-engine-mozkey'
  )
  # For ibus icons.
  parser.add_argument(
      '--ibus_mozc_install_dir', default='/usr/share/ibus-mozkey'
  )
  parser.add_argument('--output')
  return parser.parse_args()


def main():
  args = ParseArguments()
  packager = Packager(args)
  packager.PackageFiles(args.inputs, args.output)


if __name__ == '__main__':
  main()
