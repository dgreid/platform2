#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

from __future__ import print_function

import json
import os
import re

import jsonschema  # pylint: disable=import-error
from six.moves import zip_longest
import yaml  # pylint: disable=import-error
from packaging import version  # pylint: disable=import-error

import cros_config_schema
import libcros_schema

from chromite.lib import cros_test_lib

this_dir = os.path.dirname(__file__)

BASIC_CONFIG = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'

chromeos:
  devices:
    - $name: 'basking'
      products:
        - $key-id: 'OEM2'
          $brand-code: 'ASUN'
      skus:
        - $sku-id: 0
          config:
            audio:
              main:
                $card: 'bxtda7219max'
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
                files:
                  - source: "{{$dsp-ini}}"
                    destination: "/etc/cras/{{$dsp-ini}}"
                    $dsp-ini: "{{cras-config-dir}}/dsp.ini"
            brand-code: '{{$brand-code}}'
            identity:
              platform-name: "Reef"
              smbios-name-match: "Reef"
              sku-id: "{{$sku-id}}"
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
            test-label: 'reef'
"""


class MergeDictionaries(cros_test_lib.TestCase):

  def testBaseKeyMerge(self):
    primary = {'a': {'b': 1, 'c': 2}}
    overlay = {'a': {'c': 3}, 'b': 4}
    cros_config_schema.MergeDictionaries(primary, overlay)
    self.assertEqual({'a': {'b': 1, 'c': 3}, 'b': 4}, primary)

  def testBaseListAppend(self):
    primary = {'a': {'b': 1, 'c': [1, 2]}}
    overlay = {'a': {'c': [3, 4]}}
    cros_config_schema.MergeDictionaries(primary, overlay)
    self.assertEqual({'a': {'b': 1, 'c': [1, 2, 3, 4]}}, primary)


class ParseArgsTests(cros_test_lib.TestCase):

  def testParseArgs(self):
    argv = ['-s', 'schema', '-c', 'config', '-o', 'output', '-f', 'True']
    args = cros_config_schema.ParseArgs(argv)
    self.assertEqual(args.schema, 'schema')
    self.assertEqual(args.config, 'config')
    self.assertEqual(args.output, 'output')
    self.assertTrue(args.filter)

  def testParseArgsForConfigs(self):
    argv = ['-o', 'output', '-m', 'm1', 'm2', 'm3']
    args = cros_config_schema.ParseArgs(argv)
    self.assertEqual(args.output, 'output')
    self.assertEqual(args.configs, ['m1', 'm2', 'm3'])


class TransformConfigTests(cros_test_lib.TestCase):

  def testBasicTransform(self):
    result = cros_config_schema.TransformConfig(BASIC_CONFIG)
    json_dict = json.loads(result)
    self.assertEqual(len(json_dict), 1)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(1, len(json_obj.chromeos.configs))
    model = json_obj.chromeos.configs[0]
    self.assertEqual('basking', model.name)
    self.assertEqual('basking', model.audio.main.cras_config_dir)
    # Check multi-level template variable evaluation
    self.assertEqual('/etc/cras/basking/dsp.ini',
                     model.audio.main.files[0].destination)

  def testTransformConfig_NoMatch(self):
    result = cros_config_schema.TransformConfig(
        BASIC_CONFIG, model_filter_regex='abc123')
    json_dict = json.loads(result)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(0, len(json_obj.chromeos.configs))

  def testTransformConfig_FilterMatch(self):
    scoped_config = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'
chromeos:
  devices:
    - $name: 'foo'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
    - $name: 'bar'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
"""

    result = cros_config_schema.TransformConfig(
        scoped_config, model_filter_regex='bar')
    json_dict = json.loads(result)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    self.assertEqual(1, len(json_obj.chromeos.configs))
    model = json_obj.chromeos.configs[0]
    self.assertEqual('bar', model.name)

  def testTemplateVariableScope(self):
    scoped_config = """
audio_common: &audio_common
  main:
    $ucm: "default"
    $cras: "default"
    ucm-suffix: "{{$ucm}}"
    cras-config-dir: "{{$cras}}"
chromeos:
  devices:
    - $name: "some"
      $ucm: "overridden-by-device-scope"
      products:
        - $key-id: 'SOME-KEY'
          $brand-code: 'SOME-BRAND'
          $cras: "overridden-by-product-scope"
      skus:
        - $sku-id: 0
          config:
            audio: *audio_common
            brand-code: '{{$brand-code}}'
            identity:
              platform-name: "Some"
              smbios-name-match: "Some"
            name: '{{$name}}'
            firmware:
              no-firmware: True
"""
    result = cros_config_schema.TransformConfig(scoped_config)
    json_dict = json.loads(result)
    json_obj = libcros_schema.GetNamedTuple(json_dict)
    config = json_obj.chromeos.configs[0]
    self.assertEqual(
        'overridden-by-product-scope', config.audio.main.cras_config_dir)
    self.assertEqual(
        'overridden-by-device-scope', config.audio.main.ucm_suffix)


