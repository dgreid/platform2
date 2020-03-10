#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

from __future__ import print_function

import json
import os

import cros_config_test_schema
import libcros_schema

from chromite.lib import cros_test_lib
from chromite.lib import osutils


BASIC_CONFIG = """
mosys-base: &mosys_base_cmds
    name: 'mosys'
    args:
      - 'platform id'
      - 'platform name'
nautilus-mosys-base: &nautilus_mosys_cmds
    name: 'mosys'
    args:
      - 'platform version'
cros-config-base: &cros_config_base_cmds
    name: 'cros-config'
    args:
      - '/ brand-name'
cros-config-lte: &cros_config_lte_cmds
    name: 'cros-config'
    args:
      - '/arc/build-properties device'
chromeos:
  devices:
    - device-name: 'nautilus'
      command-groups:
        - *mosys_base_cmds
        - *nautilus_mosys_cmds
        - *cros_config_base_cmds
    - device-name: 'nautiluslte'
      command-groups:
        - *mosys_base_cmds
        - *nautilus_mosys_cmds
        - *cros_config_base_cmds
        - *cros_config_lte_cmds
"""

this_dir = os.path.dirname(__file__)


class ParseArgsTests(cros_test_lib.TestCase):

  def testParseArgs(self):
    argv = ['-s', 'schema', '-c', 'config', '-f', 'nautilus', '-o', 'output']
    opts = cros_config_test_schema.ParseArgs(argv)
    self.assertEqual(opts.schema, 'schema')
    self.assertEqual(opts.config, 'config')
    self.assertEqual(opts.filter, 'nautilus')
    self.assertEqual(opts.output, 'output')


class TransformConfigTests(cros_test_lib.TestCase):

  def testBasicTransform(self):
    result = cros_config_test_schema.TransformConfig(BASIC_CONFIG)
    json_dict = json.loads(result)
    self.assertEqual(1, len(json_dict))

    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(2, len(json_obj.chromeos.devices))

    device = json_obj.chromeos.devices[0]
    self.assertEqual('nautilus', device.device_name)
    self.assertEqual(3, len(device.command_groups))

    device = json_obj.chromeos.devices[1]
    self.assertEqual('nautiluslte', device.device_name)
    self.assertEqual(4, len(device.command_groups))

  def testTransformConfig_NoMatch(self):
    result = cros_config_test_schema.TransformConfig(
        BASIC_CONFIG, device_filter='abc123')
    json_dict = json.loads(result)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(0, len(json_obj.chromeos.devices))

  def testTransformConfig_FilterMatch(self):
    result = cros_config_test_schema.TransformConfig(
        BASIC_CONFIG, device_filter='nautilus')
    json_dict = json.loads(result)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(1, len(json_obj.chromeos.devices))
    device = json_obj.chromeos.devices[0]
    self.assertEqual('nautilus', device.device_name)
    self.assertEqual(3, len(device.command_groups))


class MainTests(cros_test_lib.TempDirTestCase):

  def testMainImportNoFilter(self):
    output = os.path.join(self.tempdir, 'output.json')
    cros_config_test_schema.Start(
        os.path.join(this_dir, 'test_data/cros_config_test_device.yaml'),
        None,
        output,
        None)
    json_dict = json.loads(osutils.ReadFile(output))
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(2, len(json_obj.chromeos.devices))

    device = json_obj.chromeos.devices[0]
    self.assertEqual('nautilus', device.device_name)
    self.assertEqual(4, len(device.command_groups))

    device = json_obj.chromeos.devices[1]
    self.assertEqual('nautiluslte', device.device_name)
    self.assertEqual(5, len(device.command_groups))

  def testMainImportFilterNautilus(self):
    output = os.path.join(self.tempdir, 'output.json')
    cros_config_test_schema.Start(
        os.path.join(this_dir, 'test_data/cros_config_test_device.yaml'),
        'nautilus',
        output,
        None)
    json_dict = json.loads(osutils.ReadFile(output))
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(1, len(json_obj.chromeos.devices))

    device = json_obj.chromeos.devices[0]
    self.assertEqual('nautilus', device.device_name)
    self.assertEqual(4, len(device.command_groups))

  def testMainImportFilterNautilusLte(self):
    output = os.path.join(self.tempdir, 'output.json')
    cros_config_test_schema.Start(
        os.path.join(this_dir, 'test_data/cros_config_test_device.yaml'),
        'nautiluslte',
        output,
        None)
    json_dict = json.loads(osutils.ReadFile(output))
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(1, len(json_obj.chromeos.devices))

    device = json_obj.chromeos.devices[0]
    self.assertEqual('nautiluslte', device.device_name)
    self.assertEqual(5, len(device.command_groups))


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
