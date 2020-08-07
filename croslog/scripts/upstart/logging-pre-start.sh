#!/bin/bash
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ -z "${UPSTART_JOB}" ]; then
  echo "\$UPSTART_JOB is empty."
  exit 1
fi

LOG_FIFO=/tmp/upstart-${UPSTART_JOB}-log-fifo

# Clean up a stale fifo file if it exists.
rm -f "${LOG_FIFO}"

# Create a fifo forwarded to "logger" command. This needs to be done in
# "pre-start" block to make upstart detecting a correct daemon PID.
mkfifo "${LOG_FIFO}"
logger -t "${UPSTART_JOB}" < "${LOG_FIFO}" &
