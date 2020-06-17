// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/adapter_interface_handler.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>

#include "bluetooth/common/exported_object_manager_wrapper.h"
#include "bluetooth/newblued/newblue.h"
#include "bluetooth/newblued/scan_manager.h"
#include "bluetooth/newblued/util.h"
#include "bluetooth/newblued/uuid.h"

namespace bluetooth {

namespace {
constexpr char kNewblueAdapterName[] = "NewBlue";
constexpr char kNewblueAdapterAddress[] = "AA:BB:CC:DD:EE:FF";
constexpr char kGapUUID[] = "00001800-0000-1000-8000-00805f9b34fb";
constexpr char kGattUUID[] = "00001801-0000-1000-8000-00805f9b34fb";

// A string to FilterKeys mapping table, used to convert keys of scan
// parameters into NewBlue standardized enum.
const std::map<std::string, FilterKeys> kFilterKeyTable = {
    {"RSSI", FilterKeys::RSSI},
    {"Pathloss", FilterKeys::PATHLOSS},
    {"UUIDs", FilterKeys::UUIDS}};

// Check and convert the key of scan filter parameters into NewBlue standardized
// enum.
FilterKeys ConvertFilterKey(const std::string& key) {
  const auto it = kFilterKeyTable.find(key);
  if (it != kFilterKeyTable.end())
    return it->second;

  return FilterKeys::INVALID;
}

// Translate the filter properties into a NewBlue scan dictionary. Translation
// will filter out the non-supported feature, check data format, and converting
// UUID into NewBlue formatting.
bool TranslateDiscoveryFilter(
    const brillo::VariantDictionary& filter_properties,
    brillo::VariantDictionary* filter) {
  VLOG(1) << __func__;

  // If the dict is empty, client is using the method to remove existing filter.
  if (filter_properties.empty())
    return true;

  for (const auto& pair : filter_properties) {
    switch (ConvertFilterKey(pair.first)) {
      case FilterKeys::INVALID:
        // The keyword for this parameter is not supported by NewBlue, ignored.
        break;
      case FilterKeys::RSSI:
        if (!pair.second.IsTypeCompatible<int16_t>())
          return false;
        filter->emplace(pair.first, pair.second);
        break;
      case FilterKeys::PATHLOSS:
        if (!pair.second.IsTypeCompatible<uint16_t>())
          return false;
        filter->emplace(pair.first, pair.second);
        break;
      case FilterKeys::UUIDS:
        // Convert the UUID table into NewBlue formatting.
        std::set<Uuid> uuids;
        auto uuids_string = pair.second.Get<std::vector<std::string>>();

        for (auto& uuid_string : uuids_string) {
          uuids.insert(Uuid(uuid_string));
        }
        if (uuids.empty())
          return false;
        filter->emplace(pair.first, uuids);
        break;
    }
  }
  if (filter->empty())
    return false;
  return true;
}

}  // namespace

AdapterInterfaceHandler::AdapterInterfaceHandler(
    scoped_refptr<dbus::Bus> bus,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper)
    : bus_(bus),
      exported_object_manager_wrapper_(exported_object_manager_wrapper),
      weak_ptr_factory_(this) {}

void AdapterInterfaceHandler::Init(
    DeviceInterfaceHandler* device_interface_handler, Newblue* newblue) {
  CHECK(device_interface_handler != nullptr);
  CHECK(newblue != nullptr);

  device_interface_handler_ = device_interface_handler;
  dbus::ObjectPath adapter_object_path(kAdapterObjectPath);
  exported_object_manager_wrapper_->AddExportedInterface(
      adapter_object_path, bluetooth_adapter::kBluetoothAdapterInterface,
      base::Bind(&ExportedObjectManagerWrapper::SetupStandardPropertyHandlers));
  ExportedInterface* adapter_interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          adapter_object_path, bluetooth_adapter::kBluetoothAdapterInterface);
  scan_manager_ = std::make_unique<ScanManager>(
      newblue, device_interface_handler, adapter_interface);
  // Expose the "Powered" property of the adapter. This property is only
  // controlled by BlueZ, so newblued's "Powered" property is ignored by
  // btdispatch. However, it is useful to have the dummy "Powered" property
  // for testing when Chrome (or any client) connects directly to newblued
  // instead of via btdispatch.
  adapter_interface
      ->EnsureExportedPropertyRegistered<bool>(
          bluetooth_adapter::kPoweredProperty)
      ->SetValue(true);
  adapter_interface
      ->EnsureExportedPropertyRegistered<bool>(
          bluetooth_adapter::kStackSyncQuittingProperty)
      ->SetValue(false);
  adapter_interface
      ->EnsureExportedPropertyRegistered<std::string>(
          bluetooth_adapter::kNameProperty)
      ->SetValue(kNewblueAdapterName);
  const std::vector<std::string> uuids = {kGapUUID, kGattUUID};
  adapter_interface
      ->EnsureExportedPropertyRegistered<std::vector<std::string>>(
          bluetooth_adapter::kUUIDsProperty)
      ->SetValue(uuids);
  adapter_interface
      ->EnsureExportedPropertyRegistered<std::string>(
          bluetooth_adapter::kAddressProperty)
      ->SetValue(kNewblueAdapterAddress);

