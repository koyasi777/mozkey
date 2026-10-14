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

MAJOR = 3

MINOR = 33

# BUILD number used for the OSS version.
BUILD_OSS = 6153

# Number to be increased. This value may be replaced by other tools.
BUILD = BUILD_OSS

# Represent the platform and release channel.
REVISION = 100

# User-facing Mozkey release version.  This is shown in the About dialog and
# should match the GitHub release tag without the leading "v".
MOZKEY_RELEASE_VERSION_MAJOR = 1
MOZKEY_RELEASE_VERSION_MINOR = 0
MOZKEY_RELEASE_VERSION_PATCH = 0

# macOS bundle and installer versions must advance beyond existing Mozc builds
# (such as 3.33.6153.101) while retaining the user-visible Mozkey version.
# Keep this mapping aligned with MozcVersion.GetMozkeyInstallerVersion().
MOZKEY_RELEASE_VERSION = "%d.%d.%d" % (
    MOZKEY_RELEASE_VERSION_MAJOR,
    MOZKEY_RELEASE_VERSION_MINOR,
    MOZKEY_RELEASE_VERSION_PATCH,
)
MOZKEY_MACOS_INSTALL_VERSION = "%d.%d.%d" % (
    100 + MOZKEY_RELEASE_VERSION_MAJOR,
    MOZKEY_RELEASE_VERSION_MINOR,
    MOZKEY_RELEASE_VERSION_PATCH,
)

# LINT.IfChange
DEFAULT_BUILD_LABEL_MACOS = "%d.%d.%d.%d" % (MAJOR, MINOR, BUILD, REVISION + 1)
# LINT.ThenChange(//data/version/BUILD.bazel)

# This version represents the version of Mozc IME engine (converter, predictor,
# etc.).  This version info is included both in the Mozc server and in the Mozc
# data set file so that the Mozc server can accept only the compatible version
# of data set file.  The engine version must be incremented when:
#  * POS matcher definition and/or conversion models were changed,
#  * New data are added to the data set file, and/or
#  * Any changes that loose data compatibility are made.
ENGINE_VERSION = 24

# This version is used to manage the data version and is included only in the
# data set file.  DATA_VERSION can be incremented without updating
# ENGINE_VERSION as long as it's compatible with the engine.
# This version should be reset to 0 when ENGINE_VERSION is incremented.
DATA_VERSION = 11
