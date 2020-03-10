#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=module-missing-docstring,class-missing-docstring

from __future__ import print_function

import libcros_schema

from chromite.lib import cros_test_lib


class GetNamedTupleTests(cros_test_lib.TestCase):

  def testRecursiveDicts(self):
    val = {'a': {'b': 1, 'c': 2}}
    val_tuple = libcros_schema.GetNamedTuple(val)
    self.assertEqual(val['a']['b'], val_tuple.a.b)
    self.assertEqual(val['a']['c'], val_tuple.a.c)

  def testListInRecursiveDicts(self):
    val = {'a': {'b': [{'c': 2}]}}
    val_tuple = libcros_schema.GetNamedTuple(val)
    self.assertEqual(val['a']['b'][0]['c'], val_tuple.a.b[0].c)

  def testDashesReplacedWithUnderscores(self):
    val = {'a-b': 1}
    val_tuple = libcros_schema.GetNamedTuple(val)
    self.assertEqual(val['a-b'], val_tuple.a_b)


# TODO(crbug.com/897753): Add explicit tests for FindImports and ApplyIports.
# This functionality is already tested in larger, more specific tests in
# cros_config_schema_unittest and cros_config_test_schema_unittest.


if __name__ == '__main__':
  cros_test_lib.main(module=__name__)
