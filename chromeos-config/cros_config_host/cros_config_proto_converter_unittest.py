#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

import cros_config_proto_converter

from chromite.lib import cros_test_lib


class ParseArgsTests(cros_test_lib.TestCase):

  def testParseArgs(self):
    argv = ['-f', 'files-root', '-c', 'project-config',
            '-p', 'program-config', '-o', 'output', ]
    args = cros_config_proto_converter.ParseArgs(argv)
    self.assertEqual(args.files_root, 'files-root')
    self.assertEqual(args.project_config, 'project-config')
    self.assertEqual(args.program_config, 'program-config')
    self.assertEqual(args.output, 'output')


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
