#!/bin/bash
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to run all Python unit tests in cros_config.

python3 -m unittest discover -p '*test.py'

# Run linter
# TODO(https://crbug.com/1101555): "cros lint" doesn't work when run as part of
# an ebuild.
if which cros; then
  find . -name '*.py' -exec cros lint --py3 {} +
fi
