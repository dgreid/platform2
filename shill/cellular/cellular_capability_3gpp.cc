// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_capability_3gpp.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <ModemManager/ModemManager.h>

#include <string>
#include <vector>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/cellular_bearer.h"
#include "shill/cellular/cellular_pco.h"
#include "shill/cellular/cellular_service.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/cellular/pending_activation_store.h"
#include "shill/cellular/verizon_subscription_state.h"
#include "shill/control_interface.h"
#include "shill/dbus_properties_proxy_interface.h"
#include "shill/device_id.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/property_accessor.h"

using base::Bind;
using base::Closure;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(const CellularCapability3gpp* c) {
  return c->cellular()->GetRpcIdentifier().value();
}
}  // namespace Logging

// static
const char CellularCapability3gpp::kConnectApn[] = "apn";
const char CellularCapability3gpp::kConnectUser[] = "user";
const char CellularCapability3gpp::kConnectPassword[] = "password";
const char CellularCapability3gpp::kConnectAllowedAuth[] = "allowed-auth";
const char CellularCapability3gpp::kConnectAllowRoaming[] = "allow-roaming";
const int64_t CellularCapability3gpp::kEnterPinTimeoutMilliseconds = 20000;
const int64_t
    CellularCapability3gpp::kRegistrationDroppedUpdateTimeoutMilliseconds =
        15000;
const RpcIdentifier CellularCapability3gpp::kRootPath = RpcIdentifier("/");
const char CellularCapability3gpp::kStatusProperty[] = "status";
const char CellularCapability3gpp::kOperatorLongProperty[] = "operator-long";
const char CellularCapability3gpp::kOperatorShortProperty[] = "operator-short";
const char CellularCapability3gpp::kOperatorCodeProperty[] = "operator-code";
const char CellularCapability3gpp::kOperatorAccessTechnologyProperty[] =
    "access-technology";
const int CellularCapability3gpp::kSetPowerStateTimeoutMilliseconds = 20000;
const int CellularCapability3gpp::kUnknownLockRetriesLeft = 999;

namespace {

// Plugin strings via ModemManager.
const char kTelitMMPlugin[] = "Telit";

// This identifier is specified in the serviceproviders.prototxt file.
const char kVzwIdentifier[] = "c83d6597-dc91-4d48-a3a7-d86b80123751";
const size_t kVzwMdnLength = 10;

// Keys for the entries of Profiles.
const char kProfileApn[] = "apn";
const char kProfileUsername[] = "username";
const char kProfilePassword[] = "password";
const char kProfileAuthType[] = "auth-type";

string AccessTechnologyToString(uint32_t access_technologies) {
  // Order is important. Return the highest radio access technology.
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_LTE)
    return kNetworkTechnologyLte;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 | MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |
       MM_MODEM_ACCESS_TECHNOLOGY_EVDOB))
    return kNetworkTechnologyEvdo;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_1XRTT)
    return kNetworkTechnology1Xrtt;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS)
    return kNetworkTechnologyHspaPlus;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_HSPA | MM_MODEM_ACCESS_TECHNOLOGY_HSUPA |
       MM_MODEM_ACCESS_TECHNOLOGY_HSDPA))
    return kNetworkTechnologyHspa;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
    return kNetworkTechnologyUmts;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_EDGE)
    return kNetworkTechnologyEdge;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_GPRS)
    return kNetworkTechnologyGprs;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT | MM_MODEM_ACCESS_TECHNOLOGY_GSM))
    return kNetworkTechnologyGsm;
  return "";
}

string AccessTechnologyToTechnologyFamily(uint32_t access_technologies) {
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_LTE | MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS |
       MM_MODEM_ACCESS_TECHNOLOGY_HSPA | MM_MODEM_ACCESS_TECHNOLOGY_HSUPA |
       MM_MODEM_ACCESS_TECHNOLOGY_HSDPA | MM_MODEM_ACCESS_TECHNOLOGY_UMTS |
       MM_MODEM_ACCESS_TECHNOLOGY_EDGE | MM_MODEM_ACCESS_TECHNOLOGY_GPRS |
       MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT | MM_MODEM_ACCESS_TECHNOLOGY_GSM))
    return kTechnologyFamilyGsm;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 | MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |
       MM_MODEM_ACCESS_TECHNOLOGY_EVDOB | MM_MODEM_ACCESS_TECHNOLOGY_1XRTT))
    return kTechnologyFamilyCdma;
  return "";
}

MMBearerAllowedAuth ApnAuthenticationToMMBearerAllowedAuth(
    const std::string& authentication) {
  if (authentication == kApnAuthenticationPap) {
    return MM_BEARER_ALLOWED_AUTH_PAP;
  }
  if (authentication == kApnAuthenticationChap) {
    return MM_BEARER_ALLOWED_AUTH_CHAP;
  }
  return MM_BEARER_ALLOWED_AUTH_UNKNOWN;
}

std::string MMBearerAllowedAuthToApnAuthentication(
    MMBearerAllowedAuth authentication) {
  switch (authentication) {
    case MM_BEARER_ALLOWED_AUTH_PAP:
      return kApnAuthenticationPap;
    case MM_BEARER_ALLOWED_AUTH_CHAP:
      return kApnAuthenticationChap;
    default:
      return "";
  }
}

bool IsRegisteredState(MMModem3gppRegistrationState state) {
  return (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
          state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING);
}

}  // namespace

CellularCapability3gpp::CellularCapability3gpp(Cellular* cellular,
                                               ModemInfo* modem_info)
    : CellularCapability(cellular, modem_info),
      mobile_operator_info_(
          new MobileOperatorInfo(cellular->dispatcher(), "ParseScanResult")),
      registration_state_(MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN),
      current_capabilities_(MM_MODEM_CAPABILITY_NONE),
      access_technologies_(MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN),
      resetting_(false),
      subscription_state_(SubscriptionState::kUnknown),
      reset_done_(false),
      registration_dropped_update_timeout_milliseconds_(
          kRegistrationDroppedUpdateTimeoutMilliseconds),
      weak_ptr_factory_(this) {
  SLOG(this, 2) << "Cellular capability constructed: 3GPP";
  mobile_operator_info_->Init();
  HelpRegisterConstDerivedKeyValueStore(
      kSIMLockStatusProperty, &CellularCapability3gpp::SimLockStatusToProperty);
}

CellularCapability3gpp::~CellularCapability3gpp() = default;

KeyValueStore CellularCapability3gpp::SimLockStatusToProperty(
    Error* /*error*/) {
  KeyValueStore status;
  string lock_type;
  switch (sim_lock_status_.lock_type) {
    case MM_MODEM_LOCK_SIM_PIN:
      lock_type = "sim-pin";
      break;
    case MM_MODEM_LOCK_SIM_PUK:
      lock_type = "sim-puk";
      break;
    default:
      lock_type = "";
      break;
  }
  status.Set<bool>(kSIMLockEnabledProperty, sim_lock_status_.enabled);
  status.Set<string>(kSIMLockTypeProperty, lock_type);
  status.Set<int32_t>(kSIMLockRetriesLeftProperty,
                      sim_lock_status_.retries_left);
  return status;
}

void CellularCapability3gpp::HelpRegisterConstDerivedKeyValueStore(
    const string& name,
    KeyValueStore (CellularCapability3gpp::*get)(Error* error)) {
  cellular()->mutable_store()->RegisterDerivedKeyValueStore(
      name, KeyValueStoreAccessor(
                new CustomAccessor<CellularCapability3gpp, KeyValueStore>(
                    this, get, nullptr)));
}

