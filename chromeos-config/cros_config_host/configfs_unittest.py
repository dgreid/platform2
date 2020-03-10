#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ConfigFS data file generator."""

from __future__ import print_function

import functools
import json
import os
import subprocess
import tempfile
import configfs

from chromite.lib import cros_test_lib
from chromite.lib import osutils

this_dir = os.path.dirname(__file__)


def TestConfigs(*args):
  """Wrapper function for tests which use configs from libcros_config/

  Use like so:
  @TestConfigs('test.json', [any other files you want...])
  def testFoo(self, config, output_dir):
    # do something!
    pass
  """
  def _Decorator(method):
    @functools.wraps(method)
    def _Wrapper(self):
      for fn in args:
        with open(os.path.join(this_dir, '../libcros_config', fn)) as f:
          config = json.load(f)

        with tempfile.TemporaryDirectory(prefix='test.') as output_dir:
          squashfs_img = os.path.join(output_dir, 'configfs.img')
          configfs.GenerateConfigFSData(config, squashfs_img)
          subprocess.run(['unsquashfs', squashfs_img], check=True,
                         cwd=output_dir, stdout=subprocess.PIPE)
          method(self, config, output_dir)
    return _Wrapper
  return _Decorator


class ConfigFSTests(cros_test_lib.TestCase):
  """Tests for ConfigFS."""

  def testSerialize(self):
    self.assertEqual(configfs.Serialize(True), b'true')
    self.assertEqual(configfs.Serialize(False), b'false')
    self.assertEqual(configfs.Serialize(10), b'10')
    self.assertEqual(configfs.Serialize('hello💩'), b'hello\xf0\x9f\x92\xa9')
    self.assertEqual(configfs.Serialize(b'\xff\xff\xff'), b'\xff\xff\xff')

  @TestConfigs('test.json', 'test_arm.json')
  def testConfigV1FileStructure(self, config, output_dir):
    def _CheckConfigRec(config, path):
      if isinstance(config, dict):
        iterator = config.items()
      elif isinstance(config, list):
        iterator = enumerate(config)
      else:
        self.assertTrue(os.path.isfile(path))
        self.assertEqual(osutils.ReadFile(path, mode='rb'),
                         configfs.Serialize(config))
        return
      self.assertTrue(os.path.isdir(path))
      for name, entry in iterator:
        childpath = os.path.join(path, str(name))
        _CheckConfigRec(entry, childpath)
    _CheckConfigRec(config, os.path.join(output_dir, 'squashfs-root/v1'))

  @TestConfigs('test.json', 'test_arm.json')
  def testConfigV1Identity(self, config, output_dir):
    identity_path = os.path.join(output_dir, 'squashfs-root/v1/identity.json')
    self.assertTrue(os.path.isfile(identity_path))
    with open(identity_path) as f:
      identity_data = json.load(f)
    for device_config, identity_config in zip(
        config['chromeos']['configs'], identity_data['chromeos']['configs']):
      self.assertEqual(set(identity_config.keys()), {'identity'})
      self.assertEqual(device_config['identity'], identity_config['identity'])


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
