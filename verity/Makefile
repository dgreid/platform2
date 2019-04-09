# Copyright (C) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.
#
# Top-level Makefile for verity.

# Pull in chromium os defaults
include common.mk

# Prepend with our include
CFLAGS += -I$(SRC)/include -I$(SRC)/..
CXXFLAGS += -I$(SRC)/include -I$(SRC)/..

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

CXX_STATIC_LIBRARY(libdm-bht.pic.a): $(C_OBJECTS) \
				     $(kernel_C_OBJECTS)

clean: CLEAN(libdm-bht.pic.a)

# Give libdm-bht.pic.a a slightly cleaner name
# But, we have to do some odd stuff like use $(OUT) and use "cp"
# rather than mv because it gets called twice and can ace with the
# actual build of libdm-bht.pic.a
# TODO(wad) fix the double invocation problem and clean this up
libdm-bht.a: CXX_STATIC_LIBRARY(libdm-bht.pic.a)
	cp $(OUT)libdm-bht.pic.a $(OUT)libdm-bht.a || true

all: libdm-bht.a

# For this project, only the unittests are CXX
CXX_BINARY(verity_testrunner): $(filter-out %_main.o,$(CXX_OBJECTS)) \
                          $(filter %dm-bht-userspace.o,$(C_OBJECTS)) \
                           CXX_STATIC_LIBRARY(kernel/libkernel.pie.a) \
                           CXX_STATIC_LIBRARY(simple_file/libsimple_file.pie.a)
UNITTEST_LIBS := -lgmock -lgtest -pthread -lpthread
CXX_BINARY(verity_testrunner): LDLIBS += $(UNITTEST_LIBS)

clean: CLEAN(verity_testrunner)
tests: TEST(CXX_BINARY(verity_testrunner))
