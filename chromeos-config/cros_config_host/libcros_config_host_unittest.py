#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# pylint: disable=class-missing-docstring

"""The unit test suite for the libcros_config_host.py library"""

from __future__ import print_function

from collections import OrderedDict
from contextlib import contextmanager
from io import StringIO
import copy
import json
import os
import sys
import unittest

from libcros_config_host import CrosConfig
from libcros_config_host_base import BaseFile, SymlinkedFile, FirmwareInfo
from libcros_config_host_base import FirmwareImage, DeviceSignerInfo

YAML_FILE = '../test_data/test.yaml'
MODELS = sorted(['some', 'another', 'whitelabel'])
ANOTHER_BUCKET = ('gs://chromeos-binaries/HOME/bcs-another-private/overlay-'
                  'another-private/chromeos-base/chromeos-firmware-another/')
SOME_BUCKET = ('gs://chromeos-binaries/HOME/bcs-some-private/'
               'overlay-some-private/chromeos-base/chromeos-firmware-some/')
SOME_FIRMWARE_FILES = ['Some_EC.1111.11.1.tbz2',
                       'Some.1111.11.1.tbz2',
                       'Some_RW.1111.11.1.tbz2']
ANOTHER_FIRMWARE_FILES = ['Another_EC.1111.11.1.tbz2',
                          'Another.1111.11.1.tbz2',
                          'Another_RW.1111.11.1.tbz2']

LIB_FIRMWARE = '/lib/firmware/'
TOUCH_FIRMWARE = '/opt/google/touch/firmware/'


# Use this to suppress stdout/stderr output:
# with capture_sys_output() as (stdout, stderr)
#   ...do something...
@contextmanager
def capture_sys_output():
  capture_out, capture_err = StringIO(), StringIO()
  old_out, old_err = sys.stdout, sys.stderr
  try:
    sys.stdout, sys.stderr = capture_out, capture_err
    yield capture_out, capture_err
  finally:
    sys.stdout, sys.stderr = old_out, old_err

def _FormatNamedTuplesDict(value):
  result = copy.deepcopy(value)
  for key, val in result.items():
    result[key] = val._asdict()
  return json.dumps(result, indent=2)


def _FormatListNamedTuplesDict(value):
  result = copy.deepcopy(value)
  for key, values in result.items():
    result[key] = [value._asdict() for value in values]

  return json.dumps(result, indent=2, sort_keys=True)


