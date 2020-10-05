# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Android Installer for Chrome OS

This script will be called by push_to_device, chromeos-base/android-{container,
vm}-* ebuilds and board_specific_setup.
"""

from __future__ import print_function

import argparse
import enum
import sys
from typing import Dict, List


class AndroidInstallerCaller(enum.Enum):
  """Android Installer Caller"""
  PUSH_TO_DEVICE = 0
  EBUILD_SRC_COMPILE = 1
  EBUILD_SRC_INSTALL = 2
  EBUILD_SRC_TEST = 3
  BOARD_SPECIFIC_SETUP = 4
  BOARD_SPECIFIC_SETUP_TEST = 5

  @staticmethod
  def allowed_callers() -> List[str]:
    return [name.lower() for name in
            AndroidInstallerCaller.__members__.keys()]

  @staticmethod
  # TODO(boleynsu): annotate the return type once we are using python>3.6
  def from_str(caller: str):
    return AndroidInstallerCaller[caller.upper()]



class AndroidInstaller:
  """Android Installer"""

  # env variables used by Android Installer
  env: Dict[str, str]
  # use flags used by Android Installer
  use: Dict[str, bool]
  # caller of Android Installer
  caller: AndroidInstallerCaller

  def __init__(self, argv: List[str]) -> None:
    """Parse the arguments."""

    parser = argparse.ArgumentParser(
        usage=
        """
        Example:
          %(prog)s --env=ROOT=/build/rammus-arc-r --use=-cheets_local_img \\
            --caller push_to_device

          %(prog)s --env ROOT=/build/rammus-arc-r --env PV=9999 \\
            --env KEY_WITH_NO_VALUE= --use +cheets_local_img \\
            --use +chromeos-base/chromeos-cheets:android-container-pi \\
            --caller ebuild_src_compile
        Note:
          The dash used in --use is special so we must use --use=-use_flag
          instead of --use -use_flag
        """
    )
    parser.add_argument('--env', action='append',
                        help='the format is KEY=VALUE')
    parser.add_argument('--use', action='append',
                        help='the format is +use_flag or -use_flag')
    parser.add_argument(
        '--caller', choices=AndroidInstallerCaller.allowed_callers(),
        help='the caller of this script')

    args = parser.parse_args(args=argv)

    self.env = dict()
    if args.env:
      for e in args.env:
        kv = e.split('=', 1)
        if len(kv) < 2:
          raise ValueError(
              'Invalid --env %s argument.' % e +
              ' = is missing. For a key with no value, please use --env KEY=')
        key, value = kv
        if not key:
          raise ValueError(
              'Invalid --env %s argument.' % e +
              ' The key should not be empty.')
        # The later argument will overwrite the former one.
        self.env[key] = value

    self.use = dict()
    if args.use:
      for u in args.use:
        key = u[1:]
        if u[0] == '+':
          value = True
        elif u[0] == '-':
          value = False
        else:
          raise ValueError(
              'Invalid --use %s argument.' % u +
              ' The first character should be + or -.')
        # The later argument will overwrite the former one.
        self.use[key] = value

    if not args.caller:
      raise ValueError('--caller must be specified')
    self.caller = AndroidInstallerCaller.from_str(args.caller)

  def main(self) -> None:
    if self.caller in [AndroidInstallerCaller.PUSH_TO_DEVICE,
                       AndroidInstallerCaller.EBUILD_SRC_COMPILE]:
      self.ebuild_src_compile()
    if self.caller in [AndroidInstallerCaller.PUSH_TO_DEVICE,
                       AndroidInstallerCaller.EBUILD_SRC_TEST]:
      self.ebuild_src_test()
    if self.caller in [AndroidInstallerCaller.PUSH_TO_DEVICE,
                       AndroidInstallerCaller.EBUILD_SRC_INSTALL]:
      self.ebuild_src_install()
    if self.caller in [AndroidInstallerCaller.PUSH_TO_DEVICE,
                       AndroidInstallerCaller.BOARD_SPECIFIC_SETUP]:
      self.board_specific_setup()
    if self.caller in [AndroidInstallerCaller.PUSH_TO_DEVICE,
                       AndroidInstallerCaller.BOARD_SPECIFIC_SETUP_TEST]:
      self.board_specific_setup_test()

  def ebuild_src_compile(self) -> None:
    # TODO(boleynsu): implement this
    raise NotImplementedError()

  def ebuild_src_test(self) -> None:
    # TODO(boleynsu): implement this
    raise NotImplementedError()

  def ebuild_src_install(self) -> None:
    # TODO(boleynsu): implement this
    raise NotImplementedError()

  def board_specific_setup(self) -> None:
    # TODO(boleynsu): implement this
    raise NotImplementedError()

  def board_specific_setup_test(self) -> None:
    # TODO(boleynsu): implement this
    raise NotImplementedError()
