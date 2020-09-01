#!/bin/bash

# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

OUT=$1
v=$2

deps=$(<"${OUT}"/gen/libshill-net-deps.txt)
# For backward compatibility.
# TODO(crbug/2386886): Remove after all versioned usages are removed.
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps}/g" \
  "net/libshill-net.pc.in" > "${OUT}/lib/libshill-net-${v}.pc"
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps}/g" \
  "net/libshill-net.pc.in" > "${OUT}/lib/libshill-net.pc"

deps_test=$(<"${OUT}"/gen/libshill-net-test-${v}-deps.txt)
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps_test}/g" \
  "net/libshill-net-test.pc.in" > "${OUT}/lib/libshill-net-test-${v}.pc"
