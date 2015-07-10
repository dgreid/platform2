#!/bin/bash
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command for unit test
echo $(basename $0) "$@" | sed "s#/tmp/test_fw\..\{6\}#temp_dir#g"


