# Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.makefile file.
#
# This file provides a common architecture for building C/C++ source trees.
# It uses recursive makefile inclusion to create a single make process which
# can be built in the source tree or with the build products places elsewhere.
#
# To use:
# 1. Place common.mk in your top source level
# 2. In your top-level Makefile, place "include common.mk" at the top
# 3. In all subdirectories, create a 'module.mk' file that starts with:
#      include common.mk
#    And then contains the remainder of your targets.
# 4. All build targets should look like:
#    $(BUILD)relative/path/target: ...
#
# See existing makefiles for rule examples.
#
# Exported macros:
#   - cc_binary, cxx_binary provide standard compilation steps for binaries
#   - cxx_library, cc_library provide standard compilation steps for sos
#   All of the above optionally take an argument for extra flags.
#   - update_archive creates/updates a given .a target
#
# Exported variables
#  - RM_ON_CLEAN - append files to remove on calls to clean
#  - RMDIR_ON_CLEAN - append dirs to remove on calls to clean
#
# Exported targets meant to have prerequisites added to:
#  - all - Your $(OUT)target should be given
#  - small_tests - requires something, even if it is just NONE
#  - large_tests - requires something, even if it is just NONE
#  - NONE - nop target for *_tests
#  - FORCE - force the given target to run regardless of changes
#
# Possible command line variables:
#   - COLOR=[0|1] to set ANSI color output (default: 1)
#   - VERBOSE=[0|1] to hide/show commands (default: 0)
#   - MODE=dbg to turn down optimizations (default: opt)
#   - ARCH=[x86|arm|supported qemu name] (default: from portage or uname -m)
#   - SPLITDEBUG=[0|1] splits debug info in target.debug (default: 1)
#                      [SPLITDEBUG=0 MODE=opt will disable compiling with -g]
#   - VALGRIND=[0|1] runs tests under valgrind (default: 0)
#   - OUT=/path/to/builddir puts all output in given path
#           (default: $PWD/build-$MODE. Use OUT=. for normal behavior)
#   - VALGRIND_ARGS="" supplies extra memcheck arguments
#
# External CXXFLAGS and CFLAGS should be passed via the environment since this
# file does not use 'override' to control them.

# Behavior configuration variables
SPLITDEBUG ?= 1
VALGRIND ?= 0
COLOR ?= 1
VERBOSE ?= 0
MODE ?= opt
ARCH ?= $(shell uname -m)
# TODO: profiling support not completed.
PROFILING ?= 0

# Put objects in a separate tree based on makefile locations
# This means you can build a tree without touching it:
#   make -C $SRCDIR  # will create ./build
# Or
#   make -C $SRCDIR OUT=$PWD
# This variable is extended on subdir calls and doesn't need to be re-called.
OUT ?= $(PWD)/build-$(MODE)/
# Ensure a command-line supplied OUT has a slash
override OUT := $(abspath $(OUT))/

# Only call MODULE if we're in a submodule
MODULES_LIST := $(filter-out Makefile %.d,$(MAKEFILE_LIST))
ifeq ($(words $(filter-out Makefile common.mk %.d,$(MAKEFILE_LIST))),0)
# Setup a top level SRC
SRC ?= $(PWD)

#
# Helper macros
#

# Creates the actual archive with an index.
# $(1) is the object suffix modified: pie or pic.
define update_archive
  $(QUIET)mkdir -p $(dir $@)
  $(QUIET)# Create the archive in one step to avoid parallel use accessing it
  $(QUIET)# before all the symbols are present.
  @$(ECHO) "AR	$(subst $(PWD)/,,$(^:.o=.$(1).o)) -> $(subst $(PWD)/,,$@)"
  $(QUIET)$(AR) rcs $@ $(^:.o=.$(1).o)
endef

# Default compile from objects using pre-requisites but filters out
# subdirs and .d files.
define cc_binary
  $(call COMPILE_BINARY_implementation,CC,$(CFLAGS) $(1))
endef

define cxx_binary
  $(call COMPILE_BINARY_implementation,CXX,$(CXXFLAGS) $(1))
