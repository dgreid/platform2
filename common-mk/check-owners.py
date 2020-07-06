#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Linter for various OWNERS files."""

from __future__ import print_function

import os
import sys

TOP_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

# Find chromite!
sys.path.insert(0, os.path.join(TOP_DIR, '..', '..'))

# pylint: disable=wrong-import-position
from chromite.lib import commandline
from chromite.lib import git
from chromite.lib import cros_logging as logging
# pylint: enable=wrong-import-position


def GetActiveProjects():
  """Return the list of active projects."""
  # Look at all the paths (files & dirs) in the top of the git repo.  This way
  # we ignore local directories devs created that aren't actually committed.
  cmd = ['ls-tree', '--name-only', '-z', 'HEAD']
  result = git.RunGit(TOP_DIR, cmd)

  # Split the output on NULs to avoid whitespace/etc... issues.
  paths = result.stdout.split('\0')

  # ls-tree -z will include a trailing NUL on all entries, not just seperation,
  # so filter it out if found (in case ls-tree behavior changes on us).
  for path in [x for x in paths if x]:
    if os.path.isdir(os.path.join(TOP_DIR, path)):
      yield path


def CheckSubdirs():
  """Check the subdir OWNERS files exist."""
  # Legacy projects that don't have an OWNERS file.
  # Someone should claim them :D.
  LEGACYLIST = (
      'container_utils',
      'cros-fuzz',
      'screenshot',
  )

  ret = 0
  for proj in GetActiveProjects():
    path = os.path.join(TOP_DIR, proj, 'OWNERS')
    if os.path.exists(path):
      if proj in LEGACYLIST:
        logging.error('*** Project "%s" is in no-OWNERS LEGACYLIST, but '
                      ' actually has one. Please remove it from LEGACYLIST!',
                      proj)
    else:
      if not proj in LEGACYLIST:
        logging.error('*** Project "%s" needs an OWNERS file', proj)
        ret = 1
  return ret


def GetParser():
  """Return an argument parser."""
  parser = commandline.ArgumentParser(description=__doc__)
  return parser


def main(argv):
  """The main func!"""
  parser = GetParser()
  opts = parser.parse_args(argv)
  opts.Freeze()

  return CheckSubdirs()


if __name__ == '__main__':
  commandline.ScriptWrapperMain(lambda _: main)
