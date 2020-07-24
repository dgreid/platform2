// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service.h"

#include <string>

#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/cellular.h"
#include "shill/control_interface.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"

using std::set;
using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(const CellularService* c) {
  return c->log_name();
}
}  // namespace Logging

// statics
const char CellularService::kAutoConnActivating[] = "activating";
const char CellularService::kAutoConnBadPPPCredentials[] =
    "bad PPP credentials";
const char CellularService::kAutoConnDeviceDisabled[] = "device disabled";
const char CellularService::kAutoConnOutOfCredits[] = "service out of credits";
const char CellularService::kStorageIccid[] = "Cellular.Iccid";
const char CellularService::kStorageImsi[] = "Cellular.Imsi";
const char CellularService::kStoragePPPUsername[] = "Cellular.PPP.Username";
const char CellularService::kStoragePPPPassword[] = "Cellular.PPP.Password";
const char CellularService::kStorageSimCardId[] = "Cellular.SimCardId";

namespace {

const char kStorageAPN[] = "Cellular.APN";
const char kStorageLastGoodAPN[] = "Cellular.LastGoodAPN";

bool GetNonEmptyField(const Stringmap& stringmap,
                      const string& fieldname,
                      string* value) {
  Stringmap::const_iterator it = stringmap.find(fieldname);
  if (it != stringmap.end() && !it->second.empty()) {
    *value = it->second;
    return true;
  }
  return false;
}

}  // namespace

CellularService::CellularService(Manager* manager,
                                 const std::string& imsi,
                                 const std::string& iccid,
                                 const std::string& sim_card_id)
    : Service(manager, Technology::kCellular),
      imsi_(imsi),
      iccid_(iccid),
      sim_card_id_(sim_card_id),
      activation_type_(kActivationTypeUnknown),
      is_auto_connecting_(false),
      out_of_credits_(false) {
  // Note: This will change once SetNetworkTechnology() is called, but the
  // serial number remains unchanged so correlating log lines will be easy.
  set_log_name("cellular_" + base::NumberToString(serial_number()));

  PropertyStore* store = mutable_store();
  HelpRegisterDerivedString(kActivationTypeProperty,
                            &CellularService::CalculateActivationType, nullptr);
  store->RegisterConstString(kActivationStateProperty, &activation_state_);
  HelpRegisterDerivedStringmap(kCellularApnProperty, &CellularService::GetApn,
                               &CellularService::SetApn);
  store->RegisterConstString(kIccidProperty, &iccid_);
  store->RegisterConstString(kImsiProperty, &imsi_);
  store->RegisterConstStringmap(kCellularLastGoodApnProperty,
                                &last_good_apn_info_);
  store->RegisterConstString(kNetworkTechnologyProperty, &network_technology_);
  HelpRegisterDerivedBool(kOutOfCreditsProperty,
                          &CellularService::IsOutOfCredits, nullptr);
  store->RegisterConstStringmap(kPaymentPortalProperty, &olp_);
  store->RegisterConstString(kRoamingStateProperty, &roaming_state_);
  store->RegisterConstStringmap(kServingOperatorProperty, &serving_operator_);
  store->RegisterConstString(kUsageURLProperty, &usage_url_);
  store->RegisterString(kCellularPPPUsernameProperty, &ppp_username_);
  store->RegisterWriteOnlyString(kCellularPPPPasswordProperty, &ppp_password_);

  storage_identifier_ = GetDefaultStorageIdentifier();
}

CellularService::~CellularService() {
  SLOG(this, 2) << "CellularService Destroyed: " << log_name();
}

void CellularService::SetDevice(Cellular* device) {
  SLOG(this, 2) << __func__ << ": " << (device ? device->iccid() : "None");
  cellular_ = device;
  Error ignored_error;
  adaptor()->EmitRpcIdentifierChanged(kDeviceProperty,
                                      GetDeviceRpcId(&ignored_error));
  adaptor()->EmitBoolChanged(kVisibleProperty,
                             GetVisibleProperty(&ignored_error));
  if (!cellular_)
    return;
  SetConnectable(!!device);
  set_friendly_name(cellular_->CreateDefaultFriendlyServiceName());
  SetActivationType(kActivationTypeUnknown);

  // Update the ICCID and Sim Card ID to match |device|. This could potentially
  // happen if a SIM was reprogrammed with an IMSI from another SIM Card, e.g.
  // to replace a lost card.
  iccid_ = cellular_->iccid();
  sim_card_id_ = cellular_->GetSimCardId();
}

void CellularService::AutoConnect() {
  is_auto_connecting_ = true;
  Service::AutoConnect();
  is_auto_connecting_ = false;
}

