// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_TAGGED_DEVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_TAGGED_DEVICE_H_

#include <string>
#include <unordered_set>

#include <base/files/file_path.h>

namespace power_manager {
namespace system {

// Represents a udev device with powerd tags associated to it.
class TaggedDevice {
 public:
  // Default constructor for easier use with std::map.
  TaggedDevice();
  TaggedDevice(const std::string& syspath,
               const base::FilePath& wakeup_device_path,
               const std::string& role,
               const std::string& tags);
  ~TaggedDevice();

  const std::string& syspath() const { return syspath_; }
  const std::string& role() const { return role_; }
  const base::FilePath& wakeup_device_path() const {
    return wakeup_device_path_;
  }
  const std::unordered_set<std::string> tags() const { return tags_; }

  // Returns true if the device has the given tag.
  bool HasTag(const std::string& tag) const;

 private:
  std::string syspath_;
  // POWERD_ROLE ENV variable set by |90-powerd-id.rules| based on several udev
  // attributes. Identifies the type of input device.
  std::string role_;
  // Directory (of itself/ancestor) with power/wakeup property.
  base::FilePath wakeup_device_path_;
  std::unordered_set<std::string> tags_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_TAGGED_DEVICE_H_
