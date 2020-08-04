#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The unit test suite for the CrosConfigHost CLI tool."""

from __future__ import print_function

import os
import subprocess
import unittest

CLI_FILE = 'python -m cros_config_host.cros_config_host'
YAML_FILE = '../test_data/test.yaml'


class CrosConfigHostTest(unittest.TestCase):
  """Tests for master configuration in yaml format"""
  def setUp(self):
    self.conf_file = os.path.join(os.path.dirname(__file__), YAML_FILE)

  # Common tests shared between the YAML and FDT implementations.
  def CheckManyLinesWithoutSpaces(self, output, lines=3):
    # Expect there to be a few lines
    self.assertGreater(len(output.split()), lines)
    # Expect each line to not have spaces in it
    for line in output.split():
      self.assertFalse(' ' in line)
      self.assertNotEqual(line[-1:], ' ')
    # Expect the last thing in the output to be a newline
    self.assertEqual(output[-1:], os.linesep)

  def CheckManyLines(self, output, lines=3):
    # Expect there to be a few lines
    self.assertGreater(len(output.split()), lines)
    # Expect each line to not end in space
    for line in output.split():
      self.assertNotEqual(line[-1:], ' ')
    # Expect the last thing in the output to be a newline
    self.assertEqual(output[-1:], os.linesep)

  def testListModels(self):
    call_args = '{} -c {} list-models'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLinesWithoutSpaces(output, lines=2)

  def testListModelsWithFilter(self):
    call_args = '{} -c {} --model=another list-models'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.assertEqual('another\n', output)

  def testListModelsWithEnvFilter(self):
    call_args = '{} -c {} list-models'.format(
        CLI_FILE, self.conf_file).split()
    os.environ['CROS_CONFIG_MODEL'] = 'another'
    output = subprocess.check_output(call_args).decode('utf-8')
    del os.environ['CROS_CONFIG_MODEL']
    self.assertEqual('another\n', output)

  def testGetPropSingle(self):
    call_args = '{} -c {} --model=another get / wallpaper'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.assertEqual(output, 'default' + os.linesep)

  def testGetPropSingleWrongModel(self):
    call_args = '{} -c {} --model=dne get / wallpaper'.format(
        CLI_FILE, self.conf_file).split()
    # Ensure that the expected error output does not appear.
    output = subprocess.check_output(
        call_args, stderr=subprocess.PIPE).decode('utf-8')
    self.assertEqual(output, '')

  def testGetPropSingleWrongPath(self):
    call_args = '{} -c {} --model=another get /dne wallpaper'.format(
        CLI_FILE, self.conf_file).split()
    with self.assertRaises(subprocess.CalledProcessError):
      subprocess.run(call_args, check=True, stderr=subprocess.DEVNULL)

  def testGetPropSingleWrongProp(self):
    call_args = '{} -c {} --model=another get / dne'.format(
        CLI_FILE, self.conf_file).split()
    with self.assertRaises(subprocess.CalledProcessError):
      subprocess.run(call_args, check=True, stderr=subprocess.DEVNULL)

  def testGetFirmwareUris(self):
    call_args = '{} -c {} get-firmware-uris'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLines(output)

  def testGetMosysPlatform(self):
    call_args = '{} -c {} get-mosys-platform'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args, encoding='utf-8')
    self.assertEqual(output, 'Some\n')

  def testGetFingerprintFirmwareROVersionFound(self):
    call_args = '{} -c {} get-fpmcu-firmware-ro-version bloonchipper'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args, encoding='utf-8')
    self.assertEqual(output, 'VERSION1\n')

  def testGetFingerprintFirmwareROVersionNotSpecified(self):
    # If the ro-version is not specified, nothing is returned and the exit code
    # should be 0.
    call_args = '{} -c {} get-fpmcu-firmware-ro-version some_fpmcu'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args, encoding='utf-8')
    self.assertEqual(output, '')

  def testGetTouchFirmwareFiles(self):
    call_args = '{} -c {} get-touch-firmware-files'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLines(output, 10)

  def testGetAudioFiles(self):
    call_args = '{} -c {} get-audio-files'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLines(output, 10)

  def testGetFirmwareBuildTargets(self):
    call_args = '{} -c {} get-firmware-build-targets coreboot'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLines(output, 1)

  def testGetWallpaperFiles(self):
    call_args = '{} -c {} get-wallpaper-files'.format(
        CLI_FILE, self.conf_file).split()
    output = subprocess.check_output(call_args).decode('utf-8')
    self.CheckManyLines(output, 1)



if __name__ == '__main__':
  unittest.main()