  adapter_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_adapter::kSetDiscoveryFilter, base::Unretained(this),
      &AdapterInterfaceHandler::HandleSetDiscoveryFilter);
  adapter_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_adapter::kStartDiscovery, base::Unretained(this),
      &AdapterInterfaceHandler::HandleStartDiscovery);
  adapter_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_adapter::kStopDiscovery, base::Unretained(this),
      &AdapterInterfaceHandler::HandleStopDiscovery);
  adapter_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_adapter::kRemoveDevice, base::Unretained(this),
      &AdapterInterfaceHandler::HandleRemoveDevice);

  adapter_interface->AddMethodHandlerWithMessage(
      bluetooth_adapter::kHandleSuspendImminent,
      base::Bind(&AdapterInterfaceHandler::HandleSuspendImminent,
                 base::Unretained(this)));
  adapter_interface->AddMethodHandlerWithMessage(
      bluetooth_adapter::kHandleSuspendDone,
      base::Bind(&AdapterInterfaceHandler::HandleSuspendDone,
                 base::Unretained(this)));

  suspend_resume_state_ = SuspendResumeState::RUNNING;

  adapter_interface->ExportAndBlock();
}

bool AdapterInterfaceHandler::HandleSetDiscoveryFilter(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const brillo::VariantDictionary& filter_properties) {
  VLOG(1) << __func__;

  const std::string& client_address = message->GetSender();

  // If transport parameter is BR/EDR, the method is not for LE scan. Ignore the
  // request.
  if ("bredr" == brillo::GetVariantValueOrDefault<std::string>(
                     filter_properties, "Transport")) {
    VLOG(2) << "Scan filter is for BR/EDR only, ignored by NewBlue. Client: "
            << client_address;
    return true;
  }

  brillo::VariantDictionary filter;

  if (!TranslateDiscoveryFilter(filter_properties, &filter)) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_adapter::kErrorFailed,
        "Scan Filter contains no valid or ill-format parameters.");
    return false;
  }

  if (!scan_manager_->SetFilter(client_address, filter)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_adapter::kErrorFailed,
                         "Failed to set discovery filter");
    return false;
  }
  return true;
}

bool AdapterInterfaceHandler::HandleStartDiscovery(brillo::ErrorPtr* error,
                                                   dbus::Message* message) {
  VLOG(1) << __func__;

  const std::string& client_address = message->GetSender();

  if (base::Contains(discovery_clients_, client_address)) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_adapter::kErrorInProgress,
        base::StringPrintf("Client already has a discovery session: %s",
                           client_address.c_str()));
    return false;
  }

  if (!scan_manager_->StartScan(client_address)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_adapter::kErrorFailed,
                         "Failed to start discovery");
    return false;
  }

  discovery_clients_[client_address] =
      std::make_unique<DBusClient>(bus_, client_address);
  discovery_clients_[client_address]->WatchClientUnavailable(
      base::Bind(&AdapterInterfaceHandler::OnClientUnavailable,
                 weak_ptr_factory_.GetWeakPtr(), client_address));

  return true;
}

bool AdapterInterfaceHandler::HandleStopDiscovery(brillo::ErrorPtr* error,
                                                  dbus::Message* message) {
  VLOG(1) << __func__;

  const std::string& client_address = message->GetSender();

  if (!base::Contains(discovery_clients_, client_address)) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_adapter::kErrorFailed,
        base::StringPrintf("Client doesn't have a discovery session: %s",
                           client_address.c_str()));
    return false;
  }

  if (!scan_manager_->StopScan(client_address)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_adapter::kErrorFailed,
                         "Failed to stop discovery");
    return false;
  }

  discovery_clients_.erase(client_address);

  return true;
}

