# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.
#
# Top-level Makefile for verity.

# Pull in chromium os defaults
include common.mk

# Prepend with our include
CFLAGS += -I$(SRC)/include -I$(SRC)/..
CXXFLAGS += -I$(SRC)/include -I$(SRC)/..
# Use openssl for digests
LDFLAGS += -lcrypto

ifneq ($(WITH_CHROME),0)
  CXXFLAGS += -DWITH_CHROME=1
  LDFLAGS += -lbase -lpthread -lrt
  WITH_CHROME = 1
endif

# Current toolchain doesn't support -pie statically linked binaries
CXX_STATIC_BINARY(verity-static): \
  $(filter-out %_testrunner.o %_unittest.o,$(CXX_OBJECTS)) \
  $(C_OBJECTS) \
  CXX_STATIC_LIBRARY(kernel/libkernel.pic.a) \
  CXX_STATIC_LIBRARY(simple_file/libsimple_file.pic.a) \
  $(shell $(CXX) -print-file-name=libcrypto.a) \
  $(shell $(CXX) -print-file-name=libz.a) \
  $(shell $(CXX) -print-file-name=libdl.a) \
  $(shell $(CXX) -print-file-name=libstdc++.a) \
  $(shell $(CXX) -print-file-name=libc.a)
all: CXX_STATIC_BINARY(verity-static)
clean: CLEAN(verity-static)

CXX_BINARY(verity): $(filter-out %_testrunner.o %_unittest.o,$(CXX_OBJECTS)) \
                    $(C_OBJECTS) \
                    CXX_STATIC_LIBRARY(kernel/libkernel.pie.a) \
                    CXX_STATIC_LIBRARY(simple_file/libsimple_file.pie.a)
all: CXX_BINARY(verity)
clean: CLEAN(verity)

# Simple helper. Not ideal but here it is.
verity: CXX_BINARY(verity)

# Shared dm-bht library
CXX_LIBRARY(libdm-bht.so): $(filter-out %verity_main.o,$(C_OBJECTS)) \
                          $(kernel_C_OBJECTS)
all: CXX_LIBRARY(libdm-bht.so)
clean: CLEAN(libdm-bht.so)

# Tests require libchrome
ifneq ($(WITH_CHROME),0)
  # For this project, only the unittests are CXX
  CXX_BINARY(verity_testrunner): $(filter-out %_main.o,$(CXX_OBJECTS)) \
                           $(filter %dm-bht-userspace.o,$(C_OBJECTS)) \
                           CXX_STATIC_LIBRARY(kernel/libkernel.pie.a) \
                           CXX_STATIC_LIBRARY(simple_file/libsimple_file.pie.a)
	$(call cxx_binary,-lgtest -lgmock)

  all: CXX_BINARY(verity_testrunner)
  clean: CLEAN(verity_testrunner)
  tests: TEST(CXX_BINARY(verity_testrunner))
endif