class ValidateConfigSchemaTests(cros_test_lib.TestCase):

  def setUp(self):
    self._schema = cros_config_schema.ReadSchema()

  def testBasicSchemaValidation(self):
    libcros_schema.ValidateConfigSchema(
        self._schema, cros_config_schema.TransformConfig(BASIC_CONFIG))

  def testMissingRequiredElement(self):
    config = re.sub(r' *cras-config-dir: .*', '', BASIC_CONFIG)
    config = re.sub(r' *volume: .*', '', BASIC_CONFIG)
    try:
      libcros_schema.ValidateConfigSchema(
          self._schema, cros_config_schema.TransformConfig(config))
    except jsonschema.ValidationError as err:
      self.assertIn('required', err.__str__())
      self.assertIn('cras-config-dir', err.__str__())

  def testReferencedNonExistentTemplateVariable(self):
    config = re.sub(r' *$card: .*', '', BASIC_CONFIG)
    try:
      libcros_schema.ValidateConfigSchema(
          self._schema, cros_config_schema.TransformConfig(config))
    except cros_config_schema.ValidationError as err:
      self.assertIn('Referenced template variable', err.__str__())
      self.assertIn('cras-config-dir', err.__str__())

  def testSkuIdOutOfBound(self):
    config = BASIC_CONFIG.replace('$sku-id: 0', '$sku-id: 0x80000000')
    with self.assertRaises(jsonschema.ValidationError) as ctx:
      libcros_schema.ValidateConfigSchema(
          self._schema, cros_config_schema.TransformConfig(config))
    if version.parse(jsonschema.__version__) >= version.Version('3.0.0'):
      self.assertIn(
          '%i is greater than the maximum' % 0x80000000, str(ctx.exception))
      self.assertIn('sku-id', str(ctx.exception))
    else:
      self.assertIn("'sku-id': %i" % 0x80000000, str(ctx.exception))
      self.assertIn('is not valid', str(ctx.exception))


class ValidateFingerprintSchema(cros_test_lib.TestCase):

  def setUp(self):
    self._schema = cros_config_schema.ReadSchema()

  def testROVersion(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'foo',
                              'sku-id': 1},
                 'name': 'foo',
                 'fingerprint': {
                     'board': 'dartmonkey',
                     'ro-version': '123'}
                 },
            ],
        },
    }
    libcros_schema.ValidateConfigSchema(self._schema,
                                        libcros_schema.FormatJson(config))

  def testROVersionMissingBoardName(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'foo',
                              'sku-id': 1},
                 'name': 'foo',
                 'fingerprint': {
                     # "ro-version" only allowed if "board" is also specified.
                     'ro-version': '123'}
                 },
            ],
        },
    }
    with self.assertRaises(jsonschema.exceptions.ValidationError) as ctx:
      libcros_schema.ValidateConfigSchema(self._schema,
                                          libcros_schema.FormatJson(config))

    self.assertEqual(ctx.exception.message,
                     "'board' is a dependency of 'ro-version'")


