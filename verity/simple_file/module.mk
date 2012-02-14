# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.

include common.mk

CXX_STATIC_LIBRARY(simple_file/libsimple_file.pie.a): $(simple_file_CXX_OBJECTS)
CXX_STATIC_LIBRARY(simple_file/libsimple_file.pic.a): $(simple_file_CXX_OBJECTS)
clean: CLEAN(simple_file/libsimple_file.pic.a)
clean: CLEAN(simple_file/libsimple_file.pie.a)
