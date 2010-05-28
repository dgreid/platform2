# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.
#
# Top-level Makefile for verity.

# Pull in chromium os defaults
include common.mk

# Force g++ use.
#CC = $(CXX)

# Prepend with our include
CFLAGS := -Iinclude $(CFLAGS)
CXXFLAGS := -Iinclude -I../ $(CXXFLAGS)
# Use openssl for digests
LDFLAGS += -lcrypto

ifneq ($(CHROME_OS),0)
  CXXFLAGS += -DCHROME_OS=1
  LDFLAGS += -lpthread -lrt -L/usr/lib -lbase
  CHROME_OS = 1
else
  LDFLAGS += -lglog
endif

# Helper so I can type "make verity" on the prompt
verity: $(OUT)verity

## Binary target linked against a shared libdm-bht.so
#$(OUT)verity-l: $(OUT)libdm-bht.so $(OUT)verity_main.o
#	@# Build the target as a binary with extra flags
#	$(call cc_binary,-L$(OUT) -ldm-bht)
## Adds verity to the default compile
#all: $(OUT)verity-l
## Add our targets to get auto-rm'd
#RM_ON_CLEAN += $(OUT)verity-l

# Current toolchain doesn't support -pie statically linked binaries
#$(OUT)verity-static: $(C_OBJECTS) $(OUT)kernel/libkernel-pie.a \
#                       /usr/lib/libcrypto.a
#	@# Build the target as a binary with extra flags
#	$(call cc_binary,-static)
#all: $(OUT)verity-static
#RM_ON_CLEAN += $(wildcard $(OUT)verity-static)

$(OUT)verity: $(filter-out %_testrunner.o %_unittest.o,$(CXX_OBJECTS)) \
              $(C_OBJECTS) \
              $(OUT)kernel/libkernel-pie.a \
              $(OUT)simple_file/libsimple_file-pie.a
	$(call cxx_binary)
all: $(OUT)verity
RM_ON_CLEAN += $(OUT)verity

# Current toolchain doesn't support -pie statically linked binaries
#$(OUT)verity-static: $(C_OBJECTS) $(OUT)kernel/libkernel-pie.a \
#                       /usr/lib/libcrypto.a
#	@# Build the target as a binary with extra flags
#	$(call cc_binary,-static)
#all: $(OUT)verity-static
#m_clean: $(wildcard $(OUT)verity)

# Shared dm-bht library
$(OUT)libdm-bht.so: $(filter-out %verity_main.o,$(C_OBJECTS)) \
                      $(OUT)kernel/libkernel-pic.a
	$(call cc_library)
all: $(OUT)libdm-bht.so
RM_ON_CLEAN += $(OUT)libdm-bht.so

# For this project, only the unittests are CXX
$(OUT)verity_testrunner: $(filter-out %_main.o,$(CXX_OBJECTS)) \
                         $(OUT)simple_file/libsimple_file-pie.a \
                         $(OUT)kernel/libkernel-pie.a
	$(call cxx_binary,-lgtest -lgmock)

all: $(OUT)verity_testrunner
RM_ON_CLEAN += $(OUT)verity_testrunner
# Will run each prerequisite for tests.
# If cross-compiled, this is done against the correct architecture.
small_tests: $(OUT)verity_testrunner

# No large tests here so we use the NONE target.
large_tests: NONE