class CrosConfigHostTest(unittest.TestCase):
  def setUp(self):
    self.filepath = os.path.join(os.path.dirname(__file__), YAML_FILE)

  def _assertEqualsNamedTuplesDict(self, expected, result):
    self.assertEqual(
        _FormatNamedTuplesDict(expected), _FormatNamedTuplesDict(result))

  def _assertEqualsListNamedTuplesDict(self, expected, result):
    self.assertEqual(
        _FormatListNamedTuplesDict(expected),
        _FormatListNamedTuplesDict(result))

  def testGetProperty(self):
    config = CrosConfig(self.filepath)
    another = config.GetConfig('another')
    self.assertEqual(another.GetProperty('/', 'wallpaper'), 'default')
    with self.assertRaises(Exception):
      another.GetProperty('/', 'missing')

  def testModels(self):
    config = CrosConfig(self.filepath)
    models = config.GetModelList()
    for model in MODELS:
      self.assertIn(model, models)

  def testGetFirmwareUris(self):
    config = CrosConfig(self.filepath)
    firmware_uris = config.GetConfig('another').GetFirmwareUris()
    self.assertSequenceEqual(
        firmware_uris,
        sorted([ANOTHER_BUCKET + fname for fname in ANOTHER_FIRMWARE_FILES]))

  def testGetSharedFirmwareUris(self):
    config = CrosConfig(self.filepath)
    firmware_uris = config.GetFirmwareUris()
    expected = sorted(
        [ANOTHER_BUCKET + fname for fname in ANOTHER_FIRMWARE_FILES] +
        [SOME_BUCKET + fname for fname in SOME_FIRMWARE_FILES])
    self.assertSequenceEqual(firmware_uris, expected)

  def testGetArcFiles(self):
    config = CrosConfig(self.filepath)
    arc_files = config.GetArcFiles()
    self.assertEqual(arc_files, [
        BaseFile(
            source='some/camera_characteristics.conf',
            dest='/etc/arc/camera_characteristics.conf'),
        BaseFile(
            source='some/hardware_features.xml',
            dest='/etc/some_hardware_features.xml'),
        BaseFile(
            source='some/media_profiles.xml',
            dest='/etc/some_media_profiles.xml'),
    ])

  def testGetThermalFiles(self):
    config = CrosConfig(self.filepath)
    thermal_files = config.GetThermalFiles()
    self.assertEqual(
        thermal_files,
        [BaseFile('another/dptf.dv', '/etc/dptf/another/dptf.dv'),
         BaseFile('some_notouch/dptf.dv', '/etc/dptf/some_notouch/dptf.dv'),
         BaseFile('some_touch/dptf.dv', '/etc/dptf/some_touch/dptf.dv')])

  def testGetFirmwareBuildTargets(self):
    config = CrosConfig(self.filepath)
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('coreboot'),
                             ['another', 'some'])
    os.environ['FW_NAME'] = 'another'
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('coreboot'),
                             ['another'])
    self.assertSequenceEqual(config.GetFirmwareBuildTargets('ec'),
                             ['another', 'another_base', 'extra1', 'extra2'])
    del os.environ['FW_NAME']

  def testFileTree(self):
    """Test that we can obtain a file tree"""
    config = CrosConfig(self.filepath)
    node = config.GetFileTree()
    self.assertEqual(node.name, '')
    self.assertEqual(sorted(node.children.keys()),
                     ['etc', 'lib', 'opt', 'usr'])
    etc = node.children['etc']
    self.assertEqual(etc.name, 'etc')
    cras = etc.children['cras']
    self.assertEqual(cras.name, 'cras')
    another = cras.children['another']
    self.assertEqual(sorted(another.children.keys()),
                     ['a-card', 'dsp.ini'])

  def testShowTree(self):
    """Test that we can show a file tree"""
    config = CrosConfig(self.filepath)
    tree = config.GetFileTree()
    with capture_sys_output() as (stdout, stderr):
      config.ShowTree('/', tree)
    self.assertEqual(stderr.getvalue(), '')
    lines = [line.strip() for line in stdout.getvalue().splitlines()]
    self.assertEqual(lines[0].split(), ['Size', 'Path'])
    self.assertEqual(lines[1], '/')
    self.assertEqual(lines[2], 'etc/')
    self.assertEqual(lines[3].split(), ['missing', 'cras/'])


  def testFirmwareBuildCombinations(self):
    """Test generating a dict of firmware build combinations."""
    config = CrosConfig(self.filepath)
    expected = OrderedDict(
        [('another', ['another', 'another']),
         ('some', ['some', 'some']),
         ('some2', [None, None])])
    result = config.GetFirmwareBuildCombinations(['coreboot', 'depthcharge'])
    self.assertEqual(result, expected)

    # Unspecified targets should be represented as None.
    expected = OrderedDict(
        [('another', [None]),
         ('some', [None]),
         ('some2', ['experimental/some2'])])
    result = config.GetFirmwareBuildCombinations(['zephyr-ec'])
    self.assertEqual(result, expected)

    os.environ['FW_NAME'] = 'another'
    expected = OrderedDict([('another', ['another', 'another'])])
    result = config.GetFirmwareBuildCombinations(['coreboot', 'depthcharge'])
    self.assertEqual(result, expected)
    del os.environ['FW_NAME']

  def testGetWallpaper(self):
    """Test that we can access the wallpaper information"""
    config = CrosConfig(self.filepath)
    wallpaper = config.GetWallpaperFiles()
    self.assertEqual(['default',
                      'some',
                      'wallpaper-wl1',
                      'wallpaper-wl2'],
                     wallpaper)

  def testGetTouchFirmwareFiles(self):
    def _GetFile(source, symlink):
      """Helper to return a suitable SymlinkedFile"""
      return SymlinkedFile(source, TOUCH_FIRMWARE + source,
                           LIB_FIRMWARE + symlink)

    config = CrosConfig(self.filepath)
    touch_files = config.GetConfig('another').GetTouchFirmwareFiles()
    # pylint: disable=line-too-long
    self.assertEqual(
        touch_files,
        [SymlinkedFile(source='some_stylus_vendor/another-version.hex',
                       dest='/opt/google/touch/firmware/some_stylus_vendor/another-version.hex',
                       symlink='/lib/firmware/some_stylus_vendor_firmware_ANOTHER.bin'),
         SymlinkedFile(source='some_touch_vendor/some-pid_some-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin')])
    touch_files = config.GetConfig('some').GetTouchFirmwareFiles()

    # This checks that duplicate processing works correct, since both models
    # have the same wacom firmware
    self.assertEqual(
        touch_files,
        [SymlinkedFile(source='some_stylus_vendor/some-version.hex',
                       dest='/opt/google/touch/firmware/some_stylus_vendor/some-version.hex',
                       symlink='/lib/firmware/some_stylus_vendor_firmware_SOME.bin'),
         SymlinkedFile(source='some_touch_vendor/some-pid_some-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin'),
         SymlinkedFile(source='some_touch_vendor/some-other-pid_some-other-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-other-pid_some-other-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-other-pid.bin')])
    touch_files = config.GetTouchFirmwareFiles()
    expected = set(
        [SymlinkedFile(source='some_stylus_vendor/another-version.hex',
                       dest='/opt/google/touch/firmware/some_stylus_vendor/another-version.hex',
                       symlink='/lib/firmware/some_stylus_vendor_firmware_ANOTHER.bin'),
         SymlinkedFile(source='some_stylus_vendor/some-version.hex',
                       dest='/opt/google/touch/firmware/some_stylus_vendor/some-version.hex',
                       symlink='/lib/firmware/some_stylus_vendor_firmware_SOME.bin'),
         SymlinkedFile(source='some_touch_vendor/some-pid_some-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin'),
         SymlinkedFile(source='some_touch_vendor/some-other-pid_some-other-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-other-pid_some-other-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-other-pid.bin'),
         SymlinkedFile(source='some_touch_vendor/some-pid_some-version.bin',
                       dest='/opt/google/touch/firmware/some_touch_vendor/some-pid_some-version.bin',
                       symlink='/lib/firmware/some_touch_vendorts_i2c_some-pid.bin')])
    self.assertEqual(set(touch_files), expected)

  def testGetAudioFiles(self):
    config = CrosConfig(self.filepath)
    audio_files = config.GetAudioFiles()
    expected = \
      [BaseFile(source='cras-config/another/dsp.ini',
                dest='/etc/cras/another/dsp.ini'),
       BaseFile(source='cras-config/another/a-card',
                dest='/etc/cras/another/a-card'),
       BaseFile(source='cras-config/some/dsp.ini',
                dest='/etc/cras/some/dsp.ini'),
       BaseFile(source='cras-config/some/a-card',
                dest='/etc/cras/some/a-card'),
       BaseFile(source='cras-config/some2/dsp.ini',
                dest='/etc/cras/some2/dsp.ini'),
       BaseFile(source='cras-config/some2/a-card',
                dest='/etc/cras/some2/a-card'),
       BaseFile(source='topology/another-tplg.bin',
                dest='/lib/firmware/another-tplg.bin'),
       BaseFile(source='topology/some-tplg.bin',
                dest='/lib/firmware/some-tplg.bin'),
       BaseFile(source='ucm-config/a-card.another/HiFi.conf',
                dest='/usr/share/alsa/ucm/a-card.another/HiFi.conf'),
       BaseFile(source='ucm-config/a-card.another/a-card.another.conf',
                dest='/usr/share/alsa/ucm/a-card.another/a-card.another.conf'),
       BaseFile(source='ucm-config/a-card.some/HiFi.conf',
                dest='/usr/share/alsa/ucm/a-card.some/HiFi.conf'),
       BaseFile(source='ucm-config/a-card.some/a-card.some.conf',
                dest='/usr/share/alsa/ucm/a-card.some/a-card.some.conf'),
       BaseFile(source='ucm-config/a-card.some2/HiFi.conf',
                dest='/usr/share/alsa/ucm/a-card.some2/HiFi.conf'),
       BaseFile(source='ucm-config/a-card.some2/a-card.some2.conf',
                dest='/usr/share/alsa/ucm/a-card.some2/a-card.some2.conf')]

    self.assertEqual(audio_files, sorted(expected))

  def testFirmware(self):
    """Test access to firmware information"""
    expected = OrderedDict(
        [('another',
          FirmwareInfo(model='another',
                       shared_model='another',
                       key_id='ANOTHER',
                       have_image=True,
                       bios_build_target='another',
                       ec_build_target='another',
                       main_image_uri='bcs://Another.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Another_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Another_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='another',
                       brand_code='')),
         ('some',
          FirmwareInfo(model='some',
                       shared_model='some',
                       key_id='SOME',
                       have_image=True,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='some',
                       brand_code='')),
         ('some2',
          FirmwareInfo(model='some2',
                       shared_model='some2',
                       key_id='SOME',
                       have_image=True,
                       bios_build_target=None,
                       ec_build_target=None,
                       main_image_uri='',
                       main_rw_image_uri='',
                       ec_image_uri='',
                       pd_image_uri='',
                       sig_id='some2',
                       brand_code='')),
         ('whitelabel',
          FirmwareInfo(model='whitelabel',
                       shared_model='some',
                       key_id='WHITELABEL1',
                       have_image=True,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='sig-id-in-customization-id',
                       brand_code='')),
         ('whitelabel-whitelabel1',
          FirmwareInfo(model='whitelabel-whitelabel1',
                       shared_model='some',
                       key_id='WHITELABEL1',
                       have_image=False,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='whitelabel-whitelabel1',
                       brand_code='WLBA')),
         ('whitelabel-whitelabel2',
          FirmwareInfo(model='whitelabel-whitelabel2',
                       shared_model='some',
                       key_id='WHITELABEL2',
                       have_image=False,
                       bios_build_target='some',
                       ec_build_target='some',
                       main_image_uri='bcs://Some.1111.11.1.tbz2',
                       main_rw_image_uri='bcs://Some_RW.1111.11.1.tbz2',
                       ec_image_uri='bcs://Some_EC.1111.11.1.tbz2',
                       pd_image_uri='',
                       sig_id='whitelabel-whitelabel2',
                       brand_code='WLBB'))])
    result = CrosConfig(self.filepath).GetFirmwareInfo()
    self._assertEqualsNamedTuplesDict(expected, result)

  def testFirmwareConfigs(self):
    """Test access to firmware configs."""
    expected = {
        'some': [
            FirmwareImage(
                type='ap',
                build_target='some',
                image_uri='bcs://Some.1111.11.1.tbz2'),
            FirmwareImage(
                type='rw',
                build_target='some',
                image_uri='bcs://Some_RW.1111.11.1.tbz2'),
            FirmwareImage(
                type='ec',
                build_target='some',
                image_uri='bcs://Some_EC.1111.11.1.tbz2')
        ],
        'another': [
            FirmwareImage(
                type='ap',
                build_target='another',
                image_uri='bcs://Another.1111.11.1.tbz2'),
            FirmwareImage(
                type='rw',
                build_target='another',
                image_uri='bcs://Another_RW.1111.11.1.tbz2'),
            FirmwareImage(
                type='ec',
                build_target='another',
                image_uri='bcs://Another_EC.1111.11.1.tbz2')
        ],
        'some2': [
        ],
    }

    result = CrosConfig(self.filepath).GetFirmwareConfigs()
    self._assertEqualsListNamedTuplesDict(expected, result)

  def testFirmwareConfigsByDevice(self):
    """Test access to firmware config names."""
    expected = {
        'some': 'some',
        'some2': 'some2',
        'another': 'another',
        'whitelabel': 'some',
        'whitelabel-whitelabel1': 'some',
        'whitelabel-whitelabel2': 'some',
    }

    result = CrosConfig(self.filepath).GetFirmwareConfigsByDevice()
    self.assertEqual(result, expected)

  def testSignerInfoByDevice(self):
    """Test access to device signer info."""
    expected = {
        'whitelabel-whitelabel2':
            DeviceSignerInfo(
                key_id='WHITELABEL2', sig_id='whitelabel-whitelabel2'),
        'whitelabel-whitelabel1':
            DeviceSignerInfo(
                key_id='WHITELABEL1', sig_id='whitelabel-whitelabel1'),
        'some':
            DeviceSignerInfo(key_id='SOME', sig_id='some'),
        'some2':
            DeviceSignerInfo(key_id='SOME', sig_id='some2'),
        'whitelabel':
            DeviceSignerInfo(
                key_id='WHITELABEL1', sig_id='sig-id-in-customization-id'),
        'another':
            DeviceSignerInfo(key_id='ANOTHER', sig_id='another')
    }

    result = CrosConfig(self.filepath).GetDeviceSignerInfo()
    self.assertEqual(result, expected)


if __name__ == '__main__':
  unittest.main()
