// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_SOURCE_SOURCE_H_
#define THD_SOURCE_SOURCE_H_

#include <cstdint>

#include <base/macros.h>

namespace thd {

// Source Interface.
//
// Sources are the way to read system state in thd, be it power, temperature,
// or any other information.
class Source {
 public:
  // ReadValue is implemented by each logical way of retrieving state data
  // (file reading, etc).
  // Returns true, and sets |value| to the current value on success.
  // Returns false, and leaves |value| untouched on failure.
  virtual bool ReadValue(int64_t* value_out) = 0;

 protected:
  Source() {}
  Source(const Source&) = delete;
  Source& operator=(const Source&) = delete;

  virtual ~Source() {}
};

}  // namespace thd

#endif  // THD_SOURCE_SOURCE_H_
