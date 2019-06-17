// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service.h"

#include <string>

#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/cellular.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"

using std::set;
using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(CellularService* c) {
  return c->GetRpcIdentifier().value();
}
}  // namespace Logging

// statics
const char CellularService::kAutoConnActivating[] = "activating";
const char CellularService::kAutoConnBadPPPCredentials[] =
    "bad PPP credentials";
const char CellularService::kAutoConnDeviceDisabled[] = "device disabled";
const char CellularService::kAutoConnOutOfCredits[] = "service out of credits";
const char CellularService::kStorageIccid[] = "Cellular.Iccid";
const char CellularService::kStorageImei[] = "Cellular.Imei";
const char CellularService::kStorageImsi[] = "Cellular.Imsi";
const char CellularService::kStorageMeid[] = "Cellular.Meid";
const char CellularService::kStoragePPPUsername[] = "Cellular.PPP.Username";
const char CellularService::kStoragePPPPassword[] = "Cellular.PPP.Password";

// TODO(petkov): Add these to system_api/dbus/service_constants.h
namespace {
const char kCellularPPPUsernameProperty[] = "Cellular.PPP.Username";
const char kCellularPPPPasswordProperty[] = "Cellular.PPP.Password";
}  // namespace

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

CellularService::CellularService(Manager* manager, const CellularRefPtr& device)
    : Service(manager, Technology::kCellular),
      activation_type_(kActivationTypeUnknown),
      cellular_(device),
      is_auto_connecting_(false),
      out_of_credits_(false) {
  SetConnectable(true);
  PropertyStore* store = this->mutable_store();
  HelpRegisterDerivedString(kActivationTypeProperty,
                            &CellularService::CalculateActivationType, nullptr);
  store->RegisterConstString(kActivationStateProperty, &activation_state_);
  HelpRegisterDerivedStringmap(kCellularApnProperty, &CellularService::GetApn,
                               &CellularService::SetApn);
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

  set_friendly_name(cellular_->CreateDefaultFriendlyServiceName());

  string service_id;
  if (!device->home_provider_info()->uuid().empty()) {
    service_id = device->home_provider_info()->uuid();
  } else if (!device->serving_operator_info()->uuid().empty()) {
    service_id = device->serving_operator_info()->uuid();
  } else if (!device->sim_identifier().empty()) {
    service_id = device->sim_identifier();
  } else if (!device->meid().empty()) {
    service_id = device->meid();
  } else {
    service_id = friendly_name();
  }
  storage_identifier_ = SanitizeStorageIdentifier(base::StringPrintf(
      "%s_%s_%s", kTypeCellular, device->GetEquipmentIdentifier().c_str(),
      service_id.c_str()));
}

CellularService::~CellularService() {}