bool AdapterInterfaceHandler::HandleRemoveDevice(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const dbus::ObjectPath& device_path) {
  VLOG(1) << __func__;

  std::string device_address =
      ConvertDeviceObjectPathToAddress(device_path.value());

  std::string dbus_error;
  if (!device_interface_handler_->RemoveDevice(device_address, &dbus_error)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         bluetooth_adapter::kErrorFailed, dbus_error);
    return false;
  }

  return true;
}

void AdapterInterfaceHandler::OnClientUnavailable(
    const std::string& client_address) {
  VLOG(1) << "Discovery client becomes unavailable, address " << client_address;

  if (!scan_manager_->StopScan(client_address))
    LOG(ERROR) << "Failed to stop discovery on client unavailable.";

  discovery_clients_.erase(client_address);
}

void AdapterInterfaceHandler::HandleSuspendImminent(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message,
    const std::string& action) {
  VLOG(1) << __func__;
  CHECK(message != nullptr);
  CHECK(response != nullptr);

  UpdateSuspendResumeState(SuspendResumeState::SUSPEND_IMMINT);
  suspend_response_ = std::move(response);

  // Perform suspend tasks.
  PauseUnpauseDiscovery();
}

void AdapterInterfaceHandler::HandleSuspendDone(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message,
    const std::string& action) {
  VLOG(1) << __func__;
  CHECK(message != nullptr);
  CHECK(response != nullptr);

  UpdateSuspendResumeState(SuspendResumeState::SUSPEND_DONE);
  suspend_response_ = std::move(response);

  // Perform resume tasks.
  PauseUnpauseDiscovery();
}

void AdapterInterfaceHandler::PauseUnpauseDiscovery(void) {
  VLOG(1) << __func__;

  // Flip the corresponding task bit.
  UpdateSuspendResumeTasks(SuspendResumeTask::PAUSE_UNPAUSE_DISCOVERY, false);
  if (!scan_manager_->UpdateScanSuspensionState(is_in_suspension_))
    return;

  // Update discovery is synchronous function call. If async, call the following
  // update function in the callback instead of here.
  UpdateSuspendResumeTasks(SuspendResumeTask::PAUSE_UNPAUSE_DISCOVERY, true);
}

void AdapterInterfaceHandler::UpdateSuspendResumeTasks(SuspendResumeTask task,
                                                       bool is_completed) {
  VLOG(1) << __func__;

  // Update suspend_resume_tasks_ bit map. Clear the corresponding bit if task
  // completed, set the bit to 1 otherwise.
  if (!is_completed) {
    suspend_resume_tasks_ |= task;
    return;
  }
  suspend_resume_tasks_ &= ~task;

  if (SuspendResumeTask::NONE != suspend_resume_tasks_)
    return;

  if (SuspendResumeState::SUSPEND_IMMINT == suspend_resume_state_)
    UpdateSuspendResumeState(SuspendResumeState::SUSPEND_IMMINT_ACKED);
  else
    UpdateSuspendResumeState(SuspendResumeState::RUNNING);
}

void AdapterInterfaceHandler::UpdateSuspendResumeState(
    SuspendResumeState new_state) {
  // No state transition.
  if (new_state == suspend_resume_state_)
    return;

  VLOG(1) << "Suspend/resume state transition from: " << suspend_resume_state_
          << " to: " << new_state;
  switch (new_state) {
    case SuspendResumeState::RUNNING:
      if (SuspendResumeState::SUSPEND_DONE == suspend_resume_state_) {
        suspend_response_->SendRawResponse(
            suspend_response_->CreateCustomResponse());
      }
      break;
    case SuspendResumeState::SUSPEND_IMMINT:
      if (SuspendResumeState::RUNNING != suspend_resume_state_)
        LOG(WARNING) << "Suspend imminent called in wrong state.";
      is_in_suspension_ = true;
      suspend_resume_tasks_ = SuspendResumeTask::NONE;
      break;
    case SuspendResumeState::SUSPEND_IMMINT_ACKED:
      suspend_response_->SendRawResponse(
          suspend_response_->CreateCustomResponse());
      break;
    case SuspendResumeState::SUSPEND_DONE:
      if (SuspendResumeState::SUSPEND_IMMINT_ACKED != suspend_resume_state_)
        LOG(WARNING) << "Suspend Done called in wrong state.";
      is_in_suspension_ = false;
      break;
    default:
      LOG(ERROR) << "Invalid suspend resume state.";
      break;
  }
  suspend_resume_state_ = new_state;
}

}  // namespace bluetooth
