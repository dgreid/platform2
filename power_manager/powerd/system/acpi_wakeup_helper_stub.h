// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_STUB_H_

#include <map>
#include <string>

#include <base/macros.h>

#include "power_manager/powerd/system/acpi_wakeup_helper_interface.h"

namespace power_manager {
namespace system {

class AcpiWakeupHelperStub : public AcpiWakeupHelperInterface {
 public:
  AcpiWakeupHelperStub();
  AcpiWakeupHelperStub(const AcpiWakeupHelperStub&) = delete;
  AcpiWakeupHelperStub& operator=(const AcpiWakeupHelperStub&) = delete;

  ~AcpiWakeupHelperStub() override;

  // Implementation of AcpiWakeupHelperInterface.
  bool IsSupported() override;
  bool GetWakeupEnabled(const std::string& device_name,
                        bool* enabled_out) override;
  bool SetWakeupEnabled(const std::string& device_name, bool enabled) override;

 private:
  std::map<std::string, bool> wakeup_enabled_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_STUB_H_
