// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/advertising_manager_interface_handler.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <dbus/property.h>

#include "bluetooth/newblued/util.h"
#include "bluetooth/newblued/uuid.h"

namespace bluetooth {

class AdvertisementProperties : public dbus::PropertySet {
 public:
  AdvertisementProperties(dbus::ObjectProxy* object_proxy,
                          const base::Callback<bool()>& on_removed)
      : dbus::PropertySet(
            object_proxy,
            bluetooth_advertisement::kBluetoothAdvertisementInterface,
            base::Bind(&AdvertisementProperties::OnChange,
                       base::Unretained(this),
                       on_removed)) {
    RegisterProperty(bluetooth_advertisement::kTypeProperty, &type);
    RegisterProperty(bluetooth_advertisement::kIncludeTxPowerProperty,
                     &include_tx_power);
    RegisterProperty(bluetooth_advertisement::kServiceUUIDsProperty,
                     &service_uuids);
    RegisterProperty(bluetooth_advertisement::kSolicitUUIDsProperty,
                     &solicit_uuids);
    RegisterProperty(bluetooth_advertisement::kManufacturerDataProperty,
                     &manufacturer_data);
    RegisterProperty(bluetooth_advertisement::kServiceDataProperty,
                     &service_data);
  }

  bool Init(brillo::ErrorPtr* error) {
    dbus::MethodCall method_call(dbus::kPropertiesInterface,
                                 dbus::kPropertiesGetAll);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(interface());
    std::unique_ptr<dbus::Response> response(object_proxy()->CallMethodAndBlock(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
    if (!response) {
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           bluetooth_advertising_manager::kErrorDoesNotExist,
                           "Advertisement object does not exist");
      return false;
    }

    OnGetAll(response.get());
    return true;
  }

  void OnChange(const base::Callback<bool()>& on_removed,
                const std::string& name) {
    // An advertisement object is considered removed if "Type" is not valid,
    // which is a mandatory property.
    if (name == bluetooth_advertisement::kTypeProperty && !type.is_valid())
      on_removed.Run();
  }

  dbus::Property<std::string> type;
  dbus::Property<bool> include_tx_power;
  dbus::Property<std::vector<std::string>> service_uuids;
  dbus::Property<std::vector<std::string>> solicit_uuids;
  dbus::Property<std::map<uint16_t, std::vector<uint8_t>>> manufacturer_data;
  dbus::Property<std::map<std::string, std::vector<uint8_t>>> service_data;
};

AdvertisingManagerInterfaceHandler::AdvertisingManagerInterfaceHandler(
    LibNewblue* libnewblue,
    scoped_refptr<dbus::Bus> bus,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper)
    : libnewblue_(libnewblue),
      bus_(bus),
      exported_object_manager_wrapper_(exported_object_manager_wrapper) {}

void AdvertisingManagerInterfaceHandler::Init() {
  dbus::ObjectPath adapter_object_path(kAdapterObjectPath);
  exported_object_manager_wrapper_->AddExportedInterface(
      adapter_object_path,
      bluetooth_advertising_manager::kBluetoothAdvertisingManagerInterface,
      base::Bind(&ExportedObjectManagerWrapper::SetupStandardPropertyHandlers));
  ExportedInterface* advertising_manager_interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          adapter_object_path,
          bluetooth_advertising_manager::kBluetoothAdvertisingManagerInterface);

  advertising_manager_interface
      ->EnsureExportedPropertyRegistered<bool>(
          bluetooth_advertising_manager::kIsTXPowerSupportedProperty)
      ->SetValue(libnewblue_->HciAdvIsPowerLevelSettingSupported());
  advertising_manager_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_advertising_manager::kRegisterAdvertisement,
      base::Unretained(this),
      &AdvertisingManagerInterfaceHandler::HandleRegisterAdvertisement);
  advertising_manager_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_advertising_manager::kUnregisterAdvertisement,
      base::Unretained(this),
      &AdvertisingManagerInterfaceHandler::HandleUnregisterAdvertisement);
  advertising_manager_interface->ExportAndBlock();
}

