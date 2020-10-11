#!/bin/bash

# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

OUT=$1
v=$2
sed -e "s/@BSLOT@/${v}/g" libmetrics.pc.in > "${OUT}/lib/libmetrics.pc"