void CellularCapability3gpp::InitProxies() {
  modem_3gpp_proxy_ = control_interface()->CreateMM1ModemModem3gppProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_proxy_ = control_interface()->CreateMM1ModemProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_simple_proxy_ = control_interface()->CreateMM1ModemSimpleProxy(
      cellular()->dbus_path(), cellular()->dbus_service());

  modem_location_proxy_ = control_interface()->CreateMM1ModemLocationProxy(
      cellular()->dbus_path(), cellular()->dbus_service());

  modem_proxy_->set_state_changed_callback(
      Bind(&CellularCapability3gpp::OnModemStateChangedSignal,
           weak_ptr_factory_.GetWeakPtr()));
  // Do not create a SIM proxy until the device is enabled because we
  // do not yet know the object path of the sim object.
  // TODO(jglasgow): register callbacks
}

void CellularCapability3gpp::StartModem(Error* error,
                                        const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  InitProxies();

  deferred_enable_modem_callback_.Reset();
  EnableModem(true, error, callback);
}

void CellularCapability3gpp::EnableModem(bool deferrable,
                                         Error* error,
                                         const ResultCallback& callback) {
  SLOG(this, 3) << __func__ << "(deferrable=" << deferrable << ")";
  CHECK(!callback.is_null());
  Error local_error(Error::kOperationInitiated);
  modem_info()->metrics()->NotifyDeviceEnableStarted(
      cellular()->interface_index());
  modem_proxy_->Enable(
      true, &local_error,
      Bind(&CellularCapability3gpp::EnableModemCompleted,
           weak_ptr_factory_.GetWeakPtr(), deferrable, callback),
      kTimeoutEnable);
  if (local_error.IsFailure()) {
    SLOG(this, 2) << __func__ << "Call to modem_proxy_->Enable() failed";
  }
  if (error) {
    error->CopyFrom(local_error);
  }
}

void CellularCapability3gpp::EnableModemCompleted(
    bool deferrable, const ResultCallback& callback, const Error& error) {
  SLOG(this, 3) << __func__ << "(deferrable=" << deferrable
                << ", error=" << error << ")";

  // If the enable operation failed with Error::kWrongState, the modem is not
  // in the expected state (i.e. disabled). If |deferrable| indicates that the
  // enable operation can be deferred, we defer the operation until the modem
  // goes into the expected state (see OnModemStateChangedSignal).
  //
  // Note that when the SIM is locked, the enable operation also fails with
  // Error::kWrongState. The enable operation is deferred until the modem goes
  // into the disabled state after the SIM is unlocked. We may choose not to
  // defer the enable operation when the SIM is locked, but the UI needs to
  // trigger the enable operation after the SIM is unlocked, which is currently
  // not the case.
  if (error.IsFailure()) {
    if (!deferrable || error.type() != Error::kWrongState) {
      callback.Run(error);
      return;
    }

    if (deferred_enable_modem_callback_.is_null()) {
      SLOG(this, 2) << "Defer enable operation.";
      // The Enable operation to be deferred should not be further deferrable.
      deferred_enable_modem_callback_ = Bind(
          &CellularCapability3gpp::EnableModem, weak_ptr_factory_.GetWeakPtr(),
          false,  // non-deferrable
          nullptr, callback);
    }

    if (IsLocationUpdateSupported()) {
      ResultCallback setup_callback =
          Bind(&CellularCapability3gpp::OnSetupLocationReply,
               weak_ptr_factory_.GetWeakPtr());
      SetupLocation(MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI, false,
                    setup_callback);
    }

    return;
  }

  // After modem is enabled, it should be possible to get properties
  // TODO(jglasgow): handle errors from GetProperties
  GetProperties();
  // We expect the modem to start scanning after it has been enabled.
  // Change this if this behavior is no longer the case in the future.
  modem_info()->metrics()->NotifyDeviceEnableFinished(
      cellular()->interface_index());
  modem_info()->metrics()->NotifyDeviceScanStarted(
      cellular()->interface_index());
  callback.Run(error);
}

void CellularCapability3gpp::StopModem(Error* error,
                                       const ResultCallback& callback) {
  CHECK(!callback.is_null());
  CHECK(error);
  // If there is an outstanding registration change, simply ignore it since
  // the service will be destroyed anyway.
  if (!registration_dropped_update_callback_.IsCancelled()) {
    registration_dropped_update_callback_.Cancel();
    SLOG(this, 2) << __func__ << " Cancelled delayed deregister.";
  }

  cellular()->dispatcher()->PostTask(
      FROM_HERE, Bind(&CellularCapability3gpp::Stop_Disable,
                      weak_ptr_factory_.GetWeakPtr(), callback));
  deferred_enable_modem_callback_.Reset();
}

void CellularCapability3gpp::Stop_Disable(const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  Error error;
  modem_info()->metrics()->NotifyDeviceDisableStarted(
      cellular()->interface_index());
  modem_proxy_->Enable(false, &error,
                       Bind(&CellularCapability3gpp::Stop_DisableCompleted,
                            weak_ptr_factory_.GetWeakPtr(), callback),
                       kTimeoutEnable);
  if (error.IsFailure())
    callback.Run(error);
}

void CellularCapability3gpp::Stop_DisableCompleted(
    const ResultCallback& callback, const Error& error) {
  SLOG(this, 3) << __func__;

  if (error.IsSuccess()) {
    // The modem has been successfully disabled, but we still need to power it
    // down.
    Stop_PowerDown(callback);
  } else {
    // An error occurred; terminate the disable sequence.
    callback.Run(error);
  }
}

void CellularCapability3gpp::Stop_PowerDown(const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  Error error;
  modem_proxy_->SetPowerState(
      MM_MODEM_POWER_STATE_LOW, &error,
      Bind(&CellularCapability3gpp::Stop_PowerDownCompleted,
           weak_ptr_factory_.GetWeakPtr(), callback),
      kSetPowerStateTimeoutMilliseconds);

  if (error.IsFailure())
    // This really shouldn't happen, but if it does, report success,
    // because a stop initiated power down is only called if the
    // modem was successfully disabled, but the failure of this
    // operation should still be propagated up as a successful disable.
    Stop_PowerDownCompleted(callback, error);
}

// Note: if we were in the middle of powering down the modem when the
// system suspended, we might not get this event from
// ModemManager. And we might not even get a timeout from dbus-c++,
// because StartModem re-initializes proxies.
void CellularCapability3gpp::Stop_PowerDownCompleted(
    const ResultCallback& callback, const Error& error) {
  SLOG(this, 3) << __func__;

  if (error.IsFailure())
    SLOG(this, 2) << "Ignoring error returned by SetPowerState: " << error;

  // Since the disable succeeded, if power down fails, we currently fail
  // silently, i.e. we need to report the disable operation as having
  // succeeded.
  modem_info()->metrics()->NotifyDeviceDisableFinished(
      cellular()->interface_index());
  ReleaseProxies();
  callback.Run(Error());
}

void CellularCapability3gpp::Connect(const KeyValueStore& properties,
                                     Error* error,
                                     const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  RpcIdentifierCallback cb = Bind(&CellularCapability3gpp::OnConnectReply,
                                  weak_ptr_factory_.GetWeakPtr(), callback);
  modem_simple_proxy_->Connect(properties, error, cb, kTimeoutConnect);
}

void CellularCapability3gpp::Disconnect(Error* error,
                                        const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  if (modem_simple_proxy_) {
    SLOG(this, 2) << "Disconnect all bearers.";
    // If "/" is passed as the bearer path, ModemManager will disconnect all
    // bearers.
    modem_simple_proxy_->Disconnect(kRootPath, error, callback,
                                    kTimeoutDisconnect);
  }
}

void CellularCapability3gpp::CompleteActivation(Error* error) {
  SLOG(this, 3) << __func__;

  // Persist the ICCID as "Pending Activation".
  // We're assuming that when this function gets called,
  // |cellular()->iccid()| will be non-empty. We still check here that
  // is non-empty, though something is wrong if it is empty.
  const string& iccid = cellular()->iccid();
  if (iccid.empty()) {
    SLOG(this, 2) << "SIM identifier not available. Nothing to do.";
    return;
  }

  modem_info()->pending_activation_store()->SetActivationState(
      PendingActivationStore::kIdentifierICCID, iccid,
      PendingActivationStore::kStatePending);
  UpdatePendingActivationState();

  SLOG(this, 2) << "Resetting modem for activation.";
  ResetAfterActivation();
}