bool AdvertisingManagerInterfaceHandler::HandleRegisterAdvertisement(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    dbus::ObjectPath object_path,
    brillo::VariantDictionary options) {
  if (base::Contains(handles_, object_path)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorAlreadyExists,
                         "Advertisement already registered");
    return false;
  }

  hci_adv_set_t handle = libnewblue_->HciAdvSetAllocate();
  if (!handle) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorFailed,
                         "Cannot allocate advertisement handle");
    return false;
  }

  AdvertisementProperties properties(
      bus_->GetObjectProxy(message->GetSender(), object_path),
      base::Bind(
          &AdvertisingManagerInterfaceHandler::HandleUnregisterAdvertisement,
          base::Unretained(this), nullptr, nullptr, object_path));
  std::vector<uint8_t> data;
  if (!properties.Init(error) || !ConstructData(properties, &data, error) ||
      !ConfigureData(handle, data, error) || !SetParams(handle, error) ||
      !Enable(handle, error)) {
    libnewblue_->HciAdvSetFree(handle);
    return false;
  }

  handles_[object_path] = handle;
  return true;
}

bool AdvertisingManagerInterfaceHandler::HandleUnregisterAdvertisement(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    dbus::ObjectPath object_path) {
  auto it = handles_.find(object_path);
  if (it == handles_.end()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorDoesNotExist,
                         "Advertisement not registered");
    return false;
  }

  libnewblue_->HciAdvSetDisable(it->second);
  libnewblue_->HciAdvSetFree(it->second);
  handles_.erase(it);
  return true;
}

bool AdvertisingManagerInterfaceHandler::AddType(const std::string& type,
                                                 std::vector<uint8_t>* data,
                                                 brillo::ErrorPtr* error) {
  constexpr uint8_t kGeneralDiscoverable = 2;
  if (type == bluetooth_advertisement::kTypeBroadcast)
    return true;

  if (type == bluetooth_advertisement::kTypePeripheral) {
    data->push_back(2);
    data->push_back(HCI_EIR_TYPE_FLAGS);
    data->push_back(kGeneralDiscoverable);
    return true;
  }

  brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                       bluetooth_advertising_manager::kErrorInvalidArguments,
                       "Advertisement type invalid");
  return false;
}

bool AdvertisingManagerInterfaceHandler::AddIncludeTxPower(
    bool include_tx_power,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  if (!include_tx_power)
    return true;

  data->push_back(2);
  data->push_back(HCI_EIR_TX_POWER_LEVEL);
  data->push_back(HCI_ADV_TX_PWR_LVL_DONT_CARE);
  return true;
}

bool AdvertisingManagerInterfaceHandler::AddServiceUuid(
    const std::vector<std::string>& service_uuids,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  for (const std::string& service_uuid : service_uuids) {
    Uuid uuid(service_uuid);
    if (uuid.format() == UuidFormat::UUID_INVALID) {
      brillo::Error::AddTo(
          error, FROM_HERE, brillo::errors::dbus::kDomain,
          bluetooth_advertising_manager::kErrorInvalidArguments,
          "Service uuid invalid");
      return false;
    }

    data->push_back(kUuid128Size + 1);
    data->push_back(HCI_EIR_TYPE_COMPL_LIST_UUID128);
    data->insert(data->end(), uuid.value().rbegin(), uuid.value().rend());
  }
  return true;
}

bool AdvertisingManagerInterfaceHandler::AddSolicitUuid(
    const std::vector<std::string>& solicit_uuids,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  for (const std::string& solicit_uuid : solicit_uuids) {
    Uuid uuid(solicit_uuid);
    if (uuid.format() == UuidFormat::UUID_INVALID) {
      brillo::Error::AddTo(
          error, FROM_HERE, brillo::errors::dbus::kDomain,
          bluetooth_advertising_manager::kErrorInvalidArguments,
          "Solicit uuid invalid");
      return false;
    }

    data->push_back(kUuid128Size + 1);
    data->push_back(HCI_EIR_SVC_SOLICITS_UUID128);
    data->insert(data->end(), uuid.value().rbegin(), uuid.value().rend());
  }
  return true;
}

