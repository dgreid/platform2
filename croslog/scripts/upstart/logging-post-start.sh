#!/bin/bash
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ -z "${UPSTART_JOB}" ]; then
  echo "\$UPSTART_JOB is empty."
  exit 1
fi

LOG_FIFO=/tmp/upstart-${UPSTART_JOB}-log-fifo

# Remove the fifo file after starting the daemon, since it's already bound.
rm -f "${LOG_FIFO}"
