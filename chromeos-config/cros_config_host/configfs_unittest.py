#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# pylint: disable=unused-argument

"""Unit tests for ConfigFS data file generator."""

from __future__ import print_function

import functools
import json
import os
import struct
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
  def testFoo(self, config_filename, config, output_dir):
    # do something!
    pass
  """
  def _Decorator(method):
    @functools.wraps(method)
    def _Wrapper(self):
      for filename in args:
        with open(os.path.join(this_dir, '../test_data', filename)) as f:
          config = json.load(f)

        with tempfile.TemporaryDirectory(prefix='test.') as output_dir:
          squashfs_img = os.path.join(output_dir, 'configfs.img')
          configfs.GenerateConfigFSData(config, squashfs_img)
          subprocess.run(['unsquashfs', squashfs_img], check=True,
                         cwd=output_dir, stdout=subprocess.PIPE)
          method(self, filename, config, output_dir)
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
  def testConfigV1FileStructure(self, filename, config, output_dir):
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

  # TODO(jrosenth): remove once we've fully moved over to struct-based
  # identity.
  @TestConfigs('test.json', 'test_arm.json')
  def testConfigV1IdentityJson(self, filename, config, output_dir):
    identity_path = os.path.join(output_dir, 'squashfs-root/v1/identity.json')
    self.assertTrue(os.path.isfile(identity_path))
    with open(identity_path) as f:
      identity_data = json.load(f)
    for device_config, identity_config in zip(
        config['chromeos']['configs'], identity_data['chromeos']['configs']):
      self.assertEqual(set(identity_config.keys()), {'identity'})
      self.assertEqual(device_config['identity'], identity_config['identity'])

  def testConfigIdentityStructSizes(self):
    self.assertEqual(struct.calcsize(configfs.HEADER_FORMAT), 16)
    self.assertEqual(struct.calcsize(configfs.ENTRY_FORMAT), 16)

  @TestConfigs('test.json', 'test_arm.json')
  def testConfigIdentityV0(self, filename, config, output_dir):
    device_configs = config['chromeos']['configs']
    identity_path = os.path.join(output_dir, 'squashfs-root/identity.bin')
    identity_bin = osutils.ReadFile(identity_path, mode='rb')
    version, identity_type, entry_count = struct.unpack_from(
        configfs.HEADER_FORMAT, identity_bin)
    offset = struct.calcsize(configfs.HEADER_FORMAT)
    identity_type = configfs.IdentityType(identity_type)

    self.assertEqual(version, configfs.STRUCT_VERSION)
    if 'arm' in filename:
      self.assertEqual(identity_type, configfs.IdentityType.ARM)
    else:
      self.assertEqual(identity_type, configfs.IdentityType.X86)
    self.assertEqual(len(device_configs), entry_count)

    # Get an entry from the string table.
    def _GetString(offset):
      base = (struct.calcsize(configfs.HEADER_FORMAT)
              + struct.calcsize(configfs.ENTRY_FORMAT) * entry_count)
      string, _, _ = identity_bin[base + offset:].partition(b'\000')
      return string.decode('utf-8')

    for device in device_configs:
      flags, model_match_offset, sku_id, whitelabel_offset = struct.unpack_from(
          configfs.ENTRY_FORMAT, identity_bin, offset=offset)
      offset += struct.calcsize(configfs.ENTRY_FORMAT)

      if identity_type == configfs.IdentityType.X86:
        self.assertEqual(
            flags & configfs.EntryFlags.USES_FIRMWARE_NAME.value, 0)

      if 'smbios-name-match' in device['identity']:
        self.assertEqual(flags & configfs.EntryFlags.HAS_SMBIOS_NAME.value,
                         configfs.EntryFlags.HAS_SMBIOS_NAME.value)
        self.assertEqual(identity_type, configfs.IdentityType.X86)
        self.assertEqual(_GetString(model_match_offset),
                         device['identity']['smbios-name-match'].lower())

      if 'device-tree-compatible-match' in device['identity']:
        self.assertEqual(identity_type, configfs.IdentityType.ARM)
        self.assertEqual(
            _GetString(model_match_offset),
            device['identity']['device-tree-compatible-match'].lower())

      if 'firmware-name' in device['identity']:
        self.assertEqual(flags & configfs.EntryFlags.USES_FIRMWARE_NAME.value,
                         configfs.EntryFlags.USES_FIRMWARE_NAME.value)
        self.assertEqual(identity_type, configfs.IdentityType.ARM)
        self.assertEqual(_GetString(model_match_offset),
                         device['identity']['firmware-name'].lower())
      else:
        self.assertEqual(
            flags & configfs.EntryFlags.USES_FIRMWARE_NAME.value, 0)

      if 'sku-id' in device['identity']:
        self.assertEqual(flags & configfs.EntryFlags.HAS_SKU_ID.value,
                         configfs.EntryFlags.HAS_SKU_ID.value)
        self.assertEqual(sku_id, device['identity']['sku-id'])
      else:
        self.assertEqual(flags & configfs.EntryFlags.HAS_SKU_ID.value, 0)

      if 'whitelabel-tag' in device['identity']:
        self.assertEqual(flags & configfs.EntryFlags.HAS_WHITELABEL.value,
                         configfs.EntryFlags.HAS_WHITELABEL.value)
        self.assertEqual(_GetString(whitelabel_offset),
                         device['identity']['whitelabel-tag'].lower())
      else:
        self.assertEqual(
            flags & configfs.EntryFlags.HAS_WHITELABEL.value, 0)

      if 'customization-id' in device['identity']:
        self.assertEqual(
            flags & configfs.EntryFlags.USES_CUSTOMIZATION_ID.value,
            configfs.EntryFlags.USES_CUSTOMIZATION_ID.value)
        self.assertEqual(_GetString(whitelabel_offset),
                         device['identity']['customization-id'].lower())
      else:
        self.assertEqual(
            flags & configfs.EntryFlags.USES_CUSTOMIZATION_ID.value, 0)

if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
