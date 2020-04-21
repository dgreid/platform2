// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/tagged_device.h"

#include <base/strings/string_tokenizer.h>

namespace power_manager {
namespace system {

TaggedDevice::TaggedDevice() {}

TaggedDevice::TaggedDevice(const std::string& syspath,
                           const base::FilePath& wakeup_device_path,
                           const std::string& tags) {
  syspath_ = syspath;
  wakeup_device_path_ = wakeup_device_path;

  base::StringTokenizer parts(tags, " ");
  while (parts.GetNext())
    tags_.insert(parts.token());
}

TaggedDevice::~TaggedDevice() {}

bool TaggedDevice::HasTag(const std::string& tag) const {
  return tags_.find(tag) != tags_.end();
}

}  // namespace system
}  // namespace power_manager
