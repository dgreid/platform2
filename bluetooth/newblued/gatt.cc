// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/gatt.h"
#include "bluetooth/newblued/util.h"

#include <string>
#include <utility>

namespace bluetooth {

namespace {

constexpr char kGattHumanInterfaceDeviceServiceUuid[] =
    "00001812-0000-1000-8000-00805f9b34fb";

// TODO(b:134425062): Implement profile manager so that the blacklist can be
// removed.
// Some GATT service should be kept only for newblued used due to security
// concern, and this works as a blacklist-filter.
bool IsHiddenGattService(const GattService& service) {
  std::string uuid = service.uuid().value().canonical_value();
  if (uuid.compare(kGattHumanInterfaceDeviceServiceUuid) == 0)
    return true;
  return false;
}

// Converts AttError to CattClientOperationError based on BlueZ.
GattClientOperationError ConvertToClientOperationError(AttError error) {
  switch (error) {
    case AttError::NONE:
      return GattClientOperationError::NONE;
    case AttError::READ_NOT_ALLOWED:
      return GattClientOperationError::READ_NOT_ALLOWED;
    case AttError::WRITE_NOT_ALLOWED:
      return GattClientOperationError::WRITE_NOT_ALLOWED;
    case AttError::INSUFF_AUTHN:
      return GattClientOperationError::INSUFF_AUTHN;
    case AttError::REQ_NOT_SUPPORTED:
      return GattClientOperationError::NOT_SUPPORTED;
    case AttError::INSUFF_AUTHZ:
      return GattClientOperationError::INSUFF_AUTHZ;
    case AttError::INVALID_OFFSET:
      return GattClientOperationError::INVALID_OFFSET;
    case AttError::INSUFF_ENCR_KEY_SIZE:
      return GattClientOperationError::INSUFF_ENCR_KEY_SIZE;
    case AttError::INVALID_ATTR_VALUE_LENGTH:
      return GattClientOperationError::INVALUD_ATTR_VALUE_LENGTH;
    case AttError::INSUFF_ENCR:
      return GattClientOperationError::INSUFF_ENC;
    default:  // This covers unknown AttError and the rest of AttError.
      return GattClientOperationError::OTHER;
  }
}

}  // namespace

Gatt::Gatt(Newblue* newblue, DeviceInterfaceHandler* device_interface_handler)
    : newblue_(newblue),
      device_interface_handler_(device_interface_handler),
      weak_ptr_factory_(this) {
  CHECK(newblue_);
  CHECK(device_interface_handler_);

  device_interface_handler_->AddDeviceObserver(this);
}

Gatt::~Gatt() {
  if (device_interface_handler_)
    device_interface_handler_->RemoveDeviceObserver(this);
}

void Gatt::AddGattObserver(GattObserver* observer) {
  CHECK(observer);
  observers_.AddObserver(observer);
}

void Gatt::RemoveGattObserver(GattObserver* observer) {
  CHECK(observer);
  observers_.RemoveObserver(observer);
}

UniqueId Gatt::ReadCharacteristicValue(
    const std::string& device_address,
    uint16_t service_handle,
    uint16_t char_handle,
    uint16_t offset,
    ReadCharacteristicValueCallback callback) {
  CHECK(!device_address.empty());
  CHECK(!callback.is_null());

  gatt_client_conn_t conn_id =
      device_interface_handler_->GetConnectionIdByAddress(device_address);
  if (conn_id == kInvalidGattConnectionId) {
    VLOG(1) << "Failed to read characteristic due to no connection with device "
            << device_address;
    return kInvalidUniqueId;
  }

  const auto characteristic =
      FindGattCharacteristic(device_address, service_handle, char_handle);
  if (characteristic == nullptr) {
    VLOG(1) << "Failed to locate characteristic with service handle "
            << service_handle << " characteristic handle " << char_handle
            << " for device " << device_address;
    return kInvalidUniqueId;
  }

  auto state = std::make_unique<ClientOperationState>(
      service_handle, char_handle, kInvalidGattAttributeHandle, offset);
  ClientOperation operation = {
      .operation_type = GattClientOperationType::READ_LONG_VALUE,
      .request_type = GattClientRequestType::READ_CHARACTERISTIC_VALUE,
      .conn_id = conn_id,
      .read_char_value_callback = std::move(callback)};
  auto transaction_id = GetNextId();
  transactions_.emplace(transaction_id, std::move(operation));

  uint16_t char_value_handle = characteristic->value_handle();
  auto status = newblue_->GattClientReadLongValue(
      conn_id, char_value_handle, GattClientOperationAuthentication::NONE,
      transaction_id,
      base::Bind(&Gatt::OnGattClientReadLongValue,
                 weak_ptr_factory_.GetWeakPtr(),
                 base::Passed(std::move(state))));
  if (status != GattClientOperationStatus::OK) {
    LOG(WARNING) << "Failed to read characteristic value with value handle "
                 << char_value_handle << " from device " << device_address
                 << " with conn ID " << conn_id << ", transaction "
                 << transaction_id;

    transactions_.erase(transaction_id);
    return kInvalidUniqueId;
  }

  VLOG(1) << "Read characteristic value with value handle " << char_value_handle
          << " from device " << device_address << " with conn ID " << conn_id;

  return transaction_id;
}

UniqueId Gatt::ReadDescriptorValue(const std::string& device_address,
                                   uint16_t service_handle,
                                   uint16_t char_handle,
                                   uint16_t desc_handle,
                                   uint16_t offset,
                                   ReadDescriptorValueCallback callback) {
  CHECK(!device_address.empty());
  CHECK(!callback.is_null());

  gatt_client_conn_t conn_id =
      device_interface_handler_->GetConnectionIdByAddress(device_address);
  if (conn_id == kInvalidGattConnectionId) {
    VLOG(1) << "Failed to read descriptor due to no connection with device "
            << device_address;
    return kInvalidUniqueId;
  }

  const auto descriptor = FindGattDescriptor(device_address, service_handle,
                                             char_handle, desc_handle);
  if (descriptor == nullptr) {
    VLOG(1) << "Failed to locate descriptor with service handle "
            << service_handle << " characteristic handle " << char_handle
            << " descriptor handle " << desc_handle << " for device "
            << device_address;
    return kInvalidUniqueId;
  }

  auto state = std::make_unique<ClientOperationState>(
      service_handle, char_handle, desc_handle, offset);
  ClientOperation operation = {
      .operation_type = GattClientOperationType::READ_LONG_VALUE,
      .request_type = GattClientRequestType::READ_DESCRIPTOR_VALUE,
      .conn_id = conn_id,
      .read_desc_value_callback = std::move(callback)};
  auto transaction_id = GetNextId();
  transactions_.emplace(transaction_id, std::move(operation));

  auto status = newblue_->GattClientReadLongValue(
      conn_id, desc_handle, GattClientOperationAuthentication::NONE,
      transaction_id,
      base::Bind(&Gatt::OnGattClientReadLongValue,
                 weak_ptr_factory_.GetWeakPtr(),
                 base::Passed(std::move(state))));
  if (status != GattClientOperationStatus::OK) {
    LOG(WARNING) << "Failed to read descriptor value with handle "
                 << desc_handle << " from device " << device_address
                 << " with conn ID " << conn_id << ", transaction "
                 << transaction_id;

    transactions_.erase(transaction_id);
    return kInvalidUniqueId;
  }

  VLOG(1) << "Read descriptor value with handle " << desc_handle
          << " from device " << device_address << " with conn ID " << conn_id;

  return transaction_id;
}

void Gatt::OnGattConnected(const std::string& device_address,
                           gatt_client_conn_t conn_id) {
  CHECK(!device_address.empty());

  auto services = remote_services_.find(device_address);
  if (services != remote_services_.end()) {
    LOG(WARNING) << "GATT cache for device " << device_address
                 << " was not cleared, clear it";
    remote_services_.erase(services);
  }

  // Start GATT browsing.
  UniqueId transaction_id = GetNextId();
  ClientOperation operation = {
      .operation_type = GattClientOperationType::SERVICES_ENUM,
      .request_type = GattClientRequestType::NONE,
      .conn_id = conn_id};
  transactions_.emplace(transaction_id, std::move(operation));

  GattClientOperationStatus status = newblue_->GattClientEnumServices(
      conn_id, true, transaction_id,
      base::Bind(&Gatt::OnGattClientEnumServices,
                 weak_ptr_factory_.GetWeakPtr()));
  if (status != GattClientOperationStatus::OK) {
    LOG(ERROR) << "Failed to browse GATT for device " << device_address
               << " with conn ID " << conn_id;
    transactions_.erase(transaction_id);
  }

  VLOG(1) << "Start GATT browsing for device " << device_address
          << ", transaction " << transaction_id;
}

void Gatt::OnGattDisconnected(const std::string& device_address,
                              gatt_client_conn_t conn_id,
                              bool is_disconnected_by_newblue) {
  CHECK(!device_address.empty());

  // TODO(b:137581907): Investigate if the removal of transaction can rely on
  // the callbacks of GATT operations.
  for (std::map<UniqueId, ClientOperation>::iterator transaction =
           transactions_.begin();
       transaction != transactions_.end();) {
    if (transaction->second.conn_id == conn_id) {
      VLOG(2) << "Clear ongoing GATT client transaction " << transaction->first
              << " with device " << device_address;
      transactions_.erase(transaction++);
    } else {
      ++transaction;
    }
  }

  device_interface_handler_->SetGattServicesResolved(device_address,
                                                     false /* resolved */);

  const auto services = remote_services_.find(device_address);
  if (services != remote_services_.end()) {
    VLOG(1) << "Clear the cached GATT services of device " << device_address;
    for (auto& observer : observers_) {
      for (const auto& service : services->second) {
        if (IsHiddenGattService(*service.second))
          continue;

        for (const auto& characteristic : service.second->characteristics()) {
          for (const auto& descriptor : characteristic.second->descriptors())
            observer.OnGattDescriptorRemoved(*descriptor.second);

          observer.OnGattCharacteristicRemoved(*characteristic.second);
        }
        observer.OnGattServiceRemoved(*service.second);
      }
    }
    remote_services_.erase(device_address);
  }
}

void Gatt::TravPrimaryServices(const std::string& device_address,
                               gatt_client_conn_t conn_id) {
  auto services = remote_services_.find(device_address);

  if (services == remote_services_.end()) {
    LOG(WARNING) << "Failed to find remote services associated with device "
                 << device_address;
    return;
  }

  for (const auto& service_entry : services->second) {
    GattService* service = service_entry.second.get();

    if (!service->primary().value())
      continue;

    UniqueId transaction_id = GetNextId();
    ClientOperation operation = {
        .operation_type = GattClientOperationType::PRIMARY_SERVICE_TRAV,
        .request_type = GattClientRequestType::NONE,
        .conn_id = conn_id};
    transactions_.emplace(transaction_id, std::move(operation));

    GattClientOperationStatus status = newblue_->GattClientTravPrimaryService(
        conn_id, service->uuid().value(), transaction_id,
        base::Bind(&Gatt::OnGattClientTravPrimaryService,
                   weak_ptr_factory_.GetWeakPtr()));
    if (status != GattClientOperationStatus::OK) {
      LOG(ERROR) << "Failed to traverse GATT primary service "
                 << service->uuid().value().canonical_value() << " for device "
                 << device_address << " with conn ID " << conn_id;
      transactions_.erase(transaction_id);
    } else {
      VLOG(1) << "Start traversing GATT primary service "
              << service->uuid().value().canonical_value() << " for device "
              << device_address << ", transaction " << transaction_id;
    }
  }
}

void Gatt::OnGattClientEnumServices(bool finished,
                                    gatt_client_conn_t conn_id,
                                    UniqueId transaction_id,
                                    Uuid uuid,
                                    bool primary,
                                    uint16_t first_handle,
                                    uint16_t num_handles,
                                    GattClientOperationStatus status) {
  auto transaction = transactions_.find(transaction_id);
  CHECK(transaction != transactions_.end());
  CHECK(transaction->second.operation_type ==
        GattClientOperationType::SERVICES_ENUM);

  if (status != GattClientOperationStatus::OK) {
    LOG(ERROR) << "Error GATT client operation, dropping it";
    return;
  }

  // This may be invoked after device is removed, so we check whether the device
  // is still valid.
  std::string device_address =
      device_interface_handler_->GetAddressByConnectionId(conn_id);
  if (device_address.empty()) {
    LOG(WARNING) << "Unknown GATT connection " << conn_id
                 << " for service enumeration result";
    return;
  }

  // Close the transaction when the service enumeration finished.
  if (finished) {
    VLOG(1) << "GATT browsing finished for device " << device_address
            << ", transaction " << transaction_id;
    transactions_.erase(transaction_id);

    // Start primary services traversal.
    TravPrimaryServices(device_address, conn_id);
    return;
  }

  VLOG(2) << "GATT Browsing continues on device " << device_address
          << ", transaction " << transaction_id << ", found "
          << uuid.canonical_value();

  if (!base::Contains(remote_services_, device_address)) {
    std::map<uint16_t, std::unique_ptr<GattService>> services;
    remote_services_.emplace(device_address, std::move(services));
  }

  auto services = remote_services_.find(device_address);
  services->second.emplace(first_handle,
                           std::make_unique<GattService>(
                               device_address, first_handle,
                               first_handle + num_handles - 1, primary, uuid));
  for (auto& observer : observers_) {
    if (IsHiddenGattService(*services->second.at(first_handle)))
      continue;

    observer.OnGattServiceAdded(
        *remote_services_.at(device_address).at(first_handle));
  }
}

void Gatt::OnGattClientTravPrimaryService(
    gatt_client_conn_t conn_id,
    UniqueId transaction_id,
    std::unique_ptr<GattService> service) {
  auto transaction = transactions_.find(transaction_id);
  CHECK(transaction != transactions_.end());
  CHECK(transaction->second.operation_type ==
        GattClientOperationType::PRIMARY_SERVICE_TRAV);

  // This may be invoked after device is removed, so we check whether the device
  // is still valid.
  std::string device_address =
      device_interface_handler_->GetAddressByConnectionId(conn_id);
  if (device_address.empty()) {
    LOG(WARNING) << "Unknown GATT connection " << conn_id
                 << " for primary service traversal result";
    transactions_.erase(transaction_id);
    return;
  }

  if (service == nullptr) {
    LOG(ERROR) << "Primary service traversal failed with device "
               << device_address;
    transactions_.erase(transaction_id);
    return;
  }

  auto services = remote_services_.find(device_address);
  if (services == remote_services_.end()) {
    LOG(WARNING) << "No remote services associated with device "
                 << device_address << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  // If there is service change before the traversal finished where the service
  // is no longer there, we drop the result.
  auto srv = services->second.find(service->first_handle());
  if (srv == services->second.end()) {
    LOG(WARNING) << "Unknown primary service "
                 << service->uuid().value().canonical_value()
                 << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  CHECK(srv->second->device_address().value() == device_address);

  VLOG(2) << "Replacing service " << service->uuid().value().canonical_value()
          << " of device " << device_address
          << " with the traversed one, transaction id " << transaction_id;

  service->SetDeviceAddress(device_address);
  srv->second = std::move(service);
  transactions_.erase(transaction_id);

  // Notify observers on service-changed, characteristic-added and
  // descriptor-added events.
  for (auto& observer : observers_) {
    if (IsHiddenGattService(*srv->second))
      continue;

    observer.OnGattServiceChanged(*srv->second);
    for (const auto& characteristic : srv->second->characteristics()) {
      observer.OnGattCharacteristicAdded(*characteristic.second);

      for (const auto& descriptor : characteristic.second->descriptors())
        observer.OnGattDescriptorAdded(*descriptor.second);
    }
  }

  // Notify device interface handler about the completion of traversal.
  if (IsTravPrimaryServicesCompleted(conn_id)) {
    VLOG(1) << "GATT primary services traversal finished for device "
            << device_address;
    device_interface_handler_->SetGattServicesResolved(device_address,
                                                       true /* resolved */);
  }
}

bool Gatt::IsTravPrimaryServicesCompleted(gatt_client_conn_t conn_id) {
  for (const auto& transaction : transactions_) {
    if (transaction.second.conn_id == conn_id &&
        transaction.second.operation_type ==
            GattClientOperationType::PRIMARY_SERVICE_TRAV) {
      return false;
    }
  }
  return true;
}

GattCharacteristic* Gatt::FindGattCharacteristic(
    const std::string& device_address,
    uint16_t service_handle,
    uint16_t char_handle) const {
  if (device_address.empty() || service_handle == kInvalidGattAttributeHandle ||
      char_handle == kInvalidGattAttributeHandle) {
    return nullptr;
  }

  auto services = remote_services_.find(device_address);
  if (services == remote_services_.end())
    return nullptr;

  auto service = services->second.find(service_handle);
  if (service == services->second.end())
    return nullptr;

  auto characteristic = service->second->characteristics().find(char_handle);
  if (characteristic == service->second->characteristics().end())
    return nullptr;

  return characteristic->second.get();
}

GattDescriptor* Gatt::FindGattDescriptor(const std::string& device_address,
                                         uint16_t service_handle,
                                         uint16_t char_handle,
                                         uint16_t desc_handle) const {
  auto characteristic =
      FindGattCharacteristic(device_address, service_handle, char_handle);
  if (characteristic == nullptr)
    return nullptr;

  auto descriptor = characteristic->descriptors().find(desc_handle);
  if (descriptor == characteristic->descriptors().end())
    return nullptr;

  return descriptor->second.get();
}

void Gatt::OnGattClientReadLongValue(
    std::unique_ptr<ClientOperationState> state,
    gatt_client_conn_t conn_id,
    UniqueId transaction_id,
    uint16_t value_handle,
    AttError error,
    const std::vector<uint8_t>& value) {
  auto transaction = transactions_.find(transaction_id);
  CHECK(transaction != transactions_.end());
  CHECK(transaction->second.operation_type ==
        GattClientOperationType::READ_LONG_VALUE);

  std::string device_address =
      device_interface_handler_->GetAddressByConnectionId(conn_id);
  if (device_address.empty()) {
    LOG(WARNING) << "Unknown GATT conn ID " << conn_id
                 << " for read value result, dropping transaction "
                 << transaction_id;
    transactions_.erase(transaction_id);
    return;
  }

  switch (transaction->second.request_type) {
    case GattClientRequestType::READ_CHARACTERISTIC_VALUE:
      OnGattClientReadLongCharacteristicValue(std::move(state), device_address,
                                              transaction_id, value_handle,
                                              error, value);
      return;
    case GattClientRequestType::READ_DESCRIPTOR_VALUE:
      OnGattClientReadLongDescriptorValue(std::move(state), device_address,
                                          transaction_id, value_handle, error,
                                          value);
      return;
    default:
      LOG(WARNING) << "Unexpected GATT client request "
                   << static_cast<uint8_t>(transaction->second.request_type)
                   << ", transaction" << transaction_id;
      transactions_.erase(transaction_id);
      return;
  }
}

void Gatt::OnGattClientReadLongCharacteristicValue(
    std::unique_ptr<ClientOperationState> state,
    const std::string& device_address,
    UniqueId transaction_id,
    uint16_t value_handle,
    AttError error,
    const std::vector<uint8_t>& value) {
  CHECK(state != nullptr);

  auto characteristic = FindGattCharacteristic(
      device_address, state->service_handle, state->char_handle);
  if (characteristic == nullptr) {
    LOG(WARNING) << "Failed to locate characteristic with service handle "
                 << state->service_handle << " characteristic handle "
                 << state->char_handle << " for device " << device_address
                 << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  CHECK(characteristic->value_handle() == value_handle);

  if (value.empty()) {
    LOG(WARNING) << "Error GATT client read characteristic value with handle "
                 << value_handle << " from given device " << device_address
                 << ", error code " << static_cast<uint8_t>(error);
  } else {
    // Concatenate the latest value fragment to the tail.
    state->value.insert(state->value.end(), value.begin(), value.end());

    if (state->offset >= state->value.size()) {
      error = AttError::INVALID_OFFSET;
      LOG(WARNING) << "Error GATT client read characteristic value with handle "
                   << value_handle << " from given device " << device_address
                   << ", error code " << static_cast<uint8_t>(error);

      state->value.clear();
    } else {
      VLOG(1) << "GATT client read characteristic value with handle "
              << value_handle << " finished for device " << device_address
              << ", transaction " << transaction_id;

      state->value.erase(state->value.begin(),
                         state->value.begin() + state->offset);
    }
  }

  // Notify observers on both successful and failed read of characteristic.
  characteristic->SetValue(state->value);
  for (auto& observer : observers_)
    observer.OnGattCharacteristicChanged(*characteristic);
  characteristic->ResetPropertiesUpdated();

  // In the error cases, we still report the values that we read so far.
  auto callback =
      std::move(transactions_.at(transaction_id).read_char_value_callback);
  transactions_.erase(transaction_id);
  callback.Run(transaction_id, device_address, state->service_handle,
               state->char_handle, ConvertToClientOperationError(error),
               state->value);
}

void Gatt::OnGattClientReadLongDescriptorValue(
    std::unique_ptr<ClientOperationState> state,
    const std::string& device_address,
    UniqueId transaction_id,
    uint16_t value_handle,
    AttError error,
    const std::vector<uint8_t>& value) {
  CHECK(state != nullptr);

  auto descriptor = FindGattDescriptor(device_address, state->service_handle,
                                       state->char_handle, state->desc_handle);
  if (descriptor == nullptr) {
    LOG(WARNING) << "Failed to locate descriptor with service handle "
                 << state->service_handle << " characteristic handle "
                 << state->char_handle << " descriptor handle "
                 << state->desc_handle << " for device " << device_address
                 << ", dropping it";
    transactions_.erase(transaction_id);
    return;
  }

  CHECK(state->desc_handle == value_handle);

  if (value.empty()) {
    LOG(WARNING) << "Error GATT client read descriptor value with handle "
                 << value_handle << " from given device " << device_address
                 << ", error code " << static_cast<uint8_t>(error);
  } else {
    // Concatenate the latest value fragment to the tail.
    state->value.insert(state->value.end(), value.begin(), value.end());

    if (state->offset >= state->value.size()) {
      error = AttError::INVALID_OFFSET;
      LOG(WARNING) << "Error GATT client read descriptor value with handle "
                   << value_handle << " from given device " << device_address
                   << ", error code " << static_cast<uint8_t>(error);

      state->value.clear();
    } else {
      VLOG(1) << "GATT client read descriptor value with handle "
              << value_handle << " finished for device " << device_address
              << ", transaction " << transaction_id;

      state->value.erase(state->value.begin(),
                         state->value.begin() + state->offset);
    }
  }

  // Notify observers on both successful and failed read of descriptor.
  descriptor->SetValue(state->value);
  for (auto& observer : observers_)
    observer.OnGattDescriptorChanged(*descriptor);
  descriptor->ResetPropertiesUpdated();

  // In the error cases, we still report the values that we read so far.
  auto callback =
      std::move(transactions_.at(transaction_id).read_desc_value_callback);
  transactions_.erase(transaction_id);
  callback.Run(transaction_id, device_address, state->service_handle,
               state->char_handle, state->desc_handle,
               ConvertToClientOperationError(error), state->value);
}

}  // namespace bluetooth
