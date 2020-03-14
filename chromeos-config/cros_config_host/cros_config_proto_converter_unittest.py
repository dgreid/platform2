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
from config.test import fake_config


THIS_DIR = os.path.dirname(__file__)

PROGRAM_CONFIG_FILE = fake_config.FAKE_PROGRAM_CONFIG
PROJECT_CONFIG_FILE = fake_config.FAKE_PROJECT_CONFIG


def fakeConfig():
  return cros_config_proto_converter._MergeConfigs([
      cros_config_proto_converter._ReadConfig(PROGRAM_CONFIG_FILE),
      cros_config_proto_converter._ReadConfig(PROJECT_CONFIG_FILE)])


class ParseArgsTests(cros_test_lib.TestCase):

  def testParseArgs(self):
    argv = ['-c', 'config1', 'config2',
            '-p', 'program_config',
            '-o', 'output', ]
    args = cros_config_proto_converter.ParseArgs(argv)
    self.assertEqual(args.project_configs, ['config1', 'config2',])
    self.assertEqual(args.program_config, 'program_config')
    self.assertEqual(args.output, 'output')


class MainTest(cros_test_lib.TempDirTestCase):

  def testFullTransform(self):
    output_file = os.path.join(self.tempdir, 'output')
    cros_config_proto_converter.Main(project_configs=[PROJECT_CONFIG_FILE],
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


class TransformBuildConfigsTest(cros_test_lib.TempDirTestCase):

  def testMissingLookups(self):
    config = fakeConfig()
    config.ClearField('build_targets')

    with self.assertRaisesRegex(Exception, 'Failed to lookup BuildTarget'):
      cros_config_proto_converter._TransformBuildConfigs(config)

  def testEmptyDeviceBrand(self):
    config = fakeConfig()
    config.ClearField('device_brands')

    self.assertIsNotNone(
        cros_config_proto_converter._TransformBuildConfigs(config))

  def testMissingSwConfig(self):
    config = fakeConfig()
    config.designs.value[0].configs[0].ClearField('software_config_id')

    with self.assertRaisesRegex(Exception, 'Software config is required'):
      cros_config_proto_converter._TransformBuildConfigs(config)


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