endef

# Default compile from objects using pre-requisites but filters out
# subdirs and .d files.
define cc_library
  $(call COMPILE_LIBRARY_implementation,CC,$(CFLAGS) $(1))
endef
define cxx_library
  $(call COMPILE_LIBRARY_implementation,CXX,$(CXXFLAGS) $(1))
endef


# Deletes files silently if they exist. Meant for use in any local
# clean targets.
define silent_rm
  $(QUIET)(test -n "$(wildcard $(1))" && \
    $(ECHO) -n '$(COLOR_RED)CLEANFILE$(COLOR_RESET) ' && \
    $(ECHO) '$(subst $(PWD)/,,$(wildcard $(1)))' && \
    $(RM) -f $(1) 2>/dev/null) || true
endef
define silent_rmdir
  $(QUIET)(test -n "$(wildcard $(1))" && \
    $(ECHO) -n '$(COLOR_RED)CLEANDIR$(COLOR_RESET) ' && \
    $(ECHO) '$(subst $(PWD)/,,$(wildcard $(1)))' && \
    $(RMDIR) $(1) 2>/dev/null) || true
endef



#
# Default variables for use in including makefiles
# 

# All objects for .c files at the top level
C_OBJECTS := $(patsubst %.c,$(OUT)%.o,$(wildcard *.c))

# All objects for .cxx files at the top level
CXX_OBJECTS := $(patsubst %.cc,$(OUT)%.o,$(wildcard *.cc))

#
# Default variable values
#

OBJCOPY ?= objcopy
STRIP ?= strip
RMDIR ?= rmdir
# Only override CC and CXX if they are from make.
ifeq ($(origin CC), default)
  CC = gcc
endif
ifeq ($(origin CXX), default)
  CXX = g++
endif
ifeq ($(origin RANLIB), default)
  RANLIB = ranlib
endif
RANLIB ?= ranlib
ECHO = /bin/echo -e

ifeq ($(PROFILING),1)
  $(warning PROFILING=1 disables relocatable executables.)
endif

# To update these from an including Makefile:
#  CXXFLAGS += -mahflag  # Append to the list
#  CXXFLAGS := -mahflag $(CXXFLAGS) # Prepend to the list
#  CXXFLAGS := $(filter-out badflag,$(CXXFLAGS)) # Filter out a value
# The same goes for CFLAGS.
CXXFLAGS := $(CXXFLAGS) -Wall -Werror -fstack-protector-all  -DFORTIFY_SOURCE \
  -O2 -ggdb3 -DNDEBUG -Wa,--noexecstack
CFLAGS := $(CFLAGS) -Wall -Werror -fstack-protector-all -DFORTIFY_SOURCE \
  -O2 -ggdb3 -DNDEBUG -Wa,--noexecstack

ifeq ($(PROFILING),1)
  CFLAGS := -pg 
  CXXFLAGS := -pg
endif

ifeq ($(MODE),dbg)
  CFLAGS := $(filter-out -O2 -DNDEBUG,$(CFLAGS)) -O1
  CXXFLAGS := $(filter-out -O2 -DNDEBUG,$(CXXFLAGS)) -O1
  # TODO: May need -nopie. need to check gdb
else  # opt
  ifeq ($(SPLITDEBUG),0)
    # TODO: do we want -fomit-frame-pointer on x86?
    CFLAGS := $(filter-out -ggdb3,$(CFLAGS))
    CXXFLAGS := $(filter-out -ggdb3,$(CXXFLAGS))
  endif
endif

LDFLAGS := $(LDFLAGS) -Wl,-z,relro -Wl,-z,noexecstack