class ValidateCameraSchema(cros_test_lib.TestCase):

  def setUp(self):
    self._schema = cros_config_schema.ReadSchema()

  def testDevices(self):
    config = {
        'chromeos': {
            'configs': [
                {
                    'identity': {'platform-name': 'foo', 'sku-id': 1},
                    'name': 'foo',
                    'camera': {
                        'count': 2,
                        'devices': [
                            {
                                'id': '0123:abcd',
                                'interface': 'usb',
                                'facing': 'front',
                                'orientation': 180,
                            },
                            {
                                'id': 'mipi-cam',
                                'interface': 'mipi',
                                'facing': 'back',
                                'orientation': 0,
                            },
                        ],
                    }
                },
            ],
        },
    }
    libcros_schema.ValidateConfigSchema(self._schema,
                                        libcros_schema.FormatJson(config))

  def testInvalidUsbId(self):
    if version.parse(jsonschema.__version__) < version.Version('3.0.0'):
      self.skipTest('jsonschema needs upgrade to support conditionals')

    for invalid_usb_id in ('0123-abcd', '0123:Abcd', '123:abcd'):
      config = {
          'chromeos': {
              'configs': [
                  {
                      'identity': {'platform-name': 'foo', 'sku-id': 1},
                      'name': 'foo',
                      'camera': {
                          'count': 1,
                          'devices': [
                              {
                                  'id': invalid_usb_id,
                                  'interface': 'usb',
                                  'facing': 'front',
                                  'orientation': 0,
                              },
                          ],
                      }
                  },
              ],
          },
      }
      with self.assertRaises(jsonschema.ValidationError) as ctx:
        libcros_schema.ValidateConfigSchema(self._schema,
                                            libcros_schema.FormatJson(config))
      self.assertIn('%r does not match' % invalid_usb_id, str(ctx.exception))


WHITELABEL_CONFIG = """
chromeos:
  devices:
    - $name: 'whitelabel'
      products:
        - $key-id: 'DEFAULT'
          $wallpaper: 'DEFAULT_WALLPAPER'
          $regulatory-label: 'DEFAULT_LABEL'
          $whitelabel-tag: ''
          $brand-code: 'DEFAULT_BRAND_CODE'
          $stylus-category: 'none'
          $test-label: 'DEFAULT_TEST_LABEL'
        - $key-id: 'WL1'
          $wallpaper: 'WL1_WALLPAPER'
          $regulatory-label: 'WL1_LABEL'
          $whitelabel-tag: 'WL1_TAG'
          $brand-code: 'WL1_BRAND_CODE'
          $stylus-category: 'none'
          $test-label: 'WL1_TEST_LABEL'
        - $key-id: 'WL2'
          $wallpaper: 'WL2_WALLPAPER'
          $regulatory-label: 'WL2_LABEL'
          $whitelabel-tag: 'WL2_TAG'
          $brand-code: 'WL2_BRAND_CODE'
          $stylus-category: 'external'
          $test-label: 'WL2_TEST_LABEL'
      skus:
        - config:
            identity:
              sku-id: 0
              whitelabel-tag: '{{$whitelabel-tag}}'
            name: '{{$name}}'
            brand-code: '{{$brand-code}}'
            wallpaper: '{{$wallpaper}}'
            regulatory-label: '{{$regulatory-label}}'
            hardware-properties:
              stylus-category: '{{$stylus-category}}'
"""

INVALID_WHITELABEL_CONFIG = """
            # THIS WILL CAUSE THE FAILURE
            test-label: '{{$test-label}}'
"""