void CellularCapability3gpp::ResetAfterActivation() {
  SLOG(this, 3) << __func__;

  // Here the initial call to Reset might fail in rare cases. Simply ignore.
  Error error;
  ResultCallback callback =
      Bind(&CellularCapability3gpp::OnResetAfterActivationReply,
           weak_ptr_factory_.GetWeakPtr());
  Reset(&error, callback);
  if (error.IsFailure())
    SLOG(this, 2) << "Failed to reset after activation.";
}

void CellularCapability3gpp::OnResetAfterActivationReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Failed to reset after activation. Try again later.";
    // TODO(armansito): Maybe post a delayed reset task?
    return;
  }
  reset_done_ = true;
  UpdatePendingActivationState();
}

void CellularCapability3gpp::UpdatePendingActivationState() {
  SLOG(this, 3) << __func__;

  const string& iccid = cellular()->iccid();
  bool registered =
      registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_HOME;

  // We know a service is activated if |subscription_state_| is
  // SubscriptionState::kProvisioned / SubscriptionState::kOutOfCredits
  // In the case that |subscription_state_| is SubscriptionState::kUnknown, we
  // fallback on checking for a valid MDN.
  bool activated =
      ((subscription_state_ == SubscriptionState::kProvisioned) ||
       (subscription_state_ == SubscriptionState::kOutOfCredits)) ||
      ((subscription_state_ == SubscriptionState::kUnknown) && IsMdnValid());

  if (activated && !iccid.empty())
    modem_info()->pending_activation_store()->RemoveEntry(
        PendingActivationStore::kIdentifierICCID, iccid);

  CellularServiceRefPtr service = cellular()->service();

  if (!service)
    return;

  if (service->activation_state() == kActivationStateActivated)
    // Either no service or already activated. Nothing to do.
    return;

  // If the ICCID is not available, the following logic can be delayed until it
  // becomes available.
  if (iccid.empty())
    return;

  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid);
  switch (state) {
    case PendingActivationStore::kStatePending:
      // Always mark the service as activating here, as the ICCID could have
      // been unavailable earlier.
      service->SetActivationState(kActivationStateActivating);
      if (reset_done_) {
        SLOG(this, 2) << "Post-payment activation reset complete.";
        modem_info()->pending_activation_store()->SetActivationState(
            PendingActivationStore::kIdentifierICCID, iccid,
            PendingActivationStore::kStateActivated);
      }
      break;
    case PendingActivationStore::kStateActivated:
      if (registered) {
        // Trigger auto connect here.
        SLOG(this, 2) << "Modem has been reset at least once, try to "
                      << "autoconnect to force MDN to update.";
        service->AutoConnect();
      }
      break;
    case PendingActivationStore::kStateUnknown:
      // No entry exists for this ICCID. Nothing to do.
      break;
    default:
      NOTREACHED();
  }
}

string CellularCapability3gpp::GetMdnForOLP(
    const MobileOperatorInfo* operator_info) const {
  // TODO(benchan): This is ugly. Remove carrier specific code once we move
  // mobile activation logic to carrier-specifc extensions (crbug.com/260073).
  const string& mdn = cellular()->mdn();
  if (!operator_info->IsMobileNetworkOperatorKnown()) {
    // Can't make any carrier specific modifications.
    return mdn;
  }

  if (operator_info->uuid() == kVzwIdentifier) {
    // subscription_state_ is the definitive indicator of whether we need
    // activation. The OLP expects an all zero MDN in that case.
    if (subscription_state_ == SubscriptionState::kUnprovisioned ||
        mdn.empty()) {
      return string(kVzwMdnLength, '0');
    }
    if (mdn.length() > kVzwMdnLength) {
      return mdn.substr(mdn.length() - kVzwMdnLength);
    }
  }
  return mdn;
}

void CellularCapability3gpp::ReleaseProxies() {
  SLOG(this, 3) << __func__;
  modem_3gpp_proxy_.reset();
  modem_proxy_.reset();
  modem_location_proxy_.reset();
  modem_simple_proxy_.reset();

  // |sim_proxy_| is managed through OnSimPathChanged() and thus shouldn't be
  // cleared here in order to keep it in sync with |sim_path_|.
}

bool CellularCapability3gpp::AreProxiesInitialized() const {
  return (modem_3gpp_proxy_.get() && modem_proxy_.get() &&
          modem_simple_proxy_.get() && sim_proxy_.get() &&
          modem_location_proxy_.get());
}

void CellularCapability3gpp::UpdateServiceActivationState() {
  CellularServiceRefPtr service = cellular()->service();
  if (!service)
    return;

  service->NotifySubscriptionStateChanged(subscription_state_);

  const string& iccid = cellular()->iccid();
  string activation_state;
  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid);
  if ((subscription_state_ == SubscriptionState::kUnknown ||
       subscription_state_ == SubscriptionState::kUnprovisioned) &&
      !iccid.empty() && state == PendingActivationStore::kStatePending) {
    activation_state = kActivationStateActivating;
  } else if (IsServiceActivationRequired()) {
    activation_state = kActivationStateNotActivated;
  } else {
    activation_state = kActivationStateActivated;
  }
  service->SetActivationState(activation_state);
}

void CellularCapability3gpp::OnServiceCreated() {
  // ModemManager might have issued some property updates before the service
  // object was created to receive the udpates, so we explicitly refresh the
  // properties here.
  GetProperties();

  cellular()->service()->SetActivationType(CellularService::kActivationTypeOTA);
  UpdateServiceActivationState();

  // Make sure that the network technology is set when the service gets
  // created, just in case.
  cellular()->service()->SetNetworkTechnology(GetNetworkTechnologyString());
}

void CellularCapability3gpp::SetupConnectProperties(KeyValueStore* properties) {
  apn_try_list_ = cellular()->BuildApnTryList();
  FillConnectPropertyMap(properties);
}

