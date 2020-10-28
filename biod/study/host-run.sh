#!/bin/bash
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This can be run or sourced, which is why we don't choose to exec the final
# launch line.

FINGER_COUNT=2
ENROLLMENT_COUNT=20
VERIFICATION_COUNT=15

PICTURE_DIR=./fpstudy-fingers
# If LOG_DIR is left empty, log to console
LOG_DIR=

FPSTUDY_VIRTENV=/tmp/virtualenv-study

# Setup New Virtualenv
rm -rf "${FPSTUDY_VIRTENV}"
virtualenv -p python3 "${FPSTUDY_VIRTENV}"
. "${FPSTUDY_VIRTENV}/bin/activate"
pip3 install -r requirements.txt

if [[ -n "${LOG_DIR}" ]]; then
  mkdir -p "${LOG_DIR}"
fi
PATH="$(pwd)/mock-bin:${PATH}" ./study_serve.py  \
  --finger_count="${FINGER_COUNT}"               \
  --enrollment_count="${ENROLLMENT_COUNT}"       \
  --verification_count="${VERIFICATION_COUNT}"   \
  --picture_dir="${PICTURE_DIR}"                 \
  --log_dir="${LOG_DIR}"                         \
  "$@"
