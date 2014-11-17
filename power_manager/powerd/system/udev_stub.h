// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/compiler_specific.h>
#include <base/macros.h>

#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace system {

// Stub implementation of UdevInterface for use in tests.
class UdevStub : public UdevInterface {
 public:
  UdevStub();
  virtual ~UdevStub();

  // Returns true if |observer| is registered for |subsystem|.
  bool HasSubsystemObserver(const std::string& subsystem,
                            UdevSubsystemObserver* observer) const;

  // Act as if a device was changed or removed. Notifies
  // UdevTaggedDeviceObservers and modifies the internal list of tagged devices.
  void TaggedDeviceChanged(const std::string& syspath, const std::string& tags);
  void TaggedDeviceRemoved(const std::string& syspath);

  // UdevInterface implementation:
  void AddSubsystemObserver(const std::string& subsystem,
                            UdevSubsystemObserver* observer) override;
  void RemoveSubsystemObserver(const std::string& subsystem,
                               UdevSubsystemObserver* observer) override;
  void AddTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) override;
  void RemoveTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) override;
  std::vector<TaggedDevice> GetTaggedDevices() override;
  bool GetSysattr(const std::string& syspath,
                  const std::string& sysattr,
                  std::string* value) override;
  bool SetSysattr(const std::string& syspath,
                  const std::string& sysattr,
                  const std::string& value) override;
  bool FindParentWithSysattr(const std::string& syspath,
                             const std::string& sysattr,
                             const std::string& stop_at_devtype,
                             std::string* parent_syspath) override;

 private:
  // Registered observers keyed by subsystem.
  typedef std::map<std::string, std::set<UdevSubsystemObserver*>>
      SubsystemObserverMap;
  SubsystemObserverMap subsystem_observers_;

  ObserverList<UdevTaggedDeviceObserver> tagged_device_observers_;

  // Maps a syspath to the corresponding TaggedDevice.
  std::map<std::string, TaggedDevice> tagged_devices_;

  // Maps a pair (device syspath, sysattr name) to the corresponding sysattr
  // value.
  typedef std::map<std::pair<std::string, std::string>, std::string> SysattrMap;
  SysattrMap map_;

  DISALLOW_COPY_AND_ASSIGN(UdevStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_
