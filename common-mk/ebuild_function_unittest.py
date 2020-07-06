#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ebuild_function.py

Default values follow the default values of ebuild (see manpage of ebuild).
https://dev.gentoo.org/~zmedico/portage/doc/man/ebuild.5.html
"""

from __future__ import print_function

import os
import sys

import mock

import ebuild_function

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                '..', '..', '..'))
# pylint: disable=wrong-import-position
from chromite.lib import cros_test_lib
# pylint: enable=wrong-import-position


class DoCommandTests(cros_test_lib.TestCase):
  """Tests of ebuild_function.do_command()."""

  def testDoins(self):
    """Check it returns doins command that contains single source file.

    doins <source_file>
    """
    ret = ebuild_function.do_command('ins', ['source'])
    self.assertEqual(ret, [['doins', 'source']])

  def testMultipleDoins(self):
    """Check it returns doins command that contains multiple source files.

    doins <source_file1> <source_file2>
    """
    ret = ebuild_function.do_command('ins', ['source1', 'source2'])
    self.assertEqual(ret, [['doins', 'source1', 'source2']])

  def testDoinsRecursive(self):
    """Check it returns recursive doins command.

    doins -r <source_file>
    """
    ret = ebuild_function.do_command('ins', ['source'], recursive=True)
    self.assertEqual(ret, [['doins', '-r', 'source']])

  def testNotDoinsRecursive(self):
    """Check "recursive" is disabled in any install_type except for "ins"."""
    ret = ebuild_function.do_command('bin', ['source'], recursive=True)
    self.assertEqual(ret, [['dobin', 'source']])

  def testDoInvalidInstall(self):
    """Check it raises InvalidInstallTypeError when install type is invalid."""
    with self.assertRaises(ebuild_function.InvalidInstallTypeError):
      ebuild_function.do_command('invalid', ['source'])

class NewCommandTests(cros_test_lib.TestCase):
  """Tests of ebuild_function.new_command()."""

  def testNewins(self):
    """Check it returns a newins command that contains single source file."""
    ret = ebuild_function.new_command('ins', ['source'], ['output'])
    self.assertEqual(ret, [['newins', 'source', 'output']])

  def testMultipleNewins(self):
    """Check it returns multiple commands when there are multiple sources."""
    ret = ebuild_function.new_command('ins', ['source1', 'source2'],
                                      ['output1', 'output2'])
    self.assertEqual(ret, [['newins', 'source1', 'output1'],
                           ['newins', 'source2', 'output2']])

  def testDifferentLengthOutput(self):
    """Check it raises error when the number of outputs is not correct.

    Make sure the number of outputs is the same as the number of sources.
    """
    with self.assertRaises(AssertionError):
      ebuild_function.new_command('ins', ['source'], ['output1', 'output2'])

  def testNewInvalidInstall(self):
    """Check it raises InvalidInstallTypeError when install type is invalid."""
    with self.assertRaises(ebuild_function.InvalidInstallTypeError):
      ebuild_function.new_command('invalid', ['source'], ['output'])


class InstallTests(cros_test_lib.TestCase):
  """Tests of ebuild_function.install()."""

  @mock.patch('ebuild_function.sym_install')
  def testCallSymInstall(self, sym_install_mock):
    """Check it calls sym_install when symlinks are specified."""
    ebuild_function.install('sym', ['source'], ['symlink'])
    self.assertTrue(sym_install_mock.called)

  @mock.patch('ebuild_function.do_command')
  def testCallDoCommand(self, do_command_mock):
    """Check it calls do_command when symlinks and outputs aren't specified."""
    ebuild_function.install('ins', ['source'])
    self.assertTrue(do_command_mock.called)

  @mock.patch('ebuild_function.new_command')
  def testCallNewCommand(self, new_command_mock):
    """Check it calls new_command when outputs are specified."""
    ebuild_function.install('ins', ['source'], ['output'])
    self.assertTrue(new_command_mock.called)


