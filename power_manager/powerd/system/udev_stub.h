// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/compiler_specific.h>
#include <base/macros.h>
#include <base/observer_list.h>

#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace system {

// Stub implementation of UdevInterface for use in tests.
class UdevStub : public UdevInterface {
 public:
  UdevStub();
  ~UdevStub() override;

  // Returns true if |observer| is registered for |subsystem|.
  bool HasSubsystemObserver(const std::string& subsystem,
                            UdevSubsystemObserver* observer) const;

  // Notifies the relevant observers in |subsystem_observers_| about |event|.
  void NotifySubsystemObservers(const UdevEvent& event);

  // Act as if a device was changed or removed. Notifies
  // UdevTaggedDeviceObservers and modifies the internal list of tagged devices.
  void TaggedDeviceChanged(const std::string& syspath,
                           const base::FilePath& wakeup_device_path,
                           const std::string& tags);
  void TaggedDeviceRemoved(const std::string& syspath);

  // Removes a |sysattr| to test the scenarios where the file is deleted.
  void RemoveSysattr(const std::string& syspath, const std::string& sysattr);

  // Adds a device to be returned by GetSubsystemDevices.
  void AddSubsystemDevice(const std::string& subsystem,
                          const UdevDeviceInfo& udev_device,
                          std::initializer_list<std::string> devlinks);

  // UdevInterface implementation:
  void AddSubsystemObserver(const std::string& subsystem,
                            UdevSubsystemObserver* observer) override;
  void RemoveSubsystemObserver(const std::string& subsystem,
                               UdevSubsystemObserver* observer) override;
  void AddTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) override;
  void RemoveTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) override;
  std::vector<TaggedDevice> GetTaggedDevices() override;
  bool GetSubsystemDevices(const std::string& subsystem,
                           std::vector<UdevDeviceInfo>* devices_out) override;
  bool GetSysattr(const std::string& syspath,
                  const std::string& sysattr,
                  std::string* value) override;
  bool SetSysattr(const std::string& syspath,
                  const std::string& sysattr,
                  const std::string& value) override;
  bool GetDevlinks(const std::string& syspath,
                   std::vector<std::string>* out) override;

 private:
  // List of subsystem devices returned when GetSubsystemDevices is called.
  // keyed by subsystem name. Example: "input".
  using UdevDeviceInfoMap = std::map<std::string, std::vector<UdevDeviceInfo>>;
  UdevDeviceInfoMap subsystem_devices_;

  // Registered observers keyed by subsystem.
  std::map<std::string,
           std::unique_ptr<base::ObserverList<UdevSubsystemObserver>>>
      subsystem_observers_;

  base::ObserverList<UdevTaggedDeviceObserver> tagged_device_observers_;

  // Maps a syspath to the corresponding TaggedDevice.
  std::map<std::string, TaggedDevice> tagged_devices_;

  // Maps a syspath to the corresponding devlinks.
  std::map<std::string, std::vector<std::string>> devlinks_;

  // Maps a pair (device syspath, sysattr name) to the corresponding sysattr
  // value.
  typedef std::map<std::pair<std::string, std::string>, std::string> SysattrMap;
  SysattrMap map_;

  DISALLOW_COPY_AND_ASSIGN(UdevStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_UDEV_STUB_H_