void CellularService::CompleteCellularActivation(Error* error) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf("CompleteCellularActivation attempted but %s "
                           "Service %s has no device.",
                           kTypeCellular, log_name().c_str()));
    return;
  }
  cellular_->CompleteActivation(error);
}

string CellularService::GetStorageIdentifier() const {
  return storage_identifier_;
}

string CellularService::GetLoadableStorageIdentifier(
    const StoreInterface& storage) const {
  set<string> groups = storage.GetGroupsWithProperties(GetStorageProperties());
  if (groups.empty()) {
    LOG(WARNING) << "Configuration for service " << log_name()
                 << " is not available in the persistent store";
    return std::string();
  }
  if (groups.size() > 1) {
    // This could happen when different properties were used to identify a
    // Cellular service. Look for an entry with a matching identifier,
    // otherwise use the first matchng entry.
    auto iter = std::find(groups.begin(), groups.end(), storage_identifier_);
    if (iter != groups.end())
      return *iter;
    LOG(WARNING) << "More than one configuration for service " << log_name()
                 << " is available; choosing the first.";
  }
  return *groups.begin();
}

bool CellularService::IsLoadableFrom(const StoreInterface& storage) const {
  return !GetLoadableStorageIdentifier(storage).empty();
}

bool CellularService::Load(const StoreInterface* storage) {
  string id = GetLoadableStorageIdentifier(*storage);
  if (id.empty()) {
    LOG(WARNING) << "No service with matching properties found";
    return false;
  }

  SLOG(this, 2) << __func__
                << ": Service with matching properties found: " << id;

  std::string default_storage_identifier = storage_identifier_;

  // Set |storage identifier_| to match the storage name in the Profile.
  // This needs to be done before calling Service::Load().
  // NOTE: Older profiles used other identifiers instead of IMSI. This is fine
  // since entries are identified by their properties, not the id.
  storage_identifier_ = id;

  // Load properties common to all Services.
  if (!Service::Load(storage)) {
    // Restore the default storage id. The invalid profile entry will become
    // ignored.
    storage_identifier_ = default_storage_identifier;
    return false;
  }

  LoadApn(storage, id, kStorageAPN, &apn_info_);
  LoadApn(storage, id, kStorageLastGoodAPN, &last_good_apn_info_);

  const string old_username = ppp_username_;
  const string old_password = ppp_password_;
  storage->GetString(id, kStoragePPPUsername, &ppp_username_);
  storage->GetString(id, kStoragePPPPassword, &ppp_password_);
  if (IsFailed() && failure() == kFailurePPPAuth &&
      (old_username != ppp_username_ || old_password != ppp_password_)) {
    SetState(kStateIdle);
  }
  return true;
}

bool CellularService::Save(StoreInterface* storage) {
  // Save properties common to all Services.
  if (!Service::Save(storage))
    return false;

  const string id = GetStorageIdentifier();
  SaveApn(storage, id, GetUserSpecifiedApn(), kStorageAPN);
  SaveApn(storage, id, GetLastGoodApn(), kStorageLastGoodAPN);
  SaveStringOrClear(storage, id, kStorageIccid, iccid_);
  SaveStringOrClear(storage, id, kStorageImsi, imsi_);
  SaveStringOrClear(storage, id, kStoragePPPUsername, ppp_username_);
  SaveStringOrClear(storage, id, kStoragePPPPassword, ppp_password_);
  SaveStringOrClear(storage, id, kStorageSimCardId, sim_card_id_);
  // Delete deprecated keys. TODO: Remove after M84.
  storage->DeleteKey(id, "Cellular.Imei");
  storage->DeleteKey(id, "Cellular.Meid");
  return true;
}

bool CellularService::IsVisible() const {
  return !!cellular_;
}

void CellularService::SetActivationType(ActivationType type) {
  if (type == activation_type_) {
    return;
  }
  activation_type_ = type;
  adaptor()->EmitStringChanged(kActivationTypeProperty,
                               GetActivationTypeString());
}

string CellularService::GetActivationTypeString() const {
  switch (activation_type_) {
    case kActivationTypeNonCellular:
      return shill::kActivationTypeNonCellular;
    case kActivationTypeOMADM:
      return shill::kActivationTypeOMADM;
    case kActivationTypeOTA:
      return shill::kActivationTypeOTA;
    case kActivationTypeOTASP:
      return shill::kActivationTypeOTASP;
    case kActivationTypeUnknown:
      return "";
    default:
      NOTREACHED();
      return "";  // Make compiler happy.
  }
}