void CellularCapability3gpp::FillConnectPropertyMap(KeyValueStore* properties) {
  properties->Set<bool>(kConnectAllowRoaming,
                        cellular()->IsRoamingAllowedOrRequired());

  if (apn_try_list_.empty())
    return;

  // Leave the APN at the front of the list, so that it can be recorded
  // if the connect attempt succeeds.
  Stringmap apn_info = apn_try_list_.front();
  SLOG(this, 2) << __func__ << ": Using APN " << apn_info[kApnProperty];
  properties->Set<string>(kConnectApn, apn_info[kApnProperty]);
  if (base::Contains(apn_info, kApnUsernameProperty))
    properties->Set<string>(kConnectUser, apn_info[kApnUsernameProperty]);
  if (base::Contains(apn_info, kApnPasswordProperty))
    properties->Set<string>(kConnectPassword, apn_info[kApnPasswordProperty]);
  if (base::Contains(apn_info, kApnAuthenticationProperty)) {
    MMBearerAllowedAuth allowed_auth = ApnAuthenticationToMMBearerAllowedAuth(
        apn_info[kApnAuthenticationProperty]);
    if (allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
      properties->Set<uint32_t>(kConnectAllowedAuth, allowed_auth);
  }
}

void CellularCapability3gpp::OnConnectReply(const ResultCallback& callback,
                                            const RpcIdentifier& bearer,
                                            const Error& error) {
  SLOG(this, 3) << __func__ << "(" << error << ")";

  CellularServiceRefPtr service = cellular()->service();
  if (!service) {
    // The service could have been deleted before our Connect() request
    // completes if the modem was enabled and then quickly disabled.
    apn_try_list_.clear();
  } else if (error.IsFailure()) {
    service->ClearLastGoodApn();
    // The APN that was just tried (and failed) is still at the
    // front of the list, about to be removed. If the list is empty
    // after that, try one last time without an APN. This may succeed
    // with some modems in some cases.
    if (RetriableConnectError(error) && !apn_try_list_.empty()) {
      apn_try_list_.pop_front();
      SLOG(this, 2) << "Connect failed with invalid APN, "
                    << apn_try_list_.size() << " remaining APNs to try";
      KeyValueStore props;
      FillConnectPropertyMap(&props);
      Error error;
      Connect(props, &error, callback);
      return;
    }
  } else {
    if (!apn_try_list_.empty()) {
      service->SetLastGoodApn(apn_try_list_.front());
      apn_try_list_.clear();
    }
    SLOG(this, 2) << "Connected bearer " << bearer.value();
  }

  if (!callback.is_null())
    callback.Run(error);

  UpdatePendingActivationState();
}

void CellularCapability3gpp::FillInitialEpsBearerPropertyMap(
    KeyValueStore* properties) {
  std::deque<Stringmap> apn_list = cellular()->BuildApnTryList();
  auto apn_info = apn_list.end();

  // keep only 'attach APN'
  for (apn_info = apn_list.begin(); apn_info != apn_list.end(); apn_info++) {
    if (base::Contains(*apn_info, kApnAttachProperty))
      break;
  }

  if (apn_info == apn_list.end()) {
    SLOG(this, 2) << __func__ << ": no Attach APN.";
    return;
  }

  SLOG(this, 2) << __func__ << ": Using APN " << (*apn_info)[kApnProperty];
  properties->Set<string>(kConnectApn, (*apn_info)[kApnProperty]);
  if (base::Contains(*apn_info, kApnUsernameProperty))
    properties->Set<string>(kConnectUser, (*apn_info)[kApnUsernameProperty]);
  if (base::Contains(*apn_info, kApnPasswordProperty)) {
    properties->Set<string>(kConnectPassword,
                            (*apn_info)[kApnPasswordProperty]);
  }
  if (base::Contains(*apn_info, kApnAuthenticationProperty)) {
    MMBearerAllowedAuth allowed_auth = ApnAuthenticationToMMBearerAllowedAuth(
        (*apn_info)[kApnAuthenticationProperty]);
    if (allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
      properties->Set<uint32_t>(kConnectAllowedAuth, allowed_auth);
  }
}

void CellularCapability3gpp::GetProperties() {
  SLOG(this, 3) << __func__;

  std::unique_ptr<DBusPropertiesProxyInterface> properties_proxy =
      control_interface()->CreateDBusPropertiesProxy(
          cellular()->dbus_path(), cellular()->dbus_service());

  KeyValueStore properties(properties_proxy->GetAll(MM_DBUS_INTERFACE_MODEM));
  OnModemPropertiesChanged(properties, vector<string>());

  properties = properties_proxy->GetAll(MM_DBUS_INTERFACE_MODEM_MODEM3GPP);
  OnModem3gppPropertiesChanged(properties, vector<string>());
}

void CellularCapability3gpp::UpdateServiceOLP() {
  SLOG(this, 3) << __func__;

  // OLP is based off of the Home Provider.
  if (!cellular()->home_provider_info()->IsMobileNetworkOperatorKnown()) {
    return;
  }

  const vector<MobileOperatorInfo::OnlinePortal>& olp_list =
      cellular()->home_provider_info()->olp_list();
  if (olp_list.empty()) {
    return;
  }

  if (olp_list.size() > 1) {
    SLOG(this, 1) << "Found multiple online portals. Choosing the first.";
  }
  string post_data = olp_list[0].post_data;
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${iccid}",
                                     cellular()->iccid());
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${imei}",
                                     cellular()->imei());
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${imsi}",
                                     cellular()->imsi());
  base::ReplaceSubstringsAfterOffset(
      &post_data, 0, "${mdn}", GetMdnForOLP(cellular()->home_provider_info()));
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${min}",
                                     cellular()->min());
  cellular()->service()->SetOLP(olp_list[0].url, olp_list[0].method, post_data);
}

void CellularCapability3gpp::UpdateActiveBearer() {
  SLOG(this, 3) << __func__;

  // Look for the first active bearer and use its path as the connected
  // one. Right now, we don't allow more than one active bearer.
  active_bearer_.reset();
  for (const auto& path : bearer_paths_) {
    auto bearer = std::make_unique<CellularBearer>(control_interface(), path,
                                                   cellular()->dbus_service());
    // The bearer object may have vanished before ModemManager updates the
    // 'Bearers' property.
    if (!bearer->Init())
      continue;

    if (!bearer->connected())
      continue;

    SLOG(this, 2) << "Found active bearer \"" << path.value() << "\".";
    CHECK(!active_bearer_) << "Found more than one active bearer.";
    active_bearer_ = std::move(bearer);
  }

  if (!active_bearer_)
    SLOG(this, 2) << "No active bearer found.";
}

bool CellularCapability3gpp::IsServiceActivationRequired() const {
  const string& iccid = cellular()->iccid();
  // subscription_state_ is the definitive answer. If that does not work,
  // fallback on MDN based logic.
  if (subscription_state_ == SubscriptionState::kProvisioned ||
      subscription_state_ == SubscriptionState::kOutOfCredits)
    return false;

  // We are in the process of activating, ignore all other clues from the
  // network and use our own knowledge about the activation state.
  if (!iccid.empty() &&
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid) !=
          PendingActivationStore::kStateUnknown)
    return false;

  // Network notification that the service needs to be activated.
  if (subscription_state_ == SubscriptionState::kUnprovisioned)
    return true;

  // If there is no online payment portal information, it's safer to assume
  // the service does not require activation.
  if (!cellular()->home_provider_info()->IsMobileNetworkOperatorKnown() ||
      cellular()->home_provider_info()->olp_list().empty()) {
    return false;
  }

  // If the MDN is invalid (i.e. empty or contains only zeros), the service
  // requires activation.
  return !IsMdnValid();
}

bool CellularCapability3gpp::IsActivating() const {
  return false;
}

bool CellularCapability3gpp::IsMdnValid() const {
  const string& mdn = cellular()->mdn();
  // Note that |mdn| is normalized to contain only digits in OnMdnChanged().
  for (size_t i = 0; i < mdn.size(); ++i) {
    if (mdn[i] != '0')
      return true;
  }
  return false;
}