class ValidateConfigTests(cros_test_lib.TestCase):

  def testBasicValidation(self):
    cros_config_schema.ValidateConfig(
        cros_config_schema.TransformConfig(BASIC_CONFIG))

  def testIdentitiesNotUnique(self):
    config = """
reef-9042-fw: &reef-9042-fw
  bcs-overlay: 'overlay-reef-private'
  ec-ro-image: 'Reef_EC.9042.87.1.tbz2'
  main-ro-image: 'Reef.9042.87.1.tbz2'
  main-rw-image: 'Reef.9042.110.0.tbz2'
  build-targets:
    coreboot: 'reef'
chromeos:
  devices:
    - $name: 'astronaut'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
    - $name: 'astronaut'
      products:
        - $key-id: 'OEM2'
      skus:
        - config:
            identity:
              sku-id: 0
            audio:
              main:
                cras-config-dir: '{{$name}}'
                ucm-suffix: '{{$name}}'
            name: '{{$name}}'
            firmware: *reef-9042-fw
            firmware-signing:
              key-id: '{{$key-id}}'
              signature-id: '{{$name}}'
"""
    with self.assertRaises(cros_config_schema.ValidationError) as ctx:
      cros_config_schema.ValidateConfig(
          cros_config_schema.TransformConfig(config))
    self.assertIn('Identities are not unique', str(ctx.exception))

  def testWhitelabelWithExternalStylus(self):
    config = WHITELABEL_CONFIG
    cros_config_schema.ValidateConfig(
        cros_config_schema.TransformConfig(config))

  def testWhitelabelWithOtherThanBrandChanges(self):
    config = WHITELABEL_CONFIG + INVALID_WHITELABEL_CONFIG
    with self.assertRaises(cros_config_schema.ValidationError) as ctx:
      cros_config_schema.ValidateConfig(
          cros_config_schema.TransformConfig(config))
    self.assertIn('Whitelabel configs can only', str(ctx.exception))

  def testHardwarePropertiesInvalid(self):
    config = \
"""
chromeos:
  devices:
    - $name: 'bad_device'
      skus:
        - config:
            identity:
              sku-id: 0
            # THIS WILL CAUSE THE FAILURE
            hardware-properties:
              has-base-accelerometer: true
              has-base-gyroscope: 7
              has-lid-accelerometer: false
              is-lid-convertible: false
              has-base-magnetometer: false
              has-touchscreen: true
"""
    try:
      cros_config_schema.ValidateConfig(
          cros_config_schema.TransformConfig(config))
    except cros_config_schema.ValidationError as err:
      self.assertIn('must be boolean', err.__str__())
    else:
      self.fail('ValidationError not raised')

  def testHardwarePropertiesBoolean(self):
    config = \