class OptionCmdTests(cros_test_lib.TestCase):
  """Tests of ebuild_function.option_cmd()."""

  def testInstallOption(self):
    """Check it returns appropriate options when install_type is "ins"."""
    self.assertEqual(
        ebuild_function.option_cmd('ins'),
        [['insinto', '/'], ['insopts', '-m0644']])
    self.assertEqual(
        ebuild_function.option_cmd('ins', install_path='/etc/init'),
        [['insinto', '/etc/init'], ['insopts', '-m0644']])
    self.assertEqual(
        ebuild_function.option_cmd('ins', options='-m0755'),
        [['insinto', '/'], ['insopts', '-m0755']])

  def testExecutableOption(self):
    """Check it returns appropriate options when install_type is "bin"."""
    self.assertEqual(
        ebuild_function.option_cmd('bin'),
        [['into', '/usr']])
    self.assertEqual(
        ebuild_function.option_cmd('bin', install_path='/'),
        [['into', '/']])

  def testSharedLibraryOption(self):
    """Check it returns appropriate options when install_type is "lib.so"."""
    self.assertEqual(
        ebuild_function.option_cmd('lib.so'),
        [['into', '/usr']])
    self.assertEqual(
        ebuild_function.option_cmd('lib.so', install_path='/'),
        [['into', '/']])

  def testStaticLibraryOption(self):
    """Check it returns appropriate options when install_type is "lib.a"."""
    self.assertEqual(
        ebuild_function.option_cmd('lib.a'),
        [['into', '/usr']])
    self.assertEqual(
        ebuild_function.option_cmd('lib.a', install_path='/'),
        [['into', '/']])

  def testSymlinkOption(self):
    """Check it returns appropriate options when install_type is "sym"."""
    self.assertEqual(
        ebuild_function.option_cmd('sym'),
        [])


class GenerateTests(cros_test_lib.TestCase):
  """Tests of ebuild_function.generate()."""

  def testInstall(self):
    """Check it returns doins commands when command_type isn't specified."""
    self.assertEqual(
        ebuild_function.generate(['source']),
        [['insinto', '/'], ['insopts', '-m0644'], ['doins', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='/usr'),
        [['insinto', '/usr'], ['insopts', '-m0644'], ['doins', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], options='-m0755'),
        [['insinto', '/'], ['insopts', '-m0755'], ['doins', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], recursive=True),
        [['insinto', '/'], ['insopts', '-m0644'], ['doins', '-r', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], outputs=['output']),
        [
            ['insinto', '/'],
            ['insopts', '-m0644'],
            ['newins', 'source', 'output'],
        ])

  def testExecutableInstall(self):
    """Check it returns do(s)bin commands when command_type is "executable"."""
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='bin',
                                 command_type='executable'),
        [['into', '/usr'], ['dobin', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='sbin',
                                 command_type='executable'),
        [['into', '/usr'], ['dosbin', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='/bin',
                                 command_type='executable'),
        [['into', '/'], ['dobin', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='bin',
                                 outputs=['output'],
                                 command_type='executable'),
        [['into', '/usr'], ['newbin', 'source', 'output']])

  def testSharedLibraryInstall(self):
    """Check it returns dolib.so when command_type is shared_library."""
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='lib',
                                 command_type='shared_library'),
        [['into', '/usr'], ['dolib.so', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='/lib',
                                 command_type='shared_library'),
        [['into', '/'], ['dolib.so', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='lib',
                                 outputs=['output'],
                                 command_type='shared_library'),
        [['into', '/usr'], ['newlib.so', 'source', 'output']])

  def testStaticLibraryInstall(self):
    """Check it returns dolib.a commands when command_type is static_library."""
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='lib',
                                 command_type='static_library'),
        [['into', '/usr'], ['dolib.a', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='/lib',
                                 command_type='static_library'),
        [['into', '/'], ['dolib.a', 'source']])
    self.assertEqual(
        ebuild_function.generate(['source'], install_path='lib',
                                 outputs=['output'],
                                 command_type='static_library'),
        [['into', '/usr'], ['newlib.a', 'source', 'output']])

  def testSymlinkInstall(self):
    """Check it returns dosym commands when symlink is specified."""
    self.assertEqual(
        ebuild_function.generate(['source'], symlinks=['symlink']),
        [['dosym', 'source', 'symlink']])
    self.assertEqual(
        ebuild_function.generate(['source'], symlinks=['symlink'],
                                 install_path='/'),
        [['dosym', 'source', '/symlink']])

  def testUnknownTypeInstall(self):
    """Check it raises error when command_type is specified as unknown type."""
    with self.assertRaises(AssertionError):
      ebuild_function.generate(['source'], command_type='something')


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