// always called from an async context
void CellularCapability3gpp::Register(const ResultCallback& callback) {
  SLOG(this, 3) << __func__ << " \"" << cellular()->selected_network() << "\"";
  CHECK(!callback.is_null());
  Error error;
  ResultCallback cb = Bind(&CellularCapability3gpp::OnRegisterReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Register(cellular()->selected_network(), &error, cb,
                              kTimeoutRegister);
  if (error.IsFailure())
    callback.Run(error);
}

void CellularCapability3gpp::RegisterOnNetwork(const string& network_id,
                                               Error* error,
                                               const ResultCallback& callback) {
  SLOG(this, 3) << __func__ << "(" << network_id << ")";
  CHECK(error);
  desired_network_ = network_id;
  ResultCallback cb = Bind(&CellularCapability3gpp::OnRegisterReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Register(network_id, error, cb, kTimeoutRegister);
}

void CellularCapability3gpp::OnRegisterReply(const ResultCallback& callback,
                                             const Error& error) {
  SLOG(this, 3) << __func__ << "(" << error << ")";

  if (error.IsSuccess()) {
    cellular()->set_selected_network(desired_network_);
    desired_network_.clear();
    callback.Run(error);
    return;
  }
  // If registration on the desired network failed,
  // try to register on the home network.
  if (!desired_network_.empty()) {
    desired_network_.clear();
    cellular()->set_selected_network("");
    LOG(INFO) << "Couldn't register on selected network, trying home network";
    Register(callback);
    return;
  }
  callback.Run(error);
}

bool CellularCapability3gpp::IsRegistered() const {
  return IsRegisteredState(registration_state_);
}

void CellularCapability3gpp::SetUnregistered(bool searching) {
  // If we're already in some non-registered state, don't override that
  if (registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
      registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
    registration_state_ =
        (searching ? MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING
                   : MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
  }
}

void CellularCapability3gpp::RequirePin(const string& pin,
                                        bool require,
                                        Error* error,
                                        const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->EnablePin(pin, require, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::EnterPin(const string& pin,
                                      Error* error,
                                      const ResultCallback& callback) {
  CHECK(error);
  SLOG(this, 3) << __func__;
  sim_proxy_->SendPin(pin, error, callback, kEnterPinTimeoutMilliseconds);
}

void CellularCapability3gpp::UnblockPin(const string& unblock_code,
                                        const string& pin,
                                        Error* error,
                                        const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->SendPuk(unblock_code, pin, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::ChangePin(const string& old_pin,
                                       const string& new_pin,
                                       Error* error,
                                       const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->ChangePin(old_pin, new_pin, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::Reset(Error* error,
                                   const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  CHECK(error);
  if (resetting_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Already resetting");
    return;
  }
  ResultCallback cb = Bind(&CellularCapability3gpp::OnResetReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_proxy_->Reset(error, cb, kTimeoutReset);
  if (!error->IsFailure()) {
    resetting_ = true;
  }
}

void CellularCapability3gpp::OnResetReply(const ResultCallback& callback,
                                          const Error& error) {
  SLOG(this, 3) << __func__;
  resetting_ = false;
  if (!callback.is_null())
    callback.Run(error);
}

void CellularCapability3gpp::Scan(Error* error,
                                  const ResultStringmapsCallback& callback) {
  KeyValueStoresCallback cb = Bind(&CellularCapability3gpp::OnScanReply,
                                   weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Scan(error, cb, kTimeoutScan);
}

void CellularCapability3gpp::OnScanReply(
    const ResultStringmapsCallback& callback,
    const ScanResults& results,
    const Error& error) {
  Stringmaps found_networks;
  for (const auto& result : results)
    found_networks.push_back(ParseScanResult(result));
  callback.Run(found_networks, error);
}

Stringmap CellularCapability3gpp::ParseScanResult(const ScanResult& result) {
  /* ScanResults contain the following keys:

     "status"
     A MMModem3gppNetworkAvailability value representing network
     availability status, given as an unsigned integer (signature "u").
     This key will always be present.

     "operator-long"
     Long-format name of operator, given as a string value (signature
     "s"). If the name is unknown, this field should not be present.

     "operator-short"
     Short-format name of operator, given as a string value
     (signature "s"). If the name is unknown, this field should not
     be present.

     "operator-code"
     Mobile code of the operator, given as a string value (signature
     "s"). Returned in the format "MCCMNC", where MCC is the
     three-digit ITU E.212 Mobile Country Code and MNC is the two- or
     three-digit GSM Mobile Network Code. e.g. "31026" or "310260".

     "access-technology"
     A MMModemAccessTechnology value representing the generic access
     technology used by this mobile network, given as an unsigned
     integer (signature "u").
  */
  Stringmap parsed;

  if (result.Contains<uint32_t>(kStatusProperty)) {
    uint32_t status = result.Get<uint32_t>(kStatusProperty);
    // numerical values are taken from 3GPP TS 27.007 Section 7.3.
    static const char* const kStatusString[] = {
        "unknown",    // MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN
        "available",  // MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE
        "current",    // MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT
        "forbidden",  // MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN
    };
    parsed[kStatusProperty] = kStatusString[status];
  }

  // MMModemAccessTechnology
  if (result.Contains<uint32_t>(kOperatorAccessTechnologyProperty)) {
    parsed[kTechnologyProperty] = AccessTechnologyToString(
        result.Get<uint32_t>(kOperatorAccessTechnologyProperty));
  }

  string operator_long, operator_short, operator_code;
  if (result.Contains<string>(kOperatorLongProperty))
    parsed[kLongNameProperty] = result.Get<string>(kOperatorLongProperty);
  if (result.Contains<string>(kOperatorShortProperty))
    parsed[kShortNameProperty] = result.Get<string>(kOperatorShortProperty);
  if (result.Contains<string>(kOperatorCodeProperty))
    parsed[kNetworkIdProperty] = result.Get<string>(kOperatorCodeProperty);

  // If the long name is not available but the network ID is, look up the long
  // name in the mobile provider database.
  if ((!base::Contains(parsed, kLongNameProperty) ||
       parsed[kLongNameProperty].empty()) &&
      base::Contains(parsed, kNetworkIdProperty)) {
    mobile_operator_info_->Reset();
    mobile_operator_info_->UpdateMCCMNC(parsed[kNetworkIdProperty]);
    if (mobile_operator_info_->IsMobileNetworkOperatorKnown() &&
        !mobile_operator_info_->operator_name().empty()) {
      parsed[kLongNameProperty] = mobile_operator_info_->operator_name();
    }
  }
  return parsed;
}

void CellularCapability3gpp::SetInitialEpsBearer(
    const KeyValueStore& properties,
    Error* error,
    const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  if (modem_3gpp_proxy_) {
    modem_3gpp_proxy_->SetInitialEpsBearerSettings(properties, error, callback,
                                                   kTimeoutSetInitialEpsBearer);
  } else {
    SLOG(this, 3) << __func__ << " skipping, no 3GPP proxy";
  }
}

void CellularCapability3gpp::OnSetInitialEpsBearerReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Failed to set the 'attach APN' for the EPS bearer.";
  }
}

void CellularCapability3gpp::SetupLocation(uint32_t sources,
                                           bool signal_location,
                                           const ResultCallback& callback) {
  Error error;
  modem_location_proxy_->Setup(sources, signal_location, &error, callback,
                               kTimeoutSetupLocation);
}

void CellularCapability3gpp::OnSetupLocationReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    // Not fatal: most devices already enable this when
    // ModemManager starts. This failure is only likely for devices
    // which don't support location gathering.
    SLOG(this, 2) << "Failed to setup modem location capability.";
    return;
  }
}

void CellularCapability3gpp::GetLocation(const StringCallback& callback) {
  BrilloAnyCallback cb = Bind(&CellularCapability3gpp::OnGetLocationReply,
                              weak_ptr_factory_.GetWeakPtr(), callback);
  Error error;
  modem_location_proxy_->GetLocation(&error, cb, kTimeoutGetLocation);
}

void CellularCapability3gpp::OnGetLocationReply(
    const StringCallback& callback,
    const std::map<uint32_t, brillo::Any>& results,
    const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Error getting location.";
    return;
  }
  // For 3G modems we currently only care about the "MCC,MNC,LAC,CI" location
  auto it = results.find(MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI);
  if (it != results.end()) {
    brillo::Any gpp_value = it->second;
    const string& location_string = gpp_value.Get<const string>();
    callback.Run(location_string, Error());
  } else {
    callback.Run(std::string(), Error());
  }
}

bool CellularCapability3gpp::IsLocationUpdateSupported() const {
  // Allow modems as they're tested / needed
  return cellular()->mm_plugin() == kTelitMMPlugin;
}

CellularBearer* CellularCapability3gpp::GetActiveBearer() const {
  return active_bearer_.get();
}

const std::vector<std::unique_ptr<MobileOperatorInfo::MobileAPN>>&
CellularCapability3gpp::GetProfiles() const {
  return profiles_;
}

string CellularCapability3gpp::GetNetworkTechnologyString() const {
  return AccessTechnologyToString(access_technologies_);
}

string CellularCapability3gpp::GetRoamingStateString() const {
  switch (registration_state_) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
      return kRoamingStateHome;
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
      return kRoamingStateRoaming;
    default:
      break;
  }
  return kRoamingStateUnknown;
}

string CellularCapability3gpp::GetTypeString() const {
  return AccessTechnologyToTechnologyFamily(access_technologies_);
}

void CellularCapability3gpp::OnModemPropertiesChanged(
    const KeyValueStore& properties,
    const vector<string>& /* invalidated_properties */) {
  // Update the bearers property before the modem state property as
  // OnModemStateChanged may call UpdateActiveBearer, which reads the bearers
  // property.
  if (properties.Contains<RpcIdentifiers>(MM_MODEM_PROPERTY_BEARERS)) {
    RpcIdentifiers bearers =
        properties.Get<RpcIdentifiers>(MM_MODEM_PROPERTY_BEARERS);
    OnBearersChanged(bearers);
  }

  // This solves a bootstrapping problem: If the modem is not yet
  // enabled, there are no proxy objects associated with the capability
  // object, so modem signals like StateChanged aren't seen. By monitoring
  // changes to the State property via the ModemManager, we're able to
  // get the initialization process started, which will result in the
  // creation of the proxy objects.
  //
  // The first time we see the change to State (when the modem state
  // is Unknown), we simply update the state, and rely on the Manager to
  // enable the device when it is registered with the Manager. On subsequent
  // changes to State, we need to explicitly enable the device ourselves.
  if (properties.Contains<int32_t>(MM_MODEM_PROPERTY_STATE)) {
    int32_t istate = properties.Get<int32_t>(MM_MODEM_PROPERTY_STATE);
    Cellular::ModemState state = static_cast<Cellular::ModemState>(istate);
    OnModemStateChanged(state);
  }
  if (properties.Contains<RpcIdentifier>(MM_MODEM_PROPERTY_SIM))
    OnSimPathChanged(properties.Get<RpcIdentifier>(MM_MODEM_PROPERTY_SIM));

  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES)) {
    OnModemCurrentCapabilitiesChanged(
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_MANUFACTURER)) {
    cellular()->set_manufacturer(
        properties.Get<string>(MM_MODEM_PROPERTY_MANUFACTURER));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_MODEL)) {
    cellular()->set_model_id(properties.Get<string>(MM_MODEM_PROPERTY_MODEL));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_PLUGIN)) {
    cellular()->set_mm_plugin(properties.Get<string>(MM_MODEM_PROPERTY_PLUGIN));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_REVISION)) {
    OnModemRevisionChanged(properties.Get<string>(MM_MODEM_PROPERTY_REVISION));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_HARDWAREREVISION)) {
    OnModemHardwareRevisionChanged(
        properties.Get<string>(MM_MODEM_PROPERTY_HARDWAREREVISION));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_DEVICE)) {
    OnModemDevicePathChanged(properties.Get<string>(MM_MODEM_PROPERTY_DEVICE));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_EQUIPMENTIDENTIFIER)) {
    cellular()->set_equipment_id(
        properties.Get<string>(MM_MODEM_PROPERTY_EQUIPMENTIDENTIFIER));
  }

  // Unlock required and SimLock
  bool lock_status_changed = false;
  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED)) {
    uint32_t unlock_required =
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED);
    OnLockTypeChanged(static_cast<MMModemLock>(unlock_required));
    lock_status_changed = true;
  }

  // Unlock retries
  if (properties.ContainsVariant(MM_MODEM_PROPERTY_UNLOCKRETRIES)) {
    OnLockRetriesChanged(properties.GetVariant(MM_MODEM_PROPERTY_UNLOCKRETRIES)
                             .Get<LockRetryData>());
    lock_status_changed = true;
  }

  if (lock_status_changed)
    OnSimLockStatusChanged();

  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_ACCESSTECHNOLOGIES)) {
    OnAccessTechnologiesChanged(
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_ACCESSTECHNOLOGIES));
  }

  if (properties.ContainsVariant(MM_MODEM_PROPERTY_SIGNALQUALITY)) {
    SignalQuality quality =
        properties.GetVariant(MM_MODEM_PROPERTY_SIGNALQUALITY)
            .Get<SignalQuality>();
    OnSignalQualityChanged(std::get<0>(quality));
  }

  if (properties.Contains<Strings>(MM_MODEM_PROPERTY_OWNNUMBERS)) {
    vector<string> numbers =
        properties.Get<Strings>(MM_MODEM_PROPERTY_OWNNUMBERS);
    string mdn;
    if (!numbers.empty())
      mdn = numbers[0];
    OnMdnChanged(mdn);
  }
}