void CellularService::SetActivationState(const string& state) {
  if (state == activation_state_) {
    return;
  }
  activation_state_ = state;
  adaptor()->EmitStringChanged(kActivationStateProperty, state);
}

void CellularService::SetOLP(const string& url,
                             const string& method,
                             const string& post_data) {
  Stringmap olp;
  olp[kPaymentPortalURL] = url;
  olp[kPaymentPortalMethod] = method;
  olp[kPaymentPortalPostData] = post_data;

  if (olp_ == olp) {
    return;
  }
  olp_ = olp;
  adaptor()->EmitStringmapChanged(kPaymentPortalProperty, olp);
}

void CellularService::SetUsageURL(const string& url) {
  if (url == usage_url_) {
    return;
  }
  usage_url_ = url;
  adaptor()->EmitStringChanged(kUsageURLProperty, url);
}

void CellularService::SetServingOperator(const Stringmap& serving_operator) {
  if (serving_operator_ == serving_operator)
    return;

  serving_operator_ = serving_operator;
  adaptor()->EmitStringmapChanged(kServingOperatorProperty, serving_operator_);
}

void CellularService::SetNetworkTechnology(const string& technology) {
  if (technology == network_technology_) {
    return;
  }
  network_technology_ = technology;
  set_log_name("cellular_" + network_technology_ + "_" +
               base::NumberToString(serial_number()));
  adaptor()->EmitStringChanged(kNetworkTechnologyProperty, technology);
}

void CellularService::SetRoamingState(const string& state) {
  if (state == roaming_state_) {
    return;
  }
  roaming_state_ = state;
  adaptor()->EmitStringChanged(kRoamingStateProperty, state);
}

Stringmap* CellularService::GetUserSpecifiedApn() {
  Stringmap::iterator it = apn_info_.find(kApnProperty);
  if (it == apn_info_.end() || it->second.empty())
    return nullptr;
  return &apn_info_;
}

Stringmap* CellularService::GetLastGoodApn() {
  Stringmap::iterator it = last_good_apn_info_.find(kApnProperty);
  if (it == last_good_apn_info_.end() || it->second.empty())
    return nullptr;
  return &last_good_apn_info_;
}

void CellularService::SetLastGoodApn(const Stringmap& apn_info) {
  last_good_apn_info_ = apn_info;
  adaptor()->EmitStringmapChanged(kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
}

void CellularService::ClearLastGoodApn() {
  last_good_apn_info_.clear();
  adaptor()->EmitStringmapChanged(kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
}

void CellularService::NotifySubscriptionStateChanged(
    SubscriptionState subscription_state) {
  bool new_out_of_credits =
      (subscription_state == SubscriptionState::kOutOfCredits);
  if (out_of_credits_ == new_out_of_credits)
    return;

  out_of_credits_ = new_out_of_credits;
  SLOG(this, 2) << (out_of_credits_ ? "Marking service out-of-credits"
                                    : "Marking service as not out-of-credits");
  adaptor()->EmitBoolChanged(kOutOfCreditsProperty, out_of_credits_);
}

void CellularService::OnConnect(Error* error) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf("Connect attempted but %s Service %s has no device.",
                           kTypeCellular, log_name().c_str()));
    return;
  }
  cellular_->Connect(error);
}

void CellularService::OnDisconnect(Error* error, const char* reason) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Disconnect attempted but %s Service %s has no device.",
            kTypeCellular, log_name().c_str()));
    return;
  }
  cellular_->Disconnect(error, reason);
}

bool CellularService::IsAutoConnectable(const char** reason) const {
  if (!cellular_ || !cellular_->running()) {
    *reason = kAutoConnDeviceDisabled;
    return false;
  }
  if (cellular_->IsActivating()) {
    *reason = kAutoConnActivating;
    return false;
  }
  if (failure() == kFailurePPPAuth) {
    *reason = kAutoConnBadPPPCredentials;
    return false;
  }
  if (out_of_credits_) {
    *reason = kAutoConnOutOfCredits;
    return false;
  }
  return Service::IsAutoConnectable(reason);
}

uint64_t CellularService::GetMaxAutoConnectCooldownTimeMilliseconds() const {
  return 30 * 60 * 1000;  // 30 minutes
}

bool CellularService::IsMeteredByServiceProperties() const {
  // TODO(crbug.com/989639): see if we can detect unmetered cellular
  // connections automatically.
  return true;
}

RpcIdentifier CellularService::GetDeviceRpcId(Error* error) const {
  if (!cellular_)
    return control_interface()->NullRpcIdentifier();
  return cellular_->GetRpcIdentifier();
}

