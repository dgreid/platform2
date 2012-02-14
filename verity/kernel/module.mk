# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.

include common.mk

CXX_STATIC_LIBRARY(kernel/libkernel.pic.a): $(kernel_C_OBJECTS)
CXX_STATIC_LIBRARY(kernel/libkernel.pie.a): $(kernel_C_OBJECTS)
clean: CLEAN(kernel/libkernel.pic.a) CLEAN(kernel/libkernel.pie.a)
