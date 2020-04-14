// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/newblue.h"

#include <newblue/sg.h>
#include <newblue/sm.h>
#include <newblue/uhid.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>

#include "bluetooth/newblued/util.h"

namespace bluetooth {

namespace {

// If the BT address is valid?
bool isValidBtAddress(const struct bt_addr& addr) {
  uint64_t addr_val = 0;
  memcpy(&addr_val, addr.addr, sizeof(addr.addr));
  return addr_val != 0;
}

}  // namespace

Newblue::Newblue(std::unique_ptr<LibNewblue> libnewblue)
    : libnewblue_(std::move(libnewblue)), weak_ptr_factory_(this) {}

base::WeakPtr<Newblue> Newblue::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool Newblue::Init() {
  if (base::ThreadTaskRunnerHandle::IsSet())
    origin_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  return true;
}

void Newblue::RegisterPairingAgent(PairingAgent* pairing_agent) {
  pairing_agent_ = pairing_agent;
}

void Newblue::UnregisterPairingAgent() {
  pairing_agent_ = nullptr;
}

bool Newblue::ListenReadyForUp(base::Closure callback) {
  // Dummy MAC address. NewBlue doesn't actually use the MAC address as it's
  // exclusively controlled by BlueZ.
  static const uint8_t kZeroMac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  if (!libnewblue_->HciUp(kZeroMac, &Newblue::OnStackReadyForUpThunk, this))
    return false;

  ready_for_up_callback_ = callback;
  return true;
}

bool Newblue::BringUp() {
  // The public LTKs that we should block.
  static const struct smKey blockedLtks[] = {
      {0xbf, 0x01, 0xfb, 0x9d, 0x4e, 0xf3, 0xbc, 0x36, 0xd8, 0x74, 0xf5, 0x39,
       0x41, 0x38, 0x68, 0x4c}};

  if (!libnewblue_->HciIsUp()) {
    LOG(ERROR) << "HCI is not ready for up";
    return false;
  }

  if (libnewblue_->L2cInit()) {
    LOG(ERROR) << "Failed to initialize L2CAP";
    return false;
  }

  if (!libnewblue_->AttInit()) {
    LOG(ERROR) << "Failed to initialize ATT";
    return false;
  }

  if (!libnewblue_->GattProfileInit()) {
    LOG(ERROR) << "Failed to initialize GATT";
    return false;
  }

  if (!libnewblue_->GattBuiltinInit()) {
    LOG(ERROR) << "Failed to initialize GATT services";
    return false;
  }

  if (!libnewblue_->SmInit()) {
    LOG(ERROR) << "Failed to init SM";
    return false;
  }

  if (!libnewblue_->SmSetBlockedLtks(blockedLtks, 1 /* count */)) {
    LOG(ERROR) << "Failed to set blocked LTKs";
    return false;
  }

  // Always register passkey display observer, assuming that our UI always
  // supports this.
  // TODO(sonnysasaka): We may optimize this by registering passkey display
  // observer only if there is currently a default agent registered.
  passkey_display_observer_id_ = libnewblue_->SmRegisterPasskeyDisplayObserver(
      this, &Newblue::PasskeyDisplayObserverCallbackThunk);

  pair_state_handle_ = libnewblue_->SmRegisterPairStateObserver(
      this, &Newblue::PairStateCallbackThunk);
  if (!pair_state_handle_) {
    LOG(ERROR) << "Failed to register as an observer of pairing state";
    return false;
  }

  libnewblue_->BtleHidInit(hidConnStateCbk, hidReportRxCbk);

  return true;
}

bool Newblue::StartDiscovery(bool active,
                             uint16_t scan_interval,
                             uint16_t scan_window,
                             bool use_random_addr,
                             bool only_whitelist,
                             bool filter_duplicates,
                             DeviceDiscoveredCallback callback) {
  if (discovery_handle_ != 0) {
    LOG(WARNING) << "Discovery is already started, ignoring request";
    return false;
  }

  discovery_handle_ = libnewblue_->HciDiscoverLeStart(
      &Newblue::DiscoveryCallbackThunk, this, active, scan_interval,
      scan_window, use_random_addr, only_whitelist, filter_duplicates);
  if (discovery_handle_ == 0) {
    LOG(ERROR) << "Failed to start LE discovery";
    return false;
  }

  device_discovered_callback_ = callback;
  return true;
}

bool Newblue::StopDiscovery() {
  if (discovery_handle_ == 0) {
    LOG(WARNING) << "Discovery is not started, ignoring request";
    return false;
  }

  bool ret = libnewblue_->HciDiscoverLeStop(discovery_handle_);
  if (!ret) {
    LOG(ERROR) << "Failed to stop LE discovery";
    return false;
  }

  device_discovered_callback_.Reset();
  discovery_handle_ = 0;
  return true;
}

UniqueId Newblue::RegisterAsPairObserver(PairStateChangedCallback callback) {
  UniqueId observer_id = GetNextId();
  if (observer_id != kInvalidUniqueId)
    pair_observers_.emplace(observer_id, callback);
  return observer_id;
}

void Newblue::UnregisterAsPairObserver(UniqueId observer_id) {
  pair_observers_.erase(observer_id);
}

bool Newblue::Pair(const std::string& device_address,
                   bool is_random_address,
                   smPairSecurityRequirements security_requirement) {
  struct bt_addr address;
  if (!ConvertToBtAddr(is_random_address, device_address, &address))
    return false;

  libnewblue_->SmPair(&address, &security_requirement);
  return true;
}

bool Newblue::CancelPair(const std::string& device_address,
                         bool is_random_address) {
  struct bt_addr address;
  if (!ConvertToBtAddr(is_random_address, device_address, &address))
    return false;

  libnewblue_->SmUnpair(&address);
  return true;
}

bool Newblue::RegisterGattClientConnectCallback(
    GattClientConnectCallback callback) {
  if (!gatt_client_connect_callback_.is_null())
    return false;

  gatt_client_connect_callback_ = callback;
  return true;
}

void Newblue::UnregisterGattClientConnectCallback() {
  gatt_client_connect_callback_.Reset();
}

gatt_client_conn_t Newblue::GattClientConnect(const std::string& device_address,
                                              bool is_random_address) {
  struct bt_addr address;
  CHECK(ConvertToBtAddr(is_random_address, device_address, &address));
  gatt_client_conn_t conn_id = libnewblue_->GattClientConnect(
      this, &address, &Newblue::GattConnectCallbackThunk);
  return conn_id;
}

GattClientOperationStatus Newblue::GattClientDisconnect(
    gatt_client_conn_t conn_id) {
  if (conn_id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Invalid conn id " << conn_id << " to disconnect from";
    return GattClientOperationStatus::ERR;
  }

  return static_cast<GattClientOperationStatus>(
      libnewblue_->GattClientDisconnect(conn_id));
}

std::vector<KnownDevice> Newblue::GetKnownDevices() {
  struct smKnownDevNode* head = libnewblue_->SmGetKnownDevices();
  struct smKnownDevNode* node = head;
  std::vector<KnownDevice> devices;

  while (node) {
    KnownDevice device;
    device.address = ConvertBtAddrToString(node->addr);
    device.address_type = node->addr.type;
    device.is_paired = node->isPaired;
    if (node->name)
      device.name = std::string(node->name);
    device.identity_address = isValidBtAddress(node->identityAddr)
                                  ? ConvertBtAddrToString(node->identityAddr)
                                  : "";
    devices.push_back(device);
    node = node->next;
  }

  libnewblue_->SmKnownDevicesFree(head);
  return devices;
}

GattClientOperationStatus Newblue::GattClientEnumServices(
    gatt_client_conn_t conn_id,
    bool primary,
    UniqueId transaction_id,
    GattClientServicesEnumCallback callback) {
  if (conn_id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Invalid GATT conn ID " << conn_id
                 << " provided, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  if (callback.is_null()) {
    LOG(WARNING) << "Callback not provided, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  if (gatt_client_ops_.find(transaction_id) != gatt_client_ops_.end()) {
    LOG(WARNING) << "Transaction " << transaction_id
                 << " already exists, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  GattClientOperation op = {.type = GattClientOperationType::SERVICES_ENUM,
                            .services_enum_callback = callback};
  gatt_client_ops_.emplace(transaction_id, std::move(op));

  return static_cast<GattClientOperationStatus>(
      libnewblue_->GattClientEnumServices(
          this, conn_id, primary, static_cast<uniq_t>(transaction_id),
          &Newblue::GattClientEnumServicesCallbackThunk));
}

GattClientOperationStatus Newblue::GattClientTravPrimaryService(
    gatt_client_conn_t conn_id,
    const Uuid& uuid,
    UniqueId transaction_id,
    GattClientPrimaryServiceTravCallback callback) {
  if (conn_id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Invalid GATT conn ID " << conn_id
                 << " provided, ignoring rquest";
    return GattClientOperationStatus::ERR;
  }

  if (callback.is_null()) {
    LOG(WARNING) << "Callback not provided, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  if (gatt_client_ops_.find(transaction_id) != gatt_client_ops_.end()) {
    LOG(WARNING) << "Transaction " << transaction_id
                 << " already exists, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  GattClientOperation op = {
      .type = GattClientOperationType::PRIMARY_SERVICE_TRAV,
      .service_trav_callback = callback};
  gatt_client_ops_.emplace(transaction_id, std::move(op));

  struct uuid u = ConvertToRawUuid(uuid);

  return static_cast<GattClientOperationStatus>(
      libnewblue_->GattClientUtilFindAndTraversePrimaryService(
          this, conn_id, &u, static_cast<uniq_t>(transaction_id),
          &Newblue::GattClientTravPrimaryServiceCallbackThunk));
}

GattClientOperationStatus Newblue::GattClientReadLongValue(
    gatt_client_conn_t conn_id,
    uint16_t value_handle,
    GattClientOperationAuthentication authentication,
    UniqueId transaction_id,
    GattClientReadLongValueCallback callback) {
  if (conn_id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Invalid GATT conn ID " << conn_id
                 << " provided, ignoring rquest";
    return GattClientOperationStatus::ERR;
  }

  if (callback.is_null()) {
    LOG(WARNING) << "Callback not provided, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  if (gatt_client_ops_.find(transaction_id) != gatt_client_ops_.end()) {
    LOG(WARNING) << "Transaction " << transaction_id
                 << " already exists, ignoring request";
    return GattClientOperationStatus::ERR;
  }

  GattClientOperation op = {.type = GattClientOperationType::READ_LONG_VALUE,
                            .read_long_value_callback = std::move(callback)};
  gatt_client_ops_.emplace(transaction_id, std::move(op));

  auto status = static_cast<GattClientOperationStatus>(
      libnewblue_->GattClientUtilLongRead(
          this, conn_id, value_handle, static_cast<uint8_t>(authentication),
          static_cast<uniq_t>(transaction_id),
          &Newblue::GattClientReadLongCallbackThunk));
  if (status != GattClientOperationStatus::OK)
    gatt_client_ops_.erase(transaction_id);

  return status;
}

/*** Private Methods ***/

bool Newblue::PostTask(const base::Location& from_here,
                       const base::Closure& task) {
  CHECK(origin_task_runner_.get());
  return origin_task_runner_->PostTask(from_here, task);
}

void Newblue::OnStackReadyForUpThunk(void* data) {
  Newblue* newblue = static_cast<Newblue*>(data);
  newblue->PostTask(FROM_HERE, base::Bind(&Newblue::OnStackReadyForUp,
                                          newblue->GetWeakPtr()));
}

void Newblue::OnStackReadyForUp() {
  if (ready_for_up_callback_.is_null()) {
    // libnewblue says it's ready for up but I don't have any callback. Most
    // probably another stack (e.g. BlueZ) just re-initialized the adapter.
    LOG(WARNING) << "No callback when stack is ready for up";
    return;
  }

  ready_for_up_callback_.Run();
  // It only makes sense to bring up the stack once and for all. Reset the
  // callback here so we won't bring up the stack twice.
  ready_for_up_callback_.Reset();
}

void Newblue::DiscoveryCallbackThunk(void* data,
                                     const struct bt_addr* adv_address,
                                     const struct bt_addr* resolved_address,
                                     int8_t rssi,
                                     uint8_t reply_type,
                                     const void* eir,
                                     uint8_t eir_len) {
  Newblue* newblue = static_cast<Newblue*>(data);
  std::vector<uint8_t> eir_vector(static_cast<const uint8_t*>(eir),
                                  static_cast<const uint8_t*>(eir) + eir_len);
  std::string address = ConvertBtAddrToString(*adv_address);
  std::string resolved_addr;
  if (resolved_address != nullptr && isValidBtAddress(*resolved_address))
    resolved_addr = ConvertBtAddrToString(*resolved_address);
  newblue->PostTask(
      FROM_HERE, base::Bind(&Newblue::DiscoveryCallback, newblue->GetWeakPtr(),
                            address, adv_address->type, resolved_addr, rssi,
                            reply_type, eir_vector));
}

void Newblue::DiscoveryCallback(const std::string& adv_address,
                                uint8_t address_type,
                                const std::string& resolved_address,
                                int8_t rssi,
                                uint8_t reply_type,
                                const std::vector<uint8_t>& eir) {
  VLOG(2) << __func__;

  if (device_discovered_callback_.is_null()) {
    LOG(WARNING) << "DiscoveryCallback called when not discovering";
    return;
  }

  device_discovered_callback_.Run(adv_address, address_type, resolved_address,
                                  rssi, reply_type, eir);
}

void Newblue::PairStateCallbackThunk(void* data,
                                     const void* pair_state_change,
                                     uniq_t observer_id) {
  CHECK(data && pair_state_change);

  Newblue* newblue = static_cast<Newblue*>(data);
  const smPairStateChange* change =
      static_cast<const smPairStateChange*>(pair_state_change);

  newblue->PostTask(
      FROM_HERE, base::Bind(&Newblue::PairStateCallback, newblue->GetWeakPtr(),
                            *change, observer_id));
}

void Newblue::PairStateCallback(const smPairStateChange& change,
                                uniq_t observer_id) {
  VLOG(1) << __func__;

  CHECK_EQ(observer_id, pair_state_handle_);

  std::string address = ConvertBtAddrToString(change.peerAddr);
  PairState state = static_cast<PairState>(change.pairState);
  PairError error = static_cast<PairError>(change.pairErr);
  std::string identity_address =
      isValidBtAddress(change.peerIdentityAddr)
          ? ConvertBtAddrToString(change.peerIdentityAddr)
          : "";

  // Notify |pair_observers|.
  for (const auto& observer : pair_observers_)
    observer.second.Run(address, state, error, identity_address);
}

void Newblue::GattConnectCallbackThunk(void* data,
                                       gatt_client_conn_t conn_id,
                                       uint8_t status) {
  Newblue* newblue = static_cast<Newblue*>(data);

  CHECK(newblue != nullptr);

  newblue->PostTask(
      FROM_HERE,
      base::Bind(newblue->gatt_client_connect_callback_, conn_id, status));
}

void Newblue::GattClientEnumServicesCallbackThunk(void* user_data,
                                                  gatt_client_conn_t conn_id,
                                                  uniq_t transaction_id,
                                                  const struct uuid* uuid,
                                                  bool primary,
                                                  uint16_t first_handle,
                                                  uint16_t num_handles,
                                                  uint8_t status) {
  Newblue* newblue = static_cast<Newblue*>(user_data);

  CHECK(newblue != nullptr);

  Uuid service_uuid = uuid == nullptr ? Uuid() : ConvertToUuid(*uuid);
  newblue->PostTask(
      FROM_HERE,
      base::Bind(&Newblue::GattClientEnumServicesCallback,
                 newblue->GetWeakPtr(), conn_id, transaction_id, service_uuid,
                 primary, first_handle, num_handles, status));
}

void Newblue::GattClientEnumServicesCallback(gatt_client_conn_t conn_id,
                                             uniq_t transaction_id,
                                             Uuid uuid,
                                             bool primary,
                                             uint16_t first_handle,
                                             uint16_t num_handles,
                                             uint8_t status) {
  UniqueId tran_id = static_cast<UniqueId>(transaction_id);
  auto op = gatt_client_ops_.find(tran_id);

  CHECK(op != gatt_client_ops_.end());
  CHECK(op->second.type == GattClientOperationType::SERVICES_ENUM);

  bool finished = uuid.format() == UuidFormat::UUID_INVALID;

  op->second.services_enum_callback.Run(
      finished, conn_id, tran_id, uuid, primary, first_handle, num_handles,
      static_cast<GattClientOperationStatus>(status));

  if (finished)
    gatt_client_ops_.erase(tran_id);
}

void Newblue::GattClientTravPrimaryServiceCallbackThunk(
    void* user_data,
    gatt_client_conn_t conn_id,
    uniq_t transaction_id,
    const struct GattTraversedService* service) {
  Newblue* newblue = static_cast<Newblue*>(user_data);

  CHECK(newblue != nullptr);

  std::unique_ptr<GattService> s = nullptr;
  if (service)
    s = ConvertToGattService(*service);

  newblue->PostTask(FROM_HERE,
                    base::Bind(&Newblue::GattClientTravPrimaryServiceCallback,
                               newblue->GetWeakPtr(), conn_id, transaction_id,
                               base::Passed(std::move(s))));
}

void Newblue::GattClientTravPrimaryServiceCallback(
    gatt_client_conn_t conn_id,
    uniq_t transaction_id,
    std::unique_ptr<GattService> service) {
  UniqueId tran_id = static_cast<UniqueId>(transaction_id);
  auto op = gatt_client_ops_.find(tran_id);

  CHECK(op != gatt_client_ops_.end());
  CHECK(op->second.type == GattClientOperationType::PRIMARY_SERVICE_TRAV);

  op->second.service_trav_callback.Run(conn_id, tran_id, std::move(service));
  gatt_client_ops_.erase(tran_id);
}

void Newblue::GattClientReadLongCallbackThunk(void* user_data,
                                              gatt_client_conn_t conn_id,
                                              uniq_t transaction_id,
                                              uint16_t handle,
                                              uint8_t error,
                                              sg data) {
  Newblue* newblue = static_cast<Newblue*>(user_data);

  CHECK(newblue != nullptr);

  std::vector<uint8_t> value = GetBytesFromSg(data);
  sgFree(data);

  newblue->PostTask(FROM_HERE,
                    base::Bind(&Newblue::GattClientReadLongCallback,
                               newblue->GetWeakPtr(), conn_id, transaction_id,
                               handle, static_cast<AttError>(error), value));
}

void Newblue::GattClientReadLongCallback(gatt_client_conn_t conn_id,
                                         uniq_t transaction_id,
                                         uint16_t handle,
                                         AttError error,
                                         std::vector<uint8_t> value) {
  UniqueId tran_id = static_cast<UniqueId>(transaction_id);
  auto op = gatt_client_ops_.find(tran_id);

  CHECK(op != gatt_client_ops_.end());
  CHECK(op->second.type == GattClientOperationType::READ_LONG_VALUE);

  op->second.read_long_value_callback.Run(conn_id, tran_id, handle, error,
                                          value);
  gatt_client_ops_.erase(tran_id);
}

void Newblue::PasskeyDisplayObserverCallbackThunk(
    void* data,
    const struct smPasskeyDisplay* passkey_display,
    uniq_t observer_id) {
  if (!passkey_display) {
    LOG(WARNING) << "passkey display is not given";
    return;
  }

  Newblue* newblue = static_cast<Newblue*>(data);
  newblue->PostTask(
      FROM_HERE,
      base::Bind(&Newblue::PasskeyDisplayObserverCallback,
                 newblue->GetWeakPtr(), *passkey_display, observer_id));
}

void Newblue::PasskeyDisplayObserverCallback(
    struct smPasskeyDisplay passkey_display, uniq_t observer_id) {
  if (observer_id != passkey_display_observer_id_) {
    LOG(WARNING) << "passkey display observer id mismatch";
    return;
  }

  if (passkey_display.valid) {
    CHECK(pairing_agent_);
    pairing_agent_->DisplayPasskey(
        ConvertBtAddrToString(passkey_display.peerAddr),
        passkey_display.passkey);
  } else {
    VLOG(1) << "The passkey session expired with the device";
  }
}

}  // namespace bluetooth