void CellularCapability3gpp::OnPropertiesChanged(
    const string& interface,
    const KeyValueStore& changed_properties,
    const vector<string>& invalidated_properties) {
  SLOG(this, 3) << __func__ << "(" << interface << ")";
  if (interface == MM_DBUS_INTERFACE_MODEM) {
    OnModemPropertiesChanged(changed_properties, invalidated_properties);
  }
  if (interface == MM_DBUS_INTERFACE_MODEM_MODEM3GPP) {
    OnModem3gppPropertiesChanged(changed_properties, invalidated_properties);
  }
  if (interface == MM_DBUS_INTERFACE_SIM) {
    OnSimPropertiesChanged(changed_properties, invalidated_properties);
  }
}

bool CellularCapability3gpp::RetriableConnectError(const Error& error) const {
  return error.type() == Error::kInvalidApn;
}

bool CellularCapability3gpp::IsValidSimPath(
    const RpcIdentifier& sim_path) const {
  return !sim_path.value().empty() && sim_path != kRootPath;
}

string CellularCapability3gpp::NormalizeMdn(const string& mdn) const {
  string normalized_mdn;
  for (size_t i = 0; i < mdn.size(); ++i) {
    if (base::IsAsciiDigit(mdn[i]))
      normalized_mdn += mdn[i];
  }
  return normalized_mdn;
}

void CellularCapability3gpp::OnSimPathChanged(const RpcIdentifier& sim_path) {
  if (sim_path == sim_path_)
    return;

  sim_proxy_ = nullptr;
  if (IsValidSimPath(sim_path)) {
    sim_proxy_ = control_interface()->CreateMM1SimProxy(
        sim_path, cellular()->dbus_service());
  }
  sim_path_ = sim_path;

  if (!IsValidSimPath(sim_path)) {
    // Clear all data about the sim
    cellular()->set_imsi("");
    spn_ = "";
    cellular()->set_sim_present(false);
    OnSimIdentifierChanged("");
    OnOperatorIdChanged("");
    cellular()->home_provider_info()->Reset();
  } else {
    cellular()->set_sim_present(true);
    std::unique_ptr<DBusPropertiesProxyInterface> properties_proxy =
        control_interface()->CreateDBusPropertiesProxy(
            sim_path, cellular()->dbus_service());
    // TODO(jglasgow): convert to async interface
    KeyValueStore properties(properties_proxy->GetAll(MM_DBUS_INTERFACE_SIM));
    OnSimPropertiesChanged(properties, vector<string>());
  }
}

void CellularCapability3gpp::OnModemCurrentCapabilitiesChanged(
    uint32_t current_capabilities) {
  current_capabilities_ = current_capabilities;

  // Only allow network scan when the modem's current capabilities support
  // GSM/UMTS.
  //
  // TODO(benchan): We should consider having the modem plugins in ModemManager
  // reporting whether network scan is supported.
  cellular()->set_scanning_supported(
      (current_capabilities & MM_MODEM_CAPABILITY_GSM_UMTS) != 0);
}