bool CellularService::IsAutoConnectable(const char** reason) const {
  if (!cellular_->running()) {
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

bool CellularService::Load(StoreInterface* storage) {
  // The initial storage identifier contains the MAC address of the cellular
  // device. However, the MAC address of a cellular device may not be constant
  // (e.g. the kernel driver may pick a random MAC address for a modem when the
  // driver can't obtain that information from the modem). As a remedy, we
  // first try to locate a profile with other service related properties (IMSI,
  // MEID, etc).
  string id = GetLoadableStorageIdentifier(*storage);
  if (id.empty()) {
    LOG(WARNING) << "No service with matching properties found";
    return false;
  }

  SLOG(this, 2) << __func__
                << ": Service with matching properties found: " << id;
  // Set our storage identifier to match the storage name in the Profile.
  storage_identifier_ = id;

  // Load properties common to all Services.
  if (!Service::Load(storage))
    return false;

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

void CellularService::LoadApn(StoreInterface* storage,
                              const string& storage_group,
                              const string& keytag,
                              Stringmap* apn_info) {
  if (!LoadApnField(storage, storage_group, keytag, kApnProperty, apn_info))
    return;
  LoadApnField(storage, storage_group, keytag, kApnUsernameProperty, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnPasswordProperty, apn_info);
}

bool CellularService::LoadApnField(StoreInterface* storage,
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

bool CellularService::Save(StoreInterface* storage) {
  // Save properties common to all Services.
  if (!Service::Save(storage))
    return false;

  const string id = GetStorageIdentifier();
  SaveApn(storage, id, GetUserSpecifiedApn(), kStorageAPN);
  SaveApn(storage, id, GetLastGoodApn(), kStorageLastGoodAPN);
  SaveString(storage, id, kStorageIccid, cellular_->sim_identifier(), false,
             true);
  SaveString(storage, id, kStorageImei, cellular_->imei(), false, true);
  SaveString(storage, id, kStorageImsi, cellular_->imsi(), false, true);
  SaveString(storage, id, kStorageMeid, cellular_->meid(), false, true);
  SaveString(storage, id, kStoragePPPUsername, ppp_username_, false, true);
  SaveString(storage, id, kStoragePPPPassword, ppp_password_, false, true);
  return true;
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

bool CellularService::IsOutOfCredits(Error* /*error*/) {
  return out_of_credits_;
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

void CellularService::AutoConnect() {
  is_auto_connecting_ = true;
  Service::AutoConnect();
  is_auto_connecting_ = false;
}

void CellularService::OnConnect(Error* error) {
  cellular_->Connect(error);
}

void CellularService::OnDisconnect(Error* error, const char* reason) {
  cellular_->Disconnect(error, reason);
}

void CellularService::CompleteCellularActivation(Error* error) {
  cellular_->CompleteActivation(error);
}

string CellularService::GetStorageIdentifier() const {
  return storage_identifier_;
}

RpcIdentifier CellularService::GetDeviceRpcId(Error* /*error*/) const {
  return cellular_->GetRpcIdentifier();
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

string CellularService::GetLoadableStorageIdentifier(
    const StoreInterface& storage) const {
  // We try the following service related identifiers in order:
  // - IMSI
  // - MEID
  //
  // TODO(benchan): IMSI / MEID is associated with the subscriber but not
  // necessarily with the currently registered network. In case of roaming and
  // MVNO, we may need to consider the home provider or serving operator UUID,
  // which requires further investigations.
  set<string> groups;
  if (!cellular_->imsi().empty()) {
    groups =
        GetStorageGroupsWithProperty(storage, kStorageImsi, cellular_->imsi());
  }

  if (groups.empty() && !cellular_->meid().empty()) {
    groups =
        GetStorageGroupsWithProperty(storage, kStorageMeid, cellular_->meid());
  }

  if (groups.empty()) {
    LOG(WARNING) << "Configuration for service " << unique_name()
                 << " is not available in the persistent store";
    return std::string();
  }
  if (groups.size() > 1) {
    LOG(WARNING) << "More than one configuration for service " << unique_name()
                 << " is available; choosing the first.";
  }
  return *groups.begin();
}

bool CellularService::IsLoadableFrom(const StoreInterface& storage) const {
  return !GetLoadableStorageIdentifier(storage).empty();
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

void CellularService::SetNetworkTechnology(const string& technology) {
  if (technology == network_technology_) {
    return;
  }
  network_technology_ = technology;
  adaptor()->EmitStringChanged(kNetworkTechnologyProperty, technology);
}

void CellularService::SetRoamingState(const string& state) {
  if (state == roaming_state_) {
    return;
  }
  roaming_state_ = state;
  adaptor()->EmitStringChanged(kRoamingStateProperty, state);
}

void CellularService::set_serving_operator(const Stringmap& serving_operator) {
  if (serving_operator_ == serving_operator)
    return;

  serving_operator_ = serving_operator;
  adaptor()->EmitStringmapChanged(kServingOperatorProperty, serving_operator_);
}

}  // namespace shill