# Fancy helpers for color if a prompt is defined
ifeq ($(COLOR),1)
COLOR_RESET = \x1b[0m
COLOR_GREEN = \x1b[32;01m
COLOR_RED = \x1b[31;01m
COLOR_YELLOW = \x1b[33;01m
endif

# By default, silence build output.
QUIET = @
ifeq ($(VERBOSE),1)
  QUIET=
endif

#
# Implementation macros for compile helpers above
#

# Useful for dealing with pie-broken toolchains.
# Call make with PIE=0 to disable default PIE use.
OBJ_PIE_FLAG = -fPIE
COMPILE_PIE_FLAG = -pie
ifeq ($(PIE),0)
  OBJ_PIE_FLAG =
  COMPILE_PIE_FLAG =
endif

# Default compile from objects using pre-requisites but filters out
# all non-.o files.
define COMPILE_BINARY_implementation
  @$(ECHO) "LD$(1)	$(subst $(PWD)/,,$@)"
  $(QUIET)$($(1)) $(COMPILE_PIE_FLAGS) -o $@ \
    $(filter %.o %.so %.a,$(^:.o=.pie.o)) $(LDFLAGS) $(2)
  $(call strip_library)
  @$(ECHO) "BIN	$(COLOR_GREEN)$(subst $(PWD)/,,$@)$(COLOR_RESET)"
  @$(ECHO) "	$(COLOR_YELLOW)-----$(COLOR_RESET)"
endef

# TODO: add version support extracted from PV environment variable
#ifeq ($(PV),9999)
#$(warning PV=$(PV). If shared object versions matter, please force PV=.)
#endif
# Then add -Wl,-soname,$@.$(PV) ?

# Default compile from objects using pre-requisites but filters out
# all non-.o values. (Remember to add -L$(OUT) -llib)
define COMPILE_LIBRARY_implementation
  @$(ECHO) "SHARED$(1)	$(subst $(PWD)/,,$@)"
  $(QUIET)$($(1)) -shared -Wl,-E -o $@ \
    $(filter %.o %.so %.a,$(^:.o=.pic.o)) $(2) $(LDFLAGS)
  $(call strip_library)
  @$(ECHO) "LIB	$(COLOR_GREEN)$(subst $(PWD)/,,$@)$(COLOR_RESET)"
  @$(ECHO) "	$(COLOR_YELLOW)-----$(COLOR_RESET)"
endef

define strip_library
  @$(ECHO) "STRIP	$(subst $(PWD)/,,$@)"
  $(if $(filter 1,$(SPLITDEBUG)), @$(ECHO) -n "DEBUG	"; \
    $(ECHO) "$(COLOR_YELLOW)$(subst $(PWD)/,,$@).debug$(COLOR_RESET)")
  $(if $(filter 1,$(SPLITDEBUG)), \
    $(QUIET)$(OBJCOPY) --only-keep-debug "$@" "$@.debug")
  $(if $(filter-out dbg,$(MODE)),$(QUIET)$(STRIP) --strip-unneeded "$@",)
endef

define strip_binary
  @$(ECHO) "STRIP	$(subst $(PWD)/,,$@)"
  $(if $(filter 1,$(SPLITDEBUG)), @$(ECHO) -n "DEBUG	"; \
    $(ECHO) "$(COLOR_YELLOW)$(subst $(PWD)/,,$@).debug$(COLOR_RESET)")
  $(if $(filter 1,$(SPLITDEBUG)), \
    $(QUIET)$(OBJCOPY) --only-keep-debug "$@" "$@.debug")
  $(if $(filter-out dbg,$(MODE)),$(QUIET)$(STRIP) --strip-unneeded "$@",)
endef

#
# Pattern rules
#

%.o: %.pie.o %.pic.o
	$(QUIET)touch $@

$(OUT)%.pie.o: %.c
	$(call OBJECT_PATTERN_implementation,CC,$(CFLAGS) $(OBJ_PIE_FLAG))

$(OUT)%.pic.o: %.c
	$(call OBJECT_PATTERN_implementation,CC,$(CFLAGS) -fPIC)

$(OUT)%.pie.o: %.cc
	$(call OBJECT_PATTERN_implementation,CXX,$(CXXFLAGS) $(OBJ_PIE_FLAG))

$(OUT)%.pic.o: %.cc
	$(call OBJECT_PATTERN_implementation,CXX,$(CXXFLAGS) -fPIC)


define OBJECT_PATTERN_implementation
  @$(ECHO) "$(subst $(PWD)/,,$(1))	$(subst $(PWD)/,,$<)"
  $(QUIET)mkdir -p $(dir $@)
  $(QUIET)$($(1)) -c -MD -MF $(basename $@).d -o $@ $< $(2)
  $(QUIET)# Wrap all the deps in $(wildcard) so a missing header
  $(QUIET)# won't cause weirdness.  First we remove newlines and \,
  $(QUIET)# then wrap it.
  $(QUIET)sed -i -e :j -e '$$!N;s|\\\s*\n| |;tj' \
    -e 's|^\(.*\s*:\s*\)\(.*\)$$|\1 $$\(wildcard \2\)|' $(basename $@).d
endef

# NOTE: A specific rule for archive objects is avoided because parallel
#       update of the archive causes build flakiness.
# Instead, just make the objects the prerequisites and use update_archive
# To use the foo.a(obj.o) functionality, targets would need to specify the
# explicit object they expect on the prerequisite line.

#
# Architecture detection and QEMU wrapping
#

ARCH ?= $(shell uname -m)
HOST_ARCH ?= $(shell uname -m)
# emake will supply "x86" or "arm" for ARCH, but
# if uname -m runs and you get x86_64, then this subst
# will break.
ifeq ($(subst x86,i386,$(ARCH)),i386)
  QEMU_ARCH := $(subst x86,i386,$(ARCH))  # x86 -> i386
else
  QEMU_ARCH = $(ARCH)
endif

# If we're cross-compiling, try to use qemu for running the tests.
QEMU_CMD ?=
ifneq ($(QEMU_ARCH),$(HOST_ARCH))
  ifeq ($(SYSROOT),)
    $(info SYSROOT not defined. qemu-based testing disabled)
  else
    # A SYSROOT is assumed for QEmu use.
    USE_QEMU ?= 1
  endif
endif

#
# Output full configuration at top level
#

# Don't show on clean
ifneq ($(MAKECMDGOALS),clean)
  $(info build configuration:)
  $(info - OUT=$(OUT))
  $(info - MODE=$(MODE))
  $(info - SPLITDEBUG=$(SPLITDEBUG))
  $(info - VALGRIND=$(VALGRIND))
  $(info - COLOR=$(COLOR))
  $(info - ARCH=$(ARCH))
  $(info - QEMU_ARCH=$(QEMU_ARCH))
  $(info - SYSROOT=$(SYSROOT))
  $(info )
endif

#
# Standard targets with detection for when they are improperly configured.
#

# all does not include tests by default
all:
	$(QUIET)(test -z "$^" && \
	$(ECHO) "You must add your targets as 'all' prerequisites") || true
	$(QUIET)test -n "$^"

# Builds and runs tests for the target arch
tests: small_tests large_tests

small_tests: qemu FORCE
	$(call TEST_implementation) 

large_tests: qemu FORCE
	$(call TEST_implementation)

qemu_clean: FORCE
ifeq ($(USE_QEMU),1)
	$(call silent_rm,$(PWD)/qemu-$(QEMU_ARCH))
endif
	
qemu: FORCE
ifeq ($(USE_QEMU),1)
	$(QUIET)$(ECHO) "QEMU	Preparing qemu-$(QEMU_ARCH)"
	$(QUIET)cp -f /usr/bin/qemu-$(QEMU_ARCH) $(PWD)/qemu-$(QEMU_ARCH)
	$(QUIET)chmod a+rx $(PWD)/qemu-$(QEMU_ARCH)
endif

# TODO(wad) separate chroot from qemu to make it possible to cross-compile
#           and test outside of the chroot.
ifeq ($(USE_QEMU),1)
  export QEMU_CMD = sudo chroot $(SYSROOT) \
    $(subst $(SYSROOT),,$(PWD))/qemu-$(QEMU_ARCH) \
    -drop-ld-preload \
    -E LD_LIBRARY_PATH="$(SYSROOT_LDPATH)" \
    -E HOME="$(HOME)" --
endif

VALGRIND_CMD =
ifeq ($(VALGRIND),1)
  VALGRIND_CMD = /usr/bin/valgrind --tool=memcheck $(VALGRIND_ARGS) --  
endif

define TEST_implementation
  $(QUIET)(test -z "$(filter FORCE qemu,$^)" && \
  $(ECHO) "No '$@' prerequisites defined!") || true
  $(QUIET)test -n "$^"
  $(QUIET)# TODO(wad) take root away after the chroot.
  $(QUIET)test "$(filter NONE,$^)" = "NONE" || \
    ((test "1" = "$(VALGRIND)" && test -n "$(USE_QEMU)" && \
      sudo mkdir -p $(SYSROOT)/proc && \
       sudo mount --bind /proc $(SYSROOT)/proc); \
    (status=0 && for tgt in $(subst $(SYSROOT),,$(filter-out FORCE qemu,$^)); \
    do \
      $(ECHO) "TEST	$$tgt"; \
      $(QEMU_CMD) $(VALGRIND_CMD) $$tgt $(GTEST_ARGS); \
      status=$$((status + $$?)); \
    done; (test "1" = "$(VALGRIND)" && test -n "$(USE_QEMU)" && \
           sudo umount $(SYSROOT)/proc); exit $$status))
endef


# Add the defaults from this dir to rm_clean
define default_rm_clean
  $(OUT)$(1)*.d $(OUT)$(1)*.o $(OUT)$(1)*.debug
endef

# Start with the defaults
RM_ON_CLEAN = $(call default_rm_clean)
RMDIR_ON_CLEAN = $(OUT)

# Recursive list reversal so that we get RMDIR_ON_CLEAN in reverse order.
define reverse
$(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))
endef

rm_clean: FORCE 
	$(call silent_rm,$(RM_ON_CLEAN))

rmdir_clean: FORCE rm_clean
	$(call silent_rmdir,$(call reverse,$(RMDIR_ON_CLEAN)))

clean: qemu_clean rmdir_clean

FORCE: ;
# Empty rule for use when no special targets are needed, like large_tests
NONE:
	
.PHONY: clean NONE qemu_clean valgrind rm_clean rmdir_clean
.DEFAULT_GOAL  :=  all
# Don't let make blow away "intermediates"
.PRECIOUS: $(OUT)%.pic.o $(OUT)%.pie.o

# Start accruing build info
OUT_DIRS = $(OUT)
SRC_DIRS = .

include $(wildcard $(OUT)*.d)
SUBMODULE_DIRS = $(wildcard */module.mk)
include $(SUBMODULE_DIRS)

else  ## In duplicate inclusions of common.mk

# Get the current inclusion directory without a trailing slash
MODULE := $(patsubst %/,%, \
           $(dir $(lastword $(filter-out %common.mk,$(MAKEFILE_LIST)))))
MODULE_NAME := $(subst /,_,$(MODULE))

# Depth first
$(eval OUT_DIRS += $(OUT)/$(MODULE))
$(eval SRC_DIRS += $(MODULE))

# Add the defaults from this dir to rm_clean
$(eval RM_ON_CLEAN += $(call default_rm_clean,$(MODULE)/))
$(eval RMDIR_ON_CLEAN += $(wildcard $(OUT)$(MODULE)/))

$(info + submodule: $(MODULE_NAME))
# We must eval otherwise they may be dropped.
$(eval $(MODULE_NAME)_C_OBJECTS ?= \
  $(patsubst %.c,$(OUT)%.o,$(wildcard $(MODULE)/*.c)))
$(eval $(MODULE_NAME)_CXX_OBJECTS ?= \
  $(patsubst %.cc,$(OUT)%.o,$(wildcard $(MODULE)/*.cc)))

# Continue recursive inclusion of module.mk files
SUBMODULE_DIRS = $(wildcard $(MODULE)/*/module.mk)

include $(wildcard $(OUT)$(MODULE)/*.d)
include $(SUBMODULE_DIRS)
endif

