// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_entry.h"

namespace croslog {

LogEntry::LogEntry(base::Time time, std::string&& entire_line)
    : time_(time), entire_line_(std::move(entire_line)) {}

}  // namespace croslog
