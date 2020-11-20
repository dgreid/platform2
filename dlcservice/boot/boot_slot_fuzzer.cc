// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>

#include <base/logging.h>

#include "dlcservice/boot/boot_device.h"
#include "dlcservice/boot/boot_slot.h"

namespace dlcservice {

// A fake BootDevice that returns fuzzed data.
class FakeBootDevice : public BootDeviceInterface {
 public:
  FakeBootDevice(const std::string boot_device, bool is_removable_device)
      : boot_device_(boot_device), is_removable_device_(is_removable_device) {}
  ~FakeBootDevice() override = default;

  // BootDeviceInterface overrides:
  bool IsRemovableDevice(const std::string& device) override {
    return is_removable_device_;
  }
  std::string GetBootDevice() override { return boot_device_; }

 private:
  std::string boot_device_;
  bool is_removable_device_;

  FakeBootDevice(const FakeBootDevice&) = delete;
  FakeBootDevice& operator=(const FakeBootDevice&) = delete;
};

}  // namespace dlcservice

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider fuzzed_data_provider(data, size);

  bool is_removable_device = fuzzed_data_provider.ConsumeBool();
  std::string boot_device =
      fuzzed_data_provider.ConsumeRemainingBytesAsString();

  dlcservice::BootSlot boot_slot(std::make_unique<dlcservice::FakeBootDevice>(
      boot_device, is_removable_device));

  std::string boot_disk_name;
  dlcservice::BootSlot::Slot slot;
  boot_slot.GetCurrentSlot(&boot_disk_name, &slot);
  return 0;
}