"""
chromeos:
  devices:
    - $name: 'good_device'
      skus:
        - config:
            identity:
              sku-id: 0
            hardware-properties:
              has-base-accelerometer: true
              has-base-gyroscope: true
              has-lid-accelerometer: true
              is-lid-convertible: false
              has-base-magnetometer: true
              has-touchscreen: false
"""
    cros_config_schema.ValidateConfig(
        cros_config_schema.TransformConfig(config))

  def testMultipleMosysPlatformsInvalid(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'SomePlatform',
                              'sku-id': 1}},
                {'identity': {'platform-name': 'SomePlatform',
                              'sku-id': 2}},
                # This causes the ValidationError.
                {'identity': {'platform-name': 'AnotherPlatform',
                              'sku-id': 3}},
            ],
        },
    }
    with self.assertRaises(cros_config_schema.ValidationError):
      cros_config_schema.ValidateConfig(json.dumps(config))

    # Removing the offending config should clear the ValidationError.
    config['chromeos']['configs'].pop()
    try:
      cros_config_schema.ValidateConfig(json.dumps(config))
    except cros_config_schema.ValidationError:
      self.fail('Removing the offending config should have cleared the '
                'ValidationError.')

  def testMultipleFingerprintFirmwareROVersionInvalid(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'foo',
                              'sku-id': 1},
                 'fingerprint': {
                     'board': 'bloonchipper',
                     'ro-version': '123'}
                 },
                {'identity': {'platform-name': 'foo',
                              'sku-id': 2},
                 'fingerprint': {
                     'board': 'bloonchipper',
                     'ro-version': '123'}
                 },
                # This causes the ValidationError.
                {'identity': {'platform-name': 'foo',
                              'sku-id': 3},
                 'fingerprint': {
                     'board': 'bloonchipper',
                     'ro-version': '456'}
                 }
            ],
        },
    }
    with self.assertRaises(cros_config_schema.ValidationError) as ctx:
      cros_config_schema.ValidateConfig(json.dumps(config))

    self.assertRegex(str(ctx.exception), re.compile(
        'You may not use different fingerprint firmware RO versions on the '
        'same board:.*'))

  def testMultipleFingerprintFirmwareROVersionsValid(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'foo',
                              'sku-id': 1},
                 'fingerprint': {
                     'board': 'bloonchipper',
                     'ro-version': '123'}
                 },
                {'identity': {'platform-name': 'foo',
                              'sku-id': 2},
                 'fingerprint': {
                     'board': 'dartmonkey',
                     'ro-version': '456'}
                 },
            ],
        },
    }
    cros_config_schema.ValidateConfig(json.dumps(config))

  def testFingerprintFirmwareROVersionsValid(self):
    config = {
        'chromeos': {
            'configs': [
                {'identity': {'platform-name': 'foo',
                              'sku-id': 1},
                 'fingerprint': {
                     'ro-version': '123'}
                 },
                # This device does not have fingerprint
                {'identity': {'platform-name': 'foo',
                              'sku-id': 2},
                 },
            ],
        },
    }
    cros_config_schema.ValidateConfig(json.dumps(config))


class FilterBuildElements(cros_test_lib.TestCase):

  def testBasicFilterBuildElements(self):
    json_dict = json.loads(
        cros_config_schema.FilterBuildElements(
            cros_config_schema.TransformConfig(BASIC_CONFIG), ['/firmware']))
    self.assertNotIn('firmware', json_dict['chromeos']['configs'][0])


class GetValidSchemaProperties(cros_test_lib.TestCase):

  def testGetValidSchemaProperties(self):
    schema_props = cros_config_schema.GetValidSchemaProperties()
    self.assertIn('cras-config-dir', schema_props['/audio/main'])
    self.assertIn('key-id', schema_props['/firmware-signing'])
    self.assertIn('files', schema_props['/audio/main'])
    self.assertIn('has-touchscreen', schema_props['/hardware-properties'])
    self.assertIn('count', schema_props['/camera'])