void CellularCapability3gpp::OnMdnChanged(const string& mdn) {
  cellular()->set_mdn(NormalizeMdn(mdn));
  UpdateServiceActivationState();
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnModemRevisionChanged(const string& revision) {
  cellular()->set_firmware_revision(revision);
}

void CellularCapability3gpp::OnModemHardwareRevisionChanged(
    const string& hardware_revision) {
  cellular()->set_hardware_revision(hardware_revision);
}

void CellularCapability3gpp::OnModemDevicePathChanged(const string& path) {
  cellular()->set_device_id(DeviceId::CreateFromSysfs(base::FilePath(path)));
}

void CellularCapability3gpp::OnModemStateChanged(Cellular::ModemState state) {
  SLOG(this, 3) << __func__ << ": " << Cellular::GetModemStateString(state);

  if (state == Cellular::kModemStateConnected) {
    // This assumes that ModemManager updates the Bearers list and the Bearer
    // properties before changing Modem state to Connected.
    SLOG(this, 2) << "Update active bearer.";
    UpdateActiveBearer();
  }

  cellular()->OnModemStateChanged(state);
  // TODO(armansito): Move the deferred enable logic to Cellular
  // (See crbug.com/279499).
  if (!deferred_enable_modem_callback_.is_null() &&
      state == Cellular::kModemStateDisabled) {
    SLOG(this, 2) << "Enabling modem after deferring.";
    deferred_enable_modem_callback_.Run();
    deferred_enable_modem_callback_.Reset();
  }
}

void CellularCapability3gpp::OnAccessTechnologiesChanged(
    uint32_t access_technologies) {
  if (access_technologies_ != access_technologies) {
    const string old_type_string(GetTypeString());
    access_technologies_ = access_technologies;
    const string new_type_string(GetTypeString());
    if (new_type_string != old_type_string) {
      // TODO(jglasgow): address layering violation of emitting change
      // signal here for a property owned by Cellular.
      cellular()->adaptor()->EmitStringChanged(kTechnologyFamilyProperty,
                                               new_type_string);
    }
    if (cellular()->service().get()) {
      cellular()->service()->SetNetworkTechnology(GetNetworkTechnologyString());
    }
  }
}

void CellularCapability3gpp::OnBearersChanged(const RpcIdentifiers& bearers) {
  bearer_paths_ = bearers;
}

void CellularCapability3gpp::OnLockRetriesChanged(
    const LockRetryData& lock_retries) {
  SLOG(this, 3) << __func__;

  // UI uses lock_retries to indicate the number of attempts remaining
  // for enable pin/disable pin/change pin
  // By default, the UI operates on PIN1, thus lock_retries should return
  // number of PIN1 retries. The only exception is PUK lock, where the UI needs
  // to report the number of PUK retries.
  // TODO(pholla): Personalization requires the UI to display multiple locks,
  // so shill needs to communicate an array of sim_lock_status (b/169615875)
  auto retry_lock_type = (sim_lock_status_.lock_type == MM_MODEM_LOCK_SIM_PUK)
                             ? MM_MODEM_LOCK_SIM_PUK
                             : MM_MODEM_LOCK_SIM_PIN;
  auto it = lock_retries.find(retry_lock_type);

  sim_lock_status_.retries_left =
      (it != lock_retries.end()) ? it->second : kUnknownLockRetriesLeft;
}

void CellularCapability3gpp::OnLockTypeChanged(MMModemLock lock_type) {
  SLOG(this, 3) << __func__ << ": " << lock_type;
  sim_lock_status_.lock_type = lock_type;

  // If the SIM is in a locked state |sim_lock_status_.enabled| might be false.
  // This is because the corresponding property 'EnabledFacilityLocks' is on
  // the 3GPP interface and the 3GPP interface is not available while the Modem
  // is in the 'LOCKED' state.
  if (lock_type != MM_MODEM_LOCK_NONE && lock_type != MM_MODEM_LOCK_UNKNOWN &&
      !sim_lock_status_.enabled)
    sim_lock_status_.enabled = true;
}

void CellularCapability3gpp::OnSimLockStatusChanged() {
  SLOG(this, 3) << __func__;
  cellular()->adaptor()->EmitKeyValueStoreChanged(
      kSIMLockStatusProperty, SimLockStatusToProperty(nullptr));

  // If the SIM is currently unlocked, assume that we need to refresh
  // carrier information, since a locked SIM prevents shill from obtaining
  // the necessary data to establish a connection later (e.g. IMSI).
  if (IsValidSimPath(sim_path_) &&
      (sim_lock_status_.lock_type == MM_MODEM_LOCK_NONE ||
       sim_lock_status_.lock_type == MM_MODEM_LOCK_UNKNOWN)) {
    std::unique_ptr<DBusPropertiesProxyInterface> properties_proxy =
        control_interface()->CreateDBusPropertiesProxy(
            sim_path_, cellular()->dbus_service());
    KeyValueStore properties(properties_proxy->GetAll(MM_DBUS_INTERFACE_SIM));
    OnSimPropertiesChanged(properties, vector<string>());
  }
}

void CellularCapability3gpp::OnModem3gppPropertiesChanged(
    const KeyValueStore& properties,
    const vector<string>& /* invalidated_properties */) {
  SLOG(this, 3) << __func__;
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_IMEI))
    cellular()->set_imei(
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_IMEI));

  // Handle registration state changes as a single change
  Stringmap::const_iterator it;
  string operator_code;
  string operator_name;
  it = serving_operator_.find(kOperatorCodeKey);
  if (it != serving_operator_.end())
    operator_code = it->second;
  it = serving_operator_.find(kOperatorNameKey);
  if (it != serving_operator_.end())
    operator_name = it->second;

  MMModem3gppRegistrationState state = registration_state_;
  bool registration_changed = false;
  if (properties.Contains<uint32_t>(
          MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE)) {
    state = static_cast<MMModem3gppRegistrationState>(properties.Get<uint32_t>(
        MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE));
    registration_changed = true;
  }
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORCODE)) {
    operator_code =
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORCODE);
    registration_changed = true;
  }
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORNAME)) {
    operator_name =
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORNAME);
    registration_changed = true;
  }
  if (registration_changed)
    On3gppRegistrationChanged(state, operator_code, operator_name);

  if (properties.Contains<uint32_t>(
          MM_MODEM_MODEM3GPP_PROPERTY_ENABLEDFACILITYLOCKS))
    OnFacilityLocksChanged(properties.Get<uint32_t>(
        MM_MODEM_MODEM3GPP_PROPERTY_ENABLEDFACILITYLOCKS));

  if (properties.ContainsVariant(MM_MODEM_MODEM3GPP_PROPERTY_PCO)) {
    OnPcoChanged(
        properties.GetVariant(MM_MODEM_MODEM3GPP_PROPERTY_PCO).Get<PcoList>());
  }

  if (properties.ContainsVariant(MM_MODEM_MODEM3GPP_PROPERTY_PROFILES)) {
    OnProfilesChanged(
        properties.GetVariant(MM_MODEM_MODEM3GPP_PROPERTY_PROFILES)
            .Get<Profiles>());
  }
}

void CellularCapability3gpp::OnProfilesChanged(const Profiles& profiles) {
  profiles_.clear();
  for (const auto& profile : profiles) {
    auto apn_info = std::make_unique<MobileOperatorInfo::MobileAPN>();
    apn_info->apn =
        brillo::GetVariantValueOrDefault<string>(profile, kProfileApn);
    apn_info->username =
        brillo::GetVariantValueOrDefault<string>(profile, kProfileUsername);
    apn_info->password =
        brillo::GetVariantValueOrDefault<string>(profile, kProfilePassword);
    apn_info->authentication =
        MMBearerAllowedAuthToApnAuthentication(static_cast<MMBearerAllowedAuth>(
            brillo::GetVariantValueOrDefault<uint32_t>(profile,
                                                       kProfileAuthType)));
    profiles_.push_back(std::move(apn_info));
  }

  // The cellular object may need to update the APN list now.
  cellular()->OnOperatorChanged();

  // Set the new parameters for the initial EPS bearer (e.g. LTE Attach APN)
  KeyValueStore properties;
  Error error;
  FillInitialEpsBearerPropertyMap(&properties);
  ResultCallback cb = Bind(&CellularCapability3gpp::OnSetInitialEpsBearerReply,
                           weak_ptr_factory_.GetWeakPtr());
  if (!properties.IsEmpty())
    SetInitialEpsBearer(properties, &error, cb);
}

