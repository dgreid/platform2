#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Linter for various OWNERS files."""

from __future__ import division
from __future__ import print_function

from pathlib import Path
import sys

TOP_DIR = Path(__file__).resolve().parent.parent

# Find chromite!
sys.path.insert(0, str(TOP_DIR.parent.parent))

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
  for path in [Path(x) for x in paths if x]:
    if (TOP_DIR / path).is_dir():
      yield path


def CheckSubdirs():
  """Check the subdir OWNERS files exist."""
  ret = 0
  for proj in GetActiveProjects():
    path = TOP_DIR / proj / 'OWNERS'
    if not path.exists():
      logging.error('*** Project "%s" needs an OWNERS file', proj)
      ret = 1
      continue
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