class MainTests(cros_test_lib.TempDirTestCase):

  def _GetSchemaYaml(self):
    with open(os.path.join(
        this_dir, 'cros_config_schema.yaml')) as schema_stream:
      schema_contents = schema_stream.read()
      return yaml.load(schema_contents, Loader=yaml.SafeLoader)

  def assertFileEqual(self, file_expected, file_actual, regen_cmd=''):
    self.assertTrue(os.path.isfile(file_expected),
                    'Expected file does not exist at path: {}'
                    .format(file_expected))

    self.assertTrue(os.path.isfile(file_actual),
                    'Actual file does not exist at path: {}'
                    .format(file_actual))

    with open(file_expected, 'r') as expected, open(file_actual, 'r') as actual:
      for line_num, (line_expected, line_actual) in \
          enumerate(zip_longest(expected, actual)):
        self.assertEqual(line_expected, line_actual, \
           ('Files differ at line {0}\n'
            'Expected: {1}\n'
            'Actual  : {2}\n'
            'Path of expected output file: {3}\n'
            'Path of actual output file: {4}\n'
            '{5}').format(line_num, repr(line_expected), repr(line_actual),
                          file_expected, file_actual, regen_cmd))

  def assertMultilineStringEqual(self, str_expected, str_actual):
    expected = str_expected.strip().split('\n')
    actual = str_actual.strip().split('\n')
    for line_num, (line_expected, line_actual) in \
        enumerate(zip_longest(expected, actual)):
      self.assertEqual(line_expected, line_actual, \
         ('Strings differ at line {0}\n'
          'Expected: {1}\n'
          'Actual  : {2}\n').format(line_num, repr(line_expected),
                                    repr(line_actual)))

  def testMainWithExampleWithBuildAndMosysCBindings(self):
    json_output = os.path.join(self.tempdir, 'output.json')
    c_output = os.path.join(self.tempdir, 'config.c')
    cros_config_schema.Main(
        None,
        os.path.join(this_dir, '../test_data/test.yaml'),
        json_output,
        gen_c_output_dir=self.tempdir)
    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython -m cros_config_host.cros_config_schema '
                 '-c test_data/test.yaml '
                 '-o test_data/test_build.json '
                 '-g test_data')

    expected_json_file = \
            os.path.join(this_dir, '../test_data/test_build.json')
    self.assertFileEqual(expected_json_file, json_output, regen_cmd)

    expected_c_file = os.path.join(this_dir, '../test_data/test.c')
    self.assertFileEqual(expected_c_file, c_output, regen_cmd)

  def testMainWithExampleWithoutBuild(self):
    output = os.path.join(self.tempdir, 'output')
    cros_config_schema.Main(
        None,
        os.path.join(this_dir, '../test_data/test.yaml'),
        output,
        filter_build_details=True)

    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython -m cros_config_host.cros_config_schema '
                 '-f True '
                 '-c test_data/test.yaml '
                 '-o test_data/test.json')

    expected_file = os.path.join(this_dir, '../test_data/test.json')
    self.assertFileEqual(expected_file, output, regen_cmd)

  def testMainArmExample(self):
    json_output = os.path.join(self.tempdir, 'output.json')
    c_output = os.path.join(self.tempdir, 'config.c')
    cros_config_schema.Main(
        None,
        os.path.join(this_dir, '../test_data/test_arm.yaml'),
        json_output,
        filter_build_details=True,
        gen_c_output_dir=self.tempdir)
    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython -m cros_config_host.cros_config_schema '
                 '-f True '
                 '-c test_data/test_arm.yaml '
                 '-o test_data/test_arm.json '
                 '-g test_data')

    expected_json_file = \
            os.path.join(this_dir, '../test_data/test_arm.json')
    self.assertFileEqual(expected_json_file, json_output, regen_cmd)

    expected_c_file = os.path.join(this_dir, '../test_data/test_arm.c')
    self.assertFileEqual(expected_c_file, c_output, regen_cmd)

  def testMainImportExample(self):
    output = os.path.join(self.tempdir, 'output')
    cros_config_schema.Main(
        None,
        os.path.join(this_dir, '../test_data/test_import.yaml'),
        output)
    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython -m cros_config_host.cros_config_schema '
                 '-c test_data/test_import.yaml '
                 '-o test_data/test_import.json')
    expected_file = os.path.join(this_dir, '../test_data/test_import.json')
    self.assertFileEqual(expected_file, output, regen_cmd)

  def testMainMergeExample(self):
    output = os.path.join(self.tempdir, 'output')
    base_path = os.path.join(this_dir, '../test_data')
    cros_config_schema.Main(
        None,
        None,
        output,
        configs=[os.path.join(base_path, 'test_merge_base.yaml'),
                 os.path.join(base_path, 'test_merge_overlay.yaml')])
    regen_cmd = ('To regenerate the expected output, run:\n'
                 '\tpython -m cros_config_host.cros_config_schema '
                 '-o test_data/test_merge.json '
                 '-m test_data/test_merge_base.yaml '
                 'test_data/test_merge_overlay.yaml')
    expected_file = os.path.join(this_dir, '../test_data/test_merge.json')
    self.assertFileEqual(expected_file, output, regen_cmd)


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
