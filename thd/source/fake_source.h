// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_SOURCE_FAKE_SOURCE_H_
#define THD_SOURCE_FAKE_SOURCE_H_

#include "thd/source/source.h"

#include <base/macros.h>
#include <cstdint>

namespace thd {

// A fake source for testing and debugging.
// Can be configured to always return true, or false,
// and what value it should return.
class FakeSource : public Source {
 public:
  FakeSource(int64_t value, bool success);
  FakeSource(const FakeSource&) = delete;
  FakeSource& operator=(const FakeSource&) = delete;

  ~FakeSource() override;

  void set_value(int64_t value_to_set) { value_ = value_to_set; }
  void set_success(bool success) { success_ = success; }

  // Source:
  bool ReadValue(int64_t* value_out) override;

 private:
  int64_t value_;
  bool success_;
};

}  // namespace thd

#endif  // THD_SOURCE_FAKE_SOURCE_H_
