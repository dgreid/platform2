// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_UDEV_TAGGED_DEVICE_OBSERVER_H_
#define POWER_MANAGER_POWERD_SYSTEM_UDEV_TAGGED_DEVICE_OBSERVER_H_

#include <string>

#include <base/observer_list_types.h>

namespace power_manager {
namespace system {

class TaggedDevice;

class UdevTaggedDeviceObserver : public base::CheckedObserver {
 public:
  virtual ~UdevTaggedDeviceObserver() {}

  // Called whenever a device with powerd tags set is added or changed.
  // Implementations should be idempotent, i.e. multiple invocations should have
  // the same effect as a one-time invocation.
  virtual void OnTaggedDeviceChanged(const TaggedDevice& device) = 0;

  // Called whenever a device with powerd tags set is removed.
  virtual void OnTaggedDeviceRemoved(const TaggedDevice& device) = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_UDEV_TAGGED_DEVICE_OBSERVER_H_