bool AdvertisingManagerInterfaceHandler::AddServiceData(
    const std::map<std::string, std::vector<uint8_t>>& service_data,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  for (const auto& uuid_data : service_data) {
    Uuid uuid(uuid_data.first);
    if (uuid.format() == UuidFormat::UUID_INVALID) {
      brillo::Error::AddTo(
          error, FROM_HERE, brillo::errors::dbus::kDomain,
          bluetooth_advertising_manager::kErrorInvalidArguments,
          "Service data uuid invalid");
      return false;
    }

    size_t length = uuid_data.second.size() + kUuid128Size + 1;
    if (length > UINT8_MAX) {
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           bluetooth_advertising_manager::kErrorInvalidLength,
                           "Service data too long");
      return false;
    }

    data->push_back(length);
    data->push_back(HCI_EIR_SVC_DATA_UUID128);
    data->insert(data->end(), uuid.value().rbegin(), uuid.value().rend());
    data->insert(data->end(), uuid_data.second.rbegin(),
                 uuid_data.second.rend());
  }
  return true;
}

bool AdvertisingManagerInterfaceHandler::AddManufacturerData(
    const std::map<uint16_t, std::vector<uint8_t>>& manufacturer_data,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  for (const auto& id_data : manufacturer_data) {
    size_t length = id_data.second.size() + sizeof(uint16_t) + 1;
    if (length > UINT8_MAX) {
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           bluetooth_advertising_manager::kErrorInvalidLength,
                           "Manufacturer data too long");
      return false;
    }

    data->push_back(length);
    data->push_back(HCI_EIR_MANUF_DATA);
    data->push_back(id_data.first & 0xff);
    data->push_back(id_data.first >> 8);
    data->insert(data->end(), id_data.second.rbegin(), id_data.second.rend());
  }
  return true;
}

bool AdvertisingManagerInterfaceHandler::ConstructData(
    const AdvertisementProperties& properties,
    std::vector<uint8_t>* data,
    brillo::ErrorPtr* error) {
  return AddType(properties.type.value(), data, error) &&
         AddIncludeTxPower(properties.include_tx_power.value(), data, error) &&
         AddServiceUuid(properties.service_uuids.value(), data, error) &&
         AddSolicitUuid(properties.solicit_uuids.value(), data, error) &&
         AddServiceData(properties.service_data.value(), data, error) &&
         AddManufacturerData(properties.manufacturer_data.value(), data, error);
}

bool AdvertisingManagerInterfaceHandler::ConfigureData(
    hci_adv_set_t handle,
    const std::vector<uint8_t>& data,
    brillo::ErrorPtr* error) {
  if (!libnewblue_->HciAdvSetConfigureData(handle, false, data.data(),
                                           data.size())) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorFailed,
                         "Cannot configure data");
    return false;
  }

  return true;
}

bool AdvertisingManagerInterfaceHandler::SetParams(hci_adv_set_t handle,
                                                   brillo::ErrorPtr* error) {
  // Some chips can only support HCI_ADV_TYPE_ADV_IND.
  if (!libnewblue_->HciAdvSetSetAdvParams(
          handle, /* min_interval = */ 0x0040, /* max_interval = */ 0x0100,
          HCI_ADV_TYPE_ADV_IND, HCI_ADV_OWN_ADDR_TYPE_PUBLIC,
          /* address = */ nullptr,
          HCI_ADV_CHAN_MAP_USE_CHAN_37 | HCI_ADV_CHAN_MAP_USE_CHAN_38 |
              HCI_ADV_CHAN_MAP_USE_CHAN_39,
          HCI_ADV_FILTER_POL_SCAN_ALL_CONNECT_ALL,
          HCI_ADV_TX_PWR_LVL_DONT_CARE)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorFailed,
                         "Cannot set parameters");
    return false;
  }

  return true;
}

bool AdvertisingManagerInterfaceHandler::Enable(hci_adv_set_t handle,
                                                brillo::ErrorPtr* error) {
  if (!libnewblue_->HciAdvSetEnable(handle)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_advertising_manager::kErrorFailed,
                         "Cannot enable advertisement");
    return false;
  }

  return true;
}

}  // namespace bluetooth
