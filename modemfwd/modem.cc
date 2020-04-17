// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem.h"

#include <map>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/unguessable_token.h>
#include <chromeos/dbus/service_constants.h>

#include "modemfwd/modem_helper.h"

namespace modemfwd {

class ModemImpl : public Modem {
 public:
  ModemImpl(const std::string& device_id,
            const std::string& equipment_id,
            const std::string& carrier_id,
            ModemHelper* helper)
      : device_id_(device_id),
        equipment_id_(equipment_id),
        carrier_id_(carrier_id),
        helper_(helper) {
    if (!helper->GetFirmwareInfo(&installed_firmware_))
      LOG(WARNING) << "Could not fetch installed firmware information";
  }
  ~ModemImpl() override = default;

  // modemfwd::Modem overrides.
  std::string GetDeviceId() const override { return device_id_; }

  std::string GetEquipmentId() const override { return equipment_id_; }

  std::string GetCarrierId() const override { return carrier_id_; }

  std::string GetMainFirmwareVersion() const override {
    return installed_firmware_.main_version;
  }

  std::string GetCarrierFirmwareId() const override {
    return installed_firmware_.carrier_uuid;
  }

  std::string GetCarrierFirmwareVersion() const override {
    return installed_firmware_.carrier_version;
  }

  bool FlashMainFirmware(const base::FilePath& path_to_fw,
                         const std::string& version) override {
    return helper_->FlashMainFirmware(path_to_fw, version);
  }

  bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                            const std::string& version) override {
    return helper_->FlashCarrierFirmware(path_to_fw, version);
  }

 private:
  std::string device_id_;
  std::string equipment_id_;
  std::string carrier_id_;
  FirmwareInfo installed_firmware_;
  ModemHelper* helper_;

  DISALLOW_COPY_AND_ASSIGN(ModemImpl);
};

std::unique_ptr<Modem> CreateModem(
    std::unique_ptr<org::chromium::flimflam::DeviceProxy> device,
    ModemHelperDirectory* helper_directory) {
  std::string object_path = device->GetObjectPath().value();
  DVLOG(1) << "Creating modem proxy for " << object_path;

  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;
  if (!device->GetProperties(&properties, &error)) {
    LOG(WARNING) << "Could not get properties for modem " << object_path;
    return nullptr;
  }

  // If we don't have a device ID, modemfwd can't do anything with this modem,
  // so check it first and just return if we can't find it.
  std::string device_id;
  if (!properties[shill::kDeviceIdProperty].GetValue(&device_id)) {
    LOG(INFO) << "Modem " << object_path << " has no device ID, ignoring";
    return nullptr;
  }

  // Equipment ID is also pretty important since we use it as a stable
  // identifier that can distinguish between modems of the same type.
  std::string equipment_id;
  if (!properties[shill::kEquipmentIdProperty].GetValue(&equipment_id)) {
    LOG(INFO) << "Modem " << object_path << " has no equipment ID, ignoring";
    return nullptr;
  }

  // This property may not exist and it's not a big deal if it doesn't.
  std::map<std::string, std::string> operator_info;
  std::string carrier_id;
  if (properties[shill::kHomeProviderProperty].GetValue(&operator_info))
    carrier_id = operator_info[shill::kOperatorUuidKey];

  // Use the device ID to grab a helper.
  ModemHelper* helper = helper_directory->GetHelperForDeviceId(device_id);
  if (!helper) {
    LOG(INFO) << "No helper found to update modems with ID [" << device_id
              << "]";
    return nullptr;
  }

  return std::make_unique<ModemImpl>(device_id, equipment_id, carrier_id,
                                     helper);
}

// StubModem acts like a modem with a particular device ID but does not
// actually talk to a real modem. This allows us to use it for force-
// flashing.
class StubModem : public Modem {
 public:
  StubModem(const std::string& device_id, ModemHelper* helper)
      : device_id_(device_id),
        equipment_id_(base::UnguessableToken().ToString()),
        helper_(helper) {}
  ~StubModem() override = default;

  // modemfwd::Modem overrides.
  std::string GetDeviceId() const override { return device_id_; }

  std::string GetEquipmentId() const override { return equipment_id_; }

  std::string GetCarrierId() const override { return ""; }

  std::string GetMainFirmwareVersion() const override { return ""; }

  std::string GetCarrierFirmwareId() const override { return ""; }

  std::string GetCarrierFirmwareVersion() const override { return ""; }

  bool FlashMainFirmware(const base::FilePath& path_to_fw,
                         const std::string& version) override {
    return helper_->FlashMainFirmware(path_to_fw, version);
  }

  bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                            const std::string& version) override {
    return helper_->FlashCarrierFirmware(path_to_fw, version);
  }

 private:
  std::string device_id_;
  std::string equipment_id_;
  ModemHelper* helper_;

  DISALLOW_COPY_AND_ASSIGN(StubModem);
};

std::unique_ptr<Modem> CreateStubModem(const std::string& device_id,
                                       ModemHelperDirectory* helper_directory) {
  // Use the device ID to grab a helper.
  ModemHelper* helper = helper_directory->GetHelperForDeviceId(device_id);
  if (!helper) {
    LOG(INFO) << "No helper found to update modems with ID [" << device_id
              << "]";
    return nullptr;
  }

  return std::make_unique<StubModem>(device_id, helper);
}

}  // namespace modemfwd