void CellularCapability3gpp::On3gppRegistrationChanged(
    MMModem3gppRegistrationState state,
    const string& operator_code,
    const string& operator_name) {
  SLOG(this, 3) << __func__ << ": regstate=" << state
                << ", opercode=" << operator_code
                << ", opername=" << operator_name;

  // While the modem is connected, if the state changed from a registered state
  // to a non registered state, defer the state change by 15 seconds.
  if (cellular()->modem_state() == Cellular::kModemStateConnected &&
      IsRegistered() && !IsRegisteredState(state)) {
    if (!registration_dropped_update_callback_.IsCancelled()) {
      LOG(WARNING) << "Modem reported consecutive 3GPP registration drops. "
                   << "Ignoring earlier notifications.";
      registration_dropped_update_callback_.Cancel();
    } else {
      // This is not a repeated post. So, count this instance of delayed drop
      // posted.
      modem_info()->metrics()->Notify3GPPRegistrationDelayedDropPosted();
    }
    SLOG(this, 2) << "Posted deferred registration state update";
    registration_dropped_update_callback_.Reset(Bind(
        &CellularCapability3gpp::Handle3gppRegistrationChange,
        weak_ptr_factory_.GetWeakPtr(), state, operator_code, operator_name));
    cellular()->dispatcher()->PostDelayedTask(
        FROM_HERE, registration_dropped_update_callback_.callback(),
        registration_dropped_update_timeout_milliseconds_);
  } else {
    if (!registration_dropped_update_callback_.IsCancelled()) {
      SLOG(this, 2) << "Cancelled a deferred registration state update";
      registration_dropped_update_callback_.Cancel();
      // If we cancelled the callback here, it means we had flaky network for a
      // small duration.
      modem_info()->metrics()->Notify3GPPRegistrationDelayedDropCanceled();
    }
    Handle3gppRegistrationChange(state, operator_code, operator_name);
  }
}

void CellularCapability3gpp::Handle3gppRegistrationChange(
    MMModem3gppRegistrationState updated_state,
    const string& updated_operator_code,
    const string& updated_operator_name) {
  SLOG(this, 3) << __func__ << ": regstate=" << updated_state
                << ", opercode=" << updated_operator_code
                << ", opername=" << updated_operator_name;

  registration_state_ = updated_state;
  serving_operator_[kOperatorCodeKey] = updated_operator_code;
  serving_operator_[kOperatorNameKey] = updated_operator_name;
  cellular()->serving_operator_info()->UpdateMCCMNC(updated_operator_code);
  cellular()->serving_operator_info()->UpdateOperatorName(
      updated_operator_name);

  cellular()->HandleNewRegistrationState();

  // A finished callback does not qualify as a canceled callback.
  // We test for a canceled callback to check for outstanding callbacks.
  // So, explicitly cancel the callback here.
  // Caution: Do not use any function arguments post the call to Cancel().
  // Cancel() call invalidates the arguments that were copied when creating
  // the callback.
  registration_dropped_update_callback_.Cancel();

  // If the modem registered with the network and the current ICCID is pending
  // activation, then reset the modem.
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnSubscriptionStateChanged(
    SubscriptionState updated_subscription_state) {
  SLOG(this, 3) << __func__ << ": Updated subscription state = "
                << SubscriptionStateToString(updated_subscription_state);

  if (updated_subscription_state == subscription_state_)
    return;

  subscription_state_ = updated_subscription_state;

  UpdateServiceActivationState();
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnModemStateChangedSignal(int32_t old_state,
                                                       int32_t new_state,
                                                       uint32_t reason) {
  Cellular::ModemState old_modem_state =
      static_cast<Cellular::ModemState>(old_state);
  Cellular::ModemState new_modem_state =
      static_cast<Cellular::ModemState>(new_state);
  SLOG(this, 3) << __func__ << "("
                << Cellular::GetModemStateString(old_modem_state) << ", "
                << Cellular::GetModemStateString(new_modem_state) << ", "
                << reason << ")";
}

void CellularCapability3gpp::OnSignalQualityChanged(uint32_t quality) {
  // Chrome OS UI uses signal quality values set by this method to draw network
  // icons. UI code maps |quality| to to number of bars: [1-25] 1 bar,
  // [26-50] 2 bars, [51-75] 3 bars and [76-100] 4 bars.
  // Modem manager measures signal strength in RSSI and maps it to a value in
  // the range of [0-100].
  // We don't want linear mappings in celluar signal strength icons.
  // The mappings we desire are: [1-12] 1 bar, [13-24] 2 bars, [25-37] 3 bars
  // and [38-100] 4 bars.
  // A simple way to accomplish the desired mappings is to scale signal strength
  // measurements by 2*x+1.
  // For example: modem manager reports a signal strength of 25. After
  // applying our scaling function chrome OS UI will receive a reading of 51.
  // 51 maps to an icon with 3 bars on Chrome OS UI.
  uint32_t scaled_quality = std::min(100u, 2 * quality + 1);
  cellular()->HandleNewSignalQuality(scaled_quality);
}

void CellularCapability3gpp::OnFacilityLocksChanged(uint32_t locks) {
  bool sim_enabled = !!(locks & MM_MODEM_3GPP_FACILITY_SIM);
  if (sim_lock_status_.enabled != sim_enabled) {
    sim_lock_status_.enabled = sim_enabled;
    OnSimLockStatusChanged();
  }
}

void CellularCapability3gpp::OnPcoChanged(const PcoList& pco_list) {
  SLOG(this, 3) << __func__;

  for (const auto& pco_info : pco_list) {
    uint32_t session_id = std::get<0>(pco_info);
    bool is_complete = std::get<1>(pco_info);
    vector<uint8_t> data = std::get<2>(pco_info);

    SLOG(this, 3) << "PCO: session-id=" << session_id
                  << ", complete=" << is_complete
                  << ", data=" << base::HexEncode(data.data(), data.size())
                  << "";

    std::unique_ptr<CellularPco> pco = CellularPco::CreateFromRawData(data);
    if (!pco) {
      LOG(WARNING) << "Failed to parse PCO (session-id " << session_id << ")";
      continue;
    }

    SubscriptionState subscription_state = SubscriptionState::kUnknown;
    if (!FindVerizonSubscriptionStateFromPco(*pco, &subscription_state))
      continue;

    if (subscription_state != SubscriptionState::kUnknown)
      OnSubscriptionStateChanged(subscription_state);
  }
}

void CellularCapability3gpp::OnSimPropertiesChanged(
    const KeyValueStore& props,
    const vector<string>& /* invalidated_properties */) {
  SLOG(this, 3) << __func__;
  if (props.Contains<string>(MM_SIM_PROPERTY_SIMIDENTIFIER))
    OnSimIdentifierChanged(props.Get<string>(MM_SIM_PROPERTY_SIMIDENTIFIER));
  if (props.Contains<string>(MM_SIM_PROPERTY_OPERATORIDENTIFIER))
    OnOperatorIdChanged(props.Get<string>(MM_SIM_PROPERTY_OPERATORIDENTIFIER));
  if (props.Contains<string>(MM_SIM_PROPERTY_OPERATORNAME))
    OnSpnChanged(props.Get<string>(MM_SIM_PROPERTY_OPERATORNAME));
  if (props.Contains<string>(MM_SIM_PROPERTY_IMSI)) {
    string imsi = props.Get<string>(MM_SIM_PROPERTY_IMSI);
    cellular()->set_imsi(imsi);
    cellular()->home_provider_info()->UpdateIMSI(imsi);
    // We do not obtain IMSI OTA right now. Provide the value from the SIM to
    // serving operator as well, to aid in MVNO identification.
    cellular()->serving_operator_info()->UpdateIMSI(imsi);
  }
}

void CellularCapability3gpp::OnSpnChanged(const std::string& spn) {
  spn_ = spn;
  cellular()->home_provider_info()->UpdateOperatorName(spn);
}

void CellularCapability3gpp::OnSimIdentifierChanged(const string& id) {
  cellular()->set_iccid(id);
  cellular()->home_provider_info()->UpdateICCID(id);
  // Provide ICCID to serving operator as well to aid in MVNO identification.
  cellular()->serving_operator_info()->UpdateICCID(id);
  UpdateServiceActivationState();
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnOperatorIdChanged(const string& operator_id) {
  SLOG(this, 2) << "Operator ID = '" << operator_id << "'";
  cellular()->home_provider_info()->UpdateMCCMNC(operator_id);
}

}  // namespace shill