void CellularService::HelpRegisterDerivedString(
    const string& name,
    string (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const string& value, Error* error)) {
  mutable_store()->RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<CellularService, string>(this, get, set)));
}

void CellularService::HelpRegisterDerivedStringmap(
    const string& name,
    Stringmap (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const Stringmap& value, Error* error)) {
  mutable_store()->RegisterDerivedStringmap(
      name, StringmapAccessor(new CustomAccessor<CellularService, Stringmap>(
                this, get, set)));
}

void CellularService::HelpRegisterDerivedBool(
    const string& name,
    bool (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const bool&, Error*)) {
  mutable_store()->RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<CellularService, bool>(this, get, set)));
}

set<string> CellularService::GetStorageGroupsWithProperty(
    const StoreInterface& storage,
    const std::string& key,
    const std::string& value) const {
  KeyValueStore properties;
  properties.Set<string>(kStorageType, kTypeCellular);
  properties.Set<string>(key, value);
  return storage.GetGroupsWithProperties(properties);
}

string CellularService::CalculateActivationType(Error* error) {
  return GetActivationTypeString();
}

Stringmap CellularService::GetApn(Error* /*error*/) {
  return apn_info_;
}

bool CellularService::SetApn(const Stringmap& value, Error* error) {
  // Only copy in the fields we care about, and validate the contents.
  // If the "apn" field is missing or empty, the APN is cleared.
  string str;
  Stringmap new_apn_info;
  if (GetNonEmptyField(value, kApnProperty, &str)) {
    new_apn_info[kApnProperty] = str;
    if (GetNonEmptyField(value, kApnUsernameProperty, &str))
      new_apn_info[kApnUsernameProperty] = str;
    if (GetNonEmptyField(value, kApnPasswordProperty, &str))
      new_apn_info[kApnPasswordProperty] = str;
    if (GetNonEmptyField(value, kApnAuthenticationProperty, &str))
      new_apn_info[kApnAuthenticationProperty] = str;
  }
  if (apn_info_ == new_apn_info) {
    return false;
  }
  apn_info_ = new_apn_info;
  adaptor()->EmitStringmapChanged(kCellularApnProperty, apn_info_);

  if (IsConnected()) {
    Disconnect(error, __func__);
    if (!error->IsSuccess()) {
      return false;
    }
    Connect(error, __func__);
    return error->IsSuccess();
  }

  return true;
}

void CellularService::LoadApn(const StoreInterface* storage,
                              const string& storage_group,
                              const string& keytag,
                              Stringmap* apn_info) {
  if (!LoadApnField(storage, storage_group, keytag, kApnProperty, apn_info))
    return;
  LoadApnField(storage, storage_group, keytag, kApnUsernameProperty, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnPasswordProperty, apn_info);
}

bool CellularService::LoadApnField(const StoreInterface* storage,
                                   const string& storage_group,
                                   const string& keytag,
                                   const string& apntag,
                                   Stringmap* apn_info) {
  string value;
  if (storage->GetString(storage_group, keytag + "." + apntag, &value) &&
      !value.empty()) {
    (*apn_info)[apntag] = value;
    return true;
  }
  return false;
}

void CellularService::SaveApn(StoreInterface* storage,
                              const string& storage_group,
                              const Stringmap* apn_info,
                              const string& keytag) {
  SaveApnField(storage, storage_group, apn_info, keytag, kApnProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnUsernameProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnPasswordProperty);
}

void CellularService::SaveApnField(StoreInterface* storage,
                                   const string& storage_group,
                                   const Stringmap* apn_info,
                                   const string& keytag,
                                   const string& apntag) {
  const string key = keytag + "." + apntag;
  string str;
  if (apn_info && GetNonEmptyField(*apn_info, apntag, &str))
    storage->SetString(storage_group, key, str);
  else
    storage->DeleteKey(storage_group, key);
}

KeyValueStore CellularService::GetStorageProperties() const {
  KeyValueStore properties;
  properties.Set<string>(kStorageType, kTypeCellular);
  properties.Set<string>(kStorageImsi, imsi_);
  return properties;
}

std::string CellularService::GetDefaultStorageIdentifier() const {
  if (imsi_.empty()) {
    LOG(ERROR) << "CellularService created with empty IMSI";
    return std::string();
  }
  return SanitizeStorageIdentifier(
      base::StringPrintf("%s_%s", kTypeCellular, imsi_.c_str()));
}

bool CellularService::IsOutOfCredits(Error* /*error*/) {
  return out_of_credits_;
}

}  // namespace shill
