# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.

include common.mk

$(OUT)kernel/libkernel-pic.a: $(kernel_C_OBJECTS)
	$(call update_archive,pic)
$(OUT)kernel/libkernel-pie.a: $(kernel_C_OBJECTS)
	$(call update_archive,pie)

# Add our target
all: $(OUT)kernel/libkernel-pie.a $(OUT)kernel/libkernel-pic.a
# Ditto
RM_ON_CLEAN += $(OUT)kernel/libkernel-*.a
