# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for android_installer.py"""

import unittest
import unittest.mock

import android_installer


class AndroidInstallerCallerTest(unittest.TestCase):
  """Unit tests for AndroidInstallerCaller"""

  def test_from_str(self):
    self.assertRaises(KeyError,
                      lambda: android_installer.AndroidInstallerCaller.from_str(
                          'invalid_caller'))


class AndroidInstallerTest(unittest.TestCase):
  """Unit tests for AndroidInstaller"""

  def test_env(self):
    # Test --env with '=' in its value
    self.assertEqual(android_installer.AndroidInstaller(
        ['--env', 'a=b=c', '--caller', 'ebuild_src_compile']).env, {'a': 'b=c'})
    # Test --env with no value
    self.assertEqual(android_installer.AndroidInstaller(
        ['--env', 'a=', '--caller', 'ebuild_src_compile']).env, {'a': ''})
    # Test --env with no =
    self.assertRaises(ValueError, lambda: android_installer.AndroidInstaller(
        ['--env', 'a', '--caller', 'ebuild_src_compile']))
    # Test --env with empty key
    self.assertRaises(ValueError, lambda: android_installer.AndroidInstaller(
        ['--env', '=a', '--caller', 'ebuild_src_compile']))
    # Test that the later argument will overwrite the former one.
    self.assertEqual(android_installer.AndroidInstaller(
        ['--env', 'a=1', '--env', 'a=2', '--caller', 'ebuild_src_compile']).env,
                     {'a': '2'})
    # Test when no --env is given.
    self.assertEqual(android_installer.AndroidInstaller(
        ['--caller', 'ebuild_src_compile']).env, {})


  def test_use(self):
    # Test enabling use flag
    self.assertEqual(android_installer.AndroidInstaller(
        ['--use', '+x', '--caller', 'ebuild_src_compile']).use, {'x': True})
    # Test disabling use flag
    self.assertEqual(android_installer.AndroidInstaller(
        ['--use=-x', '--caller', 'ebuild_src_compile']).use, {'x': False})
    # Test invalid use flag
    self.assertRaises(ValueError, lambda: android_installer.AndroidInstaller(
        ['--use', '+x', '--use=*x', '--caller', 'ebuild_src_compile']))
    # Test that the later argument will overwrite the former one.
    self.assertEqual(android_installer.AndroidInstaller(
        ['--use', '+x', '--use=-x', '--caller', 'ebuild_src_compile']).use,
                     {'x': False})
    # Test when no use flags are given.
    self.assertEqual(android_installer.AndroidInstaller(
        ['--caller', 'ebuild_src_compile']).use, {})

  def test_caller(self):
    # Test --caller ebuild_src_compile
    self.assertEqual(
        android_installer.AndroidInstaller(
            ['--caller', 'ebuild_src_compile']).caller,
        android_installer.AndroidInstallerCaller.EBUILD_SRC_COMPILE)
    # Test invalid caller
    self.assertRaises(SystemExit, lambda: android_installer.AndroidInstaller(
        ['--caller', 'invalid_caller']))
    # Test when caller is not specified
    self.assertRaises(ValueError, lambda: android_installer.AndroidInstaller(
        []))

  def test_main(self):
    all_fn = ['ebuild_src_compile', 'ebuild_src_install', 'ebuild_src_test',
              'board_specific_setup', 'board_specific_setup_test']

    def test_called(caller, called_fn):
      mock = unittest.mock.Mock(android_installer.AndroidInstaller)
      mock.caller = android_installer.AndroidInstallerCaller.from_str(caller)
      android_installer.AndroidInstaller.main(mock)
      for fn in all_fn:
        if fn in called_fn:
          mock.__getattr__(fn).assert_called_once()
        else:
          mock.__getattr__(fn).assert_not_called()

    for caller in android_installer.AndroidInstallerCaller.allowed_callers():
      if caller != 'push_to_device':
        self.assertIn(caller, all_fn)
        test_called(caller, [caller])

    test_called('push_to_device', all_fn)


if __name__ == '__main__':
  unittest.main()
