// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SCOPED_NS_H_
#define PATCHPANEL_SCOPED_NS_H_

#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace patchpanel {

// Utility class for running code blocks within the network namespace.
class ScopedNS {
 public:
  explicit ScopedNS(pid_t pid);
  ScopedNS(const ScopedNS&) = delete;
  ScopedNS& operator=(const ScopedNS&) = delete;

  ~ScopedNS();

  // Returns whether or not the object was able to enter the network namespace.
  bool IsValid() const { return valid_; }

 private:
  bool valid_;
  base::ScopedFD ns_fd_;
  base::ScopedFD self_fd_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SCOPED_NS_H_
