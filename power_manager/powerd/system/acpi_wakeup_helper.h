// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_H_
#define POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include "power_manager/powerd/system/acpi_wakeup_helper_interface.h"

namespace power_manager {
namespace system {

// Abstraction layer around /proc/acpi/wakeup so that we can substitute it for
// testing. We cannot just use a regular file because read/write have special
// semantics.
class AcpiWakeupFileInterface {
 public:
  AcpiWakeupFileInterface() {}
  AcpiWakeupFileInterface(const AcpiWakeupFileInterface&) = delete;
  AcpiWakeupFileInterface& operator=(const AcpiWakeupFileInterface&) = delete;

  virtual ~AcpiWakeupFileInterface() {}

  // Checks whether the file exists.
  virtual bool Exists() = 0;

  // Reads file contents. Returns true on success.
  virtual bool Read(std::string* contents) = 0;

  // Writes file contents. Returns true on success.
  virtual bool Write(const std::string& contents) = 0;
};

class AcpiWakeupHelper : public AcpiWakeupHelperInterface {
 public:
  AcpiWakeupHelper();
  AcpiWakeupHelper(const AcpiWakeupHelper&) = delete;
  AcpiWakeupHelper& operator=(const AcpiWakeupHelper&) = delete;

  ~AcpiWakeupHelper() override;

  // Forces use of a fake implementation instead of /proc/acpi/wakeup. Only for
  // testing.
  void set_file_for_testing(std::unique_ptr<AcpiWakeupFileInterface> file);

  // Implementation of AcpiWakeupHelperInterface.
  bool IsSupported() override;
  bool GetWakeupEnabled(const std::string& device_name,
                        bool* enabled_out) override;
  bool SetWakeupEnabled(const std::string& device_name, bool enabled) override;

 private:
  // Toggles ACPI wakeup for a given device. Used internally by
  // SetWakeupEnabled, since the kernel interface does not expose an interface
  // to set it directly.
  bool ToggleWakeupEnabled(const std::string& device_name);

  std::unique_ptr<AcpiWakeupFileInterface> file_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_ACPI_WAKEUP_HELPER_H_
