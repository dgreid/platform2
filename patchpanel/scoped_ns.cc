// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/scoped_ns.h"

#include <fcntl.h>
#include <sched.h>

#include <string>

#include <base/strings/stringprintf.h>

namespace patchpanel {

ScopedNS::ScopedNS(pid_t pid) : valid_(false) {
  const std::string filename =
      base::StringPrintf("/proc/%d/ns/net", static_cast<int>(pid));
  ns_fd_.reset(open(filename.c_str(), O_RDONLY));
  if (!ns_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open " << filename;
    return;
  }
  self_fd_.reset(open("/proc/self/ns/net", O_RDONLY));
  if (!self_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open host netns";
    return;
  }
  if (setns(ns_fd_.get(), CLONE_NEWNET) != 0) {
    PLOG(ERROR) << "Could not enter netns for " << pid;
    return;
  }
  valid_ = true;
}

ScopedNS::~ScopedNS() {
  if (valid_) {
    if (setns(self_fd_.get(), CLONE_NEWNET) != 0)
      PLOG(FATAL) << "Could not enter host ns";
  }
}

}  // namespace patchpanel
