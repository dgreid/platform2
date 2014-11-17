// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/udev_stub.h"

#include "power_manager/powerd/system/tagged_device.h"
#include "power_manager/powerd/system/udev_tagged_device_observer.h"

namespace power_manager {
namespace system {

UdevStub::UdevStub() {}

UdevStub::~UdevStub() {}

bool UdevStub::HasSubsystemObserver(const std::string& subsystem,
                                    UdevSubsystemObserver* observer) const {
  SubsystemObserverMap::const_iterator it =
      subsystem_observers_.find(subsystem);
  return it != subsystem_observers_.end() && it->second.count(observer);
}

void UdevStub::TaggedDeviceChanged(const std::string& syspath,
                                   const std::string& tags) {
  tagged_devices_[syspath] = TaggedDevice(syspath, tags);
  const TaggedDevice& device = tagged_devices_[syspath];
  FOR_EACH_OBSERVER(UdevTaggedDeviceObserver, tagged_device_observers_,
                    OnTaggedDeviceChanged(device));
}

void UdevStub::TaggedDeviceRemoved(const std::string& syspath) {
  TaggedDevice device = tagged_devices_[syspath];
  tagged_devices_.erase(syspath);
  FOR_EACH_OBSERVER(UdevTaggedDeviceObserver, tagged_device_observers_,
                    OnTaggedDeviceRemoved(device));
}

void UdevStub::AddSubsystemObserver(const std::string& subsystem,
                                    UdevSubsystemObserver* observer) {
  subsystem_observers_[subsystem].insert(observer);
}

void UdevStub::RemoveSubsystemObserver(const std::string& subsystem,
                                       UdevSubsystemObserver* observer) {
  subsystem_observers_[subsystem].erase(observer);
}

void UdevStub::AddTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.AddObserver(observer);
}

void UdevStub::RemoveTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.RemoveObserver(observer);
}

std::vector<TaggedDevice> UdevStub::GetTaggedDevices() {
  std::vector<TaggedDevice> devices;
  devices.reserve(tagged_devices_.size());
  for (const std::pair<std::string, TaggedDevice>& pair : tagged_devices_)
    devices.push_back(pair.second);
  return devices;
}

bool UdevStub::GetSysattr(const std::string& syspath,
                          const std::string& sysattr,
                          std::string* value) {
  SysattrMap::iterator it = map_.find(std::make_pair(syspath, sysattr));
  if (it == map_.end())
    return false;
  *value = it->second;
  return true;
}

bool UdevStub::SetSysattr(const std::string& syspath,
                          const std::string& sysattr,
                          const std::string& value) {
  // Allows arbitrary attributes to be created using SetSysattr, which differs
  // from UdevSysattr. For all reasonable testing scenarios, this should be
  // fine.
  map_[std::make_pair(syspath, sysattr)] = value;
  return true;
}

bool UdevStub::FindParentWithSysattr(const std::string& syspath,
                                     const std::string& sysattr,
                                     const std::string& stop_at_devtype,
                                     std::string* parent_syspath) {
  *parent_syspath = syspath;
  return true;
}

}  // namespace system
}  // namespace power_manager
