# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.

include common.mk

$(OUT)simple_file/libsimple_file-pie.a: $(simple_file_CXX_OBJECTS)
	$(call update_archive,pie)

$(OUT)simple_file/libsimple_file-pic.a: $(simple_file_CXX_OBJECTS)
	$(call update_archive,pic)


# Add our target
all: $(OUT)simple_file/libsimple_file-pie.a
all: $(OUT)simple_file/libsimple_file-pic.a
# Ditto
RM_ON_CLEAN += $(OUT)simple_file/libsimple_file-pie.a
RM_ON_CLEAN += $(OUT)simple_file/libsimple_file-pic.a
