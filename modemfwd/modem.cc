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
#include <brillo/dbus/dbus_method_invoker.h>
#include <chromeos/dbus/service_constants.h>
#include <ModemManager/ModemManager.h>

#include "modemfwd/logging.h"
#include "modemfwd/modem_helper.h"
#include "modemmanager/dbus-proxies.h"

namespace {

class Inhibitor {
 public:
  Inhibitor(std::unique_ptr<org::freedesktop::ModemManager1Proxy> mm_proxy,
            const std::string& physdev_uid)
      : mm_proxy_(std::move(mm_proxy)), physdev_uid_(physdev_uid) {}

  bool SetInhibited(bool inhibited) {
    brillo::ErrorPtr error_unused;
    return mm_proxy_->InhibitDevice(physdev_uid_, inhibited, &error_unused);
  }

 private:
  std::unique_ptr<org::freedesktop::ModemManager1Proxy> mm_proxy_;
  std::string physdev_uid_;
};

std::unique_ptr<Inhibitor> GetInhibitor(
    scoped_refptr<dbus::Bus> bus, const dbus::ObjectPath& mm_object_path) {
  // Get the MM object backing this modem, and retrieve its Device property.
  // This is the mm_physdev_uid we use for inhibition during updates.
  auto mm_device = bus->GetObjectProxy(modemmanager::kModemManager1ServiceName,
                                       mm_object_path);
  if (!mm_device)
    return nullptr;

  brillo::ErrorPtr error;
  auto resp = brillo::dbus_utils::CallMethodAndBlock(
      mm_device, dbus::kDBusPropertiesInterface, dbus::kDBusPropertiesGet,
      &error, std::string(modemmanager::kModemManager1ModemInterface),
      std::string(MM_MODEM_PROPERTY_DEVICE));
  if (!resp)
    return nullptr;

  std::string mm_physdev_uid;
  if (!brillo::dbus_utils::ExtractMethodCallResults(resp.get(), &error,
                                                    &mm_physdev_uid)) {
    return nullptr;
  }

  EVLOG(1) << "Modem " << mm_object_path.value() << " has physdev UID "
           << mm_physdev_uid;
  auto mm_proxy = std::make_unique<org::freedesktop::ModemManager1Proxy>(
      bus, modemmanager::kModemManager1ServiceName);
  return std::make_unique<Inhibitor>(std::move(mm_proxy), mm_physdev_uid);
}

}  // namespace

namespace modemfwd {

class ModemImpl : public Modem {
 public:
  ModemImpl(const std::string& device_id,
            const std::string& equipment_id,
            const std::string& carrier_id,
            std::unique_ptr<Inhibitor> inhibitor,
            ModemHelper* helper)
      : device_id_(device_id),
        equipment_id_(equipment_id),
        carrier_id_(carrier_id),
        inhibitor_(std::move(inhibitor)),
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

  bool SetInhibited(bool inhibited) override {
    if (!inhibitor_) {
      EVLOG(1) << "Inhibiting unavailable on this modem";
      return false;
    }
    return inhibitor_->SetInhibited(inhibited);
  }

  bool FlashMainFirmware(const base::FilePath& path_to_fw,
                         const std::string& version) override {
    return helper_->FlashMainFirmware(path_to_fw, version);
  }

  bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                            const std::string& version) override {
    return helper_->FlashCarrierFirmware(path_to_fw, version);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    return helper_->ClearAttachAPN(carrier_uuid);
  }

 private:
  std::string device_id_;
  std::string equipment_id_;
  std::string carrier_id_;
  std::unique_ptr<Inhibitor> inhibitor_;
  FirmwareInfo installed_firmware_;
  ModemHelper* helper_;

  DISALLOW_COPY_AND_ASSIGN(ModemImpl);
};

std::unique_ptr<Modem> CreateModem(
    scoped_refptr<dbus::Bus> bus,
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

  // Get a helper object for inhibiting the modem, if possible.
  std::unique_ptr<Inhibitor> inhibitor;
  std::string mm_object_path;
  if (!properties[shill::kDBusObjectProperty].GetValue(&mm_object_path)) {
    LOG(INFO) << "Modem " << object_path << " has no ModemManager object";
  } else {
    inhibitor = GetInhibitor(bus, dbus::ObjectPath(mm_object_path));
  }
  if (!inhibitor)
    LOG(INFO) << "Inhibiting modem " << object_path << " will not be possible";

  // Use the device ID to grab a helper.
  ModemHelper* helper = helper_directory->GetHelperForDeviceId(device_id);
  if (!helper) {
    LOG(INFO) << "No helper found to update modems with ID [" << device_id
              << "]";
    return nullptr;
  }

  return std::make_unique<ModemImpl>(device_id, equipment_id, carrier_id,
                                     std::move(inhibitor), helper);
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

  bool SetInhibited(bool inhibited) override { return true; }

  bool FlashMainFirmware(const base::FilePath& path_to_fw,
                         const std::string& version) override {
    return helper_->FlashMainFirmware(path_to_fw, version);
  }

  bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                            const std::string& version) override {
    return helper_->FlashCarrierFirmware(path_to_fw, version);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    return helper_->ClearAttachAPN(carrier_uuid);
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
