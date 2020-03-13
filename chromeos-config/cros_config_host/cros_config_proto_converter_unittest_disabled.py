#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

import os
import subprocess

import cros_config_proto_converter

from chromite.lib import cros_test_lib

THIS_DIR = os.path.dirname(__file__)

CONFIG_BINARY_PATH = 'generated/config.binaryproto'

PROGRAM_PATH = 'config_test/program/fake'
PROJECT_PATH = 'config_test/project/fake/fake'

PROGRAM_CONFIG_FILE = os.path.join(THIS_DIR, PROGRAM_PATH, CONFIG_BINARY_PATH)
PROJECT_CONFIG_FILE = os.path.join(THIS_DIR, PROJECT_PATH, CONFIG_BINARY_PATH)


class ParseArgsTests(cros_test_lib.TestCase):

  def testParseArgs(self):
    argv = ['-f', 'files-root', '-c', 'project-config',
            '-p', 'program-config', '-o', 'output', ]
    args = cros_config_proto_converter.ParseArgs(argv)
    self.assertEqual(args.files_root, 'files-root')
    self.assertEqual(args.project_config, 'project-config')
    self.assertEqual(args.program_config, 'program-config')
    self.assertEqual(args.output, 'output')


class MainTest(cros_test_lib.TempDirTestCase):

  def testFullTransform(self):
    output_file = os.path.join(self.tempdir, 'output')
    cros_config_proto_converter.Main(project_config=PROJECT_CONFIG_FILE,
                                     program_config=PROGRAM_CONFIG_FILE,
                                     output=output_file,)

    expected_file = os.path.join(THIS_DIR, 'test_data/fake_project.json')
    changed = subprocess.run(
        ['diff', expected_file, output_file]).returncode != 0

    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython3 -m cros_config_host.cros_config_proto_converter '
                 '-c %s '
                 '-p %s '
                 '-o %s ' % (
                     PROJECT_CONFIG_FILE, PROGRAM_CONFIG_FILE, expected_file))

    if changed:
      print(regen_cmd)
      self.fail('Fake project transform does not match')


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
