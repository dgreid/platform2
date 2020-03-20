#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

import os
import subprocess

import power_manager_prefs_gen_schema

from chromite.lib import cros_test_lib

THIS_DIR = os.path.dirname(__file__)
SCHEMA_FILE = os.path.join(THIS_DIR, 'power_manager_prefs_schema.yaml')


class MainTest(cros_test_lib.TempDirTestCase):

  def testSchemaMatches(self):
    output_file = os.path.join(self.tempdir, 'output')
    power_manager_prefs_gen_schema.Main(output=output_file)

    changed = subprocess.run(
        ['diff', SCHEMA_FILE, output_file]).returncode != 0

    regen_cmd = ('To regenerate the schema, run:\n'
                 '\tpython3 -m cros_config_host.power_manager_prefs_gen_schema '
                 '-o %s ' % SCHEMA_FILE)

    if changed:
      print(regen_cmd)
      self.fail('Powerd prefs schema does not match C++ prefs source.')


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
