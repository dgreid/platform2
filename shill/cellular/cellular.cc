// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular.h"

#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <ModemManager/ModemManager.h>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/cellular_bearer.h"
#include "shill/cellular/cellular_capability.h"
#include "shill/cellular/cellular_service.h"
#include "shill/cellular/cellular_service_provider.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/cellular/modem_info.h"
#include "shill/connection.h"
#include "shill/control_interface.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/external_task.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/netlink_sock_diag.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/sockets.h"
#include "shill/ppp_daemon.h"
#include "shill/ppp_device.h"
#include "shill/ppp_device_factory.h"
#include "shill/process_manager.h"
#include "shill/profile.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"
#include "shill/technology.h"

using base::Bind;
using base::StringPrintf;
using std::map;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(const Cellular* c) {
  return c->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

class ApnList {
 public:
  void AddApns(
      const std::vector<std::unique_ptr<MobileOperatorInfo::MobileAPN>>& apns) {
    for (const auto& mobile_apn : apns)
      AddApn(mobile_apn);
  }

  const Stringmaps& GetList() { return apn_dict_list_; }

 private:
  using ApnIndexKey =
      std::tuple<std::string, std::string, std::string, std::string>;

  ApnIndexKey GetKey(
      const std::unique_ptr<MobileOperatorInfo::MobileAPN>& mobile_apn) {
    return std::make_tuple(mobile_apn->apn, mobile_apn->username,
                           mobile_apn->password, mobile_apn->authentication);
  }

  void AddApn(
      const std::unique_ptr<MobileOperatorInfo::MobileAPN>& mobile_apn) {
    ApnIndexKey index = GetKey(mobile_apn);
    if (apn_index_[index] == nullptr) {
      apn_dict_list_.emplace_back();
      apn_index_[index] = &apn_dict_list_.back();
    }

    Stringmap* props = apn_index_[index];
    if (!mobile_apn->apn.empty())
      props->emplace(kApnProperty, mobile_apn->apn);
    if (!mobile_apn->username.empty())
      props->emplace(kApnUsernameProperty, mobile_apn->username);
    if (!mobile_apn->password.empty())
      props->emplace(kApnPasswordProperty, mobile_apn->password);
    if (!mobile_apn->authentication.empty())
      props->emplace(kApnAuthenticationProperty, mobile_apn->authentication);
    if (mobile_apn->is_attach_apn)
      props->emplace(kApnAttachProperty, kApnAttachProperty);
    if (!mobile_apn->ip_type.empty())
      props->emplace(kApnIpTypeProperty, mobile_apn->ip_type);

    // Find the first localized and non-localized name, if any.
    if (!mobile_apn->operator_name_list.empty())
      props->emplace(kApnNameProperty, mobile_apn->operator_name_list[0].name);
    for (const auto& lname : mobile_apn->operator_name_list) {
      if (!lname.language.empty())
        props->emplace(kApnLocalizedNameProperty, lname.name);
    }
  }

  Stringmaps apn_dict_list_;
  std::map<ApnIndexKey, Stringmap*> apn_index_;
};

}  // namespace

// static
const char Cellular::kAllowRoaming[] = "AllowRoaming";
const char Cellular::kUseAttachApn[] = "UseAttachAPN";
const int64_t Cellular::kDefaultScanningTimeoutMilliseconds = 60000;
const int64_t Cellular::kPollLocationIntervalMilliseconds = 300000;  // 5 mins
const char Cellular::kGenericServiceNamePrefix[] = "MobileNetwork";
unsigned int Cellular::friendly_service_name_id_ = 1;

Cellular::Cellular(ModemInfo* modem_info,
                   const string& link_name,
                   const string& address,
                   int interface_index,
                   Type type,
                   const string& service,
                   const RpcIdentifier& path)
    : Device(modem_info->manager(),
             link_name,
             address,
             interface_index,
             Technology::kCellular),
      state_(kStateDisabled),
      modem_state_(kModemStateUnknown),
      home_provider_info_(new MobileOperatorInfo(
          modem_info->manager()->dispatcher(), "HomeProvider")),
      serving_operator_info_(new MobileOperatorInfo(
          modem_info->manager()->dispatcher(), "ServingOperator")),
      dbus_service_(service),
      dbus_path_(path),
      dbus_path_str_(path.value()),
      scanning_supported_(false),
      scanning_(false),
      polling_location_(false),
      provider_requires_roaming_(false),
      scan_interval_(0),
      sim_present_(false),
      type_(type),
      ppp_device_factory_(PPPDeviceFactory::GetInstance()),
      process_manager_(ProcessManager::GetInstance()),
      allow_roaming_(false),
      use_attach_apn_(false),
      inhibited_(false),
      proposed_scan_in_progress_(false),
      explicit_disconnect_(false),
      is_ppp_authenticating_(false),
      scanning_timeout_milliseconds_(kDefaultScanningTimeoutMilliseconds),
      weak_ptr_factory_(this) {
  RegisterProperties();

  // TODO(pprabhu) Split MobileOperatorInfo into a context that stores the
  // costly database, and lighter objects that |Cellular| can own.
  // crbug.com/363874
  home_provider_info_->Init();
  serving_operator_info_->Init();
  home_provider_info_->AddObserver(this);
  serving_operator_info_->AddObserver(this);

  socket_destroyer_ = NetlinkSockDiag::Create(std::make_unique<Sockets>());
  if (!socket_destroyer_) {
    LOG(WARNING) << "Socket destroyer failed to initialize; "
                 << "IPv6 will be unavailable.";
  }

  mm1_proxy_ = control_interface()->CreateMM1Proxy(dbus_service_);

  SLOG(this, 1) << "Cellular() " << this->link_name();
}

Cellular::~Cellular() {
  SLOG(this, 1) << "~Cellular() " << this->link_name();
}

string Cellular::GetEquipmentIdentifier() const {
  // 3GPP devices are uniquely identified by IMEI, which has 15 decimal digits.
  if (!imei_.empty())
    return imei_;

  // 3GPP2 devices are uniquely identified by MEID, which has 14 hexadecimal
  // digits.
  if (!meid_.empty())
    return meid_;

  // An equipment ID may be reported by ModemManager, which is typically the
  // serial number of a legacy AT modem, and is either the IMEI, MEID, or ESN
  // of a MBIM/QMI modem. This is used as a fallback in case neither IMEI nor
  // MEID could be retrieved through ModemManager (e.g. when there is no SIM
  // inserted, ModemManager doesn't expose modem 3GPP interface where the IMEI
  // is reported).
  if (!equipment_id_.empty())
    return equipment_id_;

  // If none of IMEI, MEID, and equipment ID is available, fall back to MAC
  // address.
  return mac_address();
}

string Cellular::GetStorageIdentifier() const {
  return "device_" + GetEquipmentIdentifier();
}

bool Cellular::Load(const StoreInterface* storage) {
  const string id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    LOG(WARNING) << "Device is not available in the persistent store: " << id;
    return false;
  }
  storage->GetBool(id, kAllowRoaming, &allow_roaming_);
  storage->GetBool(id, kUseAttachApn, &use_attach_apn_);
  return Device::Load(storage);
}

bool Cellular::Save(StoreInterface* storage) {
  const string id = GetStorageIdentifier();
  storage->SetBool(id, kAllowRoaming, allow_roaming_);
  storage->SetBool(id, kUseAttachApn, use_attach_apn_);
  return Device::Save(storage);
}

// static
string Cellular::GetStateString(State state) {
  switch (state) {
    case kStateDisabled:
      return "CellularStateDisabled";
    case kStateEnabled:
      return "CellularStateEnabled";
    case kStateRegistered:
      return "CellularStateRegistered";
    case kStateConnected:
      return "CellularStateConnected";
    case kStateLinked:
      return "CellularStateLinked";
    default:
      NOTREACHED();
  }
  return StringPrintf("CellularStateUnknown-%d", state);
}

// static
string Cellular::GetModemStateString(ModemState modem_state) {
  switch (modem_state) {
    case kModemStateFailed:
      return "CellularModemStateFailed";
    case kModemStateUnknown:
      return "CellularModemStateUnknown";
    case kModemStateInitializing:
      return "CellularModemStateInitializing";
    case kModemStateLocked:
      return "CellularModemStateLocked";
    case kModemStateDisabled:
      return "CellularModemStateDisabled";
    case kModemStateDisabling:
      return "CellularModemStateDisabling";
    case kModemStateEnabling:
      return "CellularModemStateEnabling";
    case kModemStateEnabled:
      return "CellularModemStateEnabled";
    case kModemStateSearching:
      return "CellularModemStateSearching";
    case kModemStateRegistered:
      return "CellularModemStateRegistered";
    case kModemStateDisconnecting:
      return "CellularModemStateDisconnecting";
    case kModemStateConnecting:
      return "CellularModemStateConnecting";
    case kModemStateConnected:
      return "CellularModemStateConnected";
    default:
      NOTREACHED();
  }
  return StringPrintf("CellularModemStateUnknown-%d", modem_state);
}

string Cellular::GetCapabilityStateString(CapabilityState capability_state) {
  switch (capability_state) {
    case CapabilityState::kCellularStopped:
      return "CellularStopped";
    case CapabilityState::kCellularStarted:
      return "CellularStarted";
    case CapabilityState::kModemStarting:
      return "ModemStarting";
    case CapabilityState::kModemStarted:
      return "ModemStarted";
    case CapabilityState::kModemStopping:
      return "ModemStopping";
  }
  return StringPrintf("CellularCapabilityStateUnknown-%d", capability_state);
}

string Cellular::GetTechnologyFamily(Error* error) {
  return capability_ ? capability_->GetTypeString() : "";
}

string Cellular::GetDeviceId(Error* error) {
  return device_id_ ? device_id_->AsString() : "";
}

bool Cellular::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  if (!device_id_)
    return false;

  // The cdc-mbim kernel driver stop draining the receive buffer after the
  // network interface is brought down. However, some MBIM modem (see
  // b:71505232) may misbehave if the host stops draining the receiver buffer
  // before issuing a MBIM command to disconnect the modem from network. To
  // work around the issue, shill needs to defer bringing down the network
  // interface until after the modem is disabled.
  //
  // TODO(benchan): Investigate if we need to apply the workaround for other
  // MBIM modems or revert this change once the issue is addressed by the modem
  // firmware on Fibocom L850-GL.
  static constexpr DeviceId kAffectedDeviceIds[] = {
      {DeviceId::BusType::kUsb, 0x2cb7, 0x0007},  // Fibocom L850-GL
  };
  for (const auto& affected_device_id : kAffectedDeviceIds) {
    if (device_id_->Match(affected_device_id))
      return true;
  }

  return false;
}

void Cellular::SetState(State state) {
  SLOG(this, 1) << __func__ << ": " << GetStateString(state_) << " -> "
                << GetStateString(state);
  state_ = state;
}

void Cellular::HelpRegisterDerivedBool(const string& name,
                                       bool (Cellular::*get)(Error* error),
                                       bool (Cellular::*set)(const bool& value,
                                                             Error* error)) {
  mutable_store()->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<Cellular, bool>(this, get, set)));
}

void Cellular::HelpRegisterConstDerivedString(const string& name,
                                              string (Cellular::*get)(Error*)) {
  mutable_store()->RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<Cellular, string>(this, get, nullptr)));
}

void Cellular::Start(Error* error,
                     const EnabledStateChangedCallback& callback) {
  DCHECK(error);
  SLOG(this, 1) << __func__ << ": " << GetStateString(state_);
  // We can only short circuit the start operation if both the cellular state
  // is not disabled AND the proxies have been initialized.  We have seen
  // crashes due to NULL proxies and the state being not disabled.
  if (state_ != kStateDisabled && capability_->AreProxiesInitialized()) {
    LOG(WARNING) << __func__ << ": Skipping Start.";
    if (error)
      error->Reset();
    return;
  }
  if (!capability_) {
    // Report success, even though a connection will not succeed until a Modem
    // is instantiated and |cabability_| is created. Setting |capability_state_|
    // to kCellularStarted here will cause CreateCapability to call StartModem.
    SetCapabilityState(CapabilityState::kCellularStarted);
    LOG(WARNING) << __func__ << ": Skipping Start (no capability).";
    if (error)
      error->Reset();
    return;
  }

  StartModem(error, callback);
}

void Cellular::Stop(Error* error, const EnabledStateChangedCallback& callback) {
  SLOG(this, 1) << __func__ << ": " << GetStateString(state_);
  if (capability_) {
    StopModem(error, callback);
  } else {
    // Modem is inhibited. Invoke the callback with no error to persist the
    // disabled state.
    SetCapabilityState(CapabilityState::kCellularStopped);
    callback.Run(Error());
  }

  // Sockets should be destroyed here to ensure we make new connections
  // when we next enable cellular. Since the carrier may assign us a new IP
  // on reconnection and some carriers don't like when packets are sent from
  // this device using the old IP, we need to make sure we prevent further
  // packets from going out.
  if (manager() && manager()->device_info() && socket_destroyer_) {
    StopIPv6();

    for (const auto& address :
         manager()->device_info()->GetAddresses(interface_index())) {
      rtnl_handler()->RemoveInterfaceAddress(interface_index(), address);
      socket_destroyer_->DestroySockets(IPPROTO_TCP, address);
    }
  }
}

bool Cellular::IsUnderlyingDeviceEnabled() const {
  return IsEnabledModemState(modem_state_);
}

// static
bool Cellular::IsEnabledModemState(ModemState state) {
  switch (state) {
    case kModemStateFailed:
    case kModemStateUnknown:
    case kModemStateDisabled:
    case kModemStateInitializing:
    case kModemStateLocked:
    case kModemStateDisabling:
    case kModemStateEnabling:
      return false;
    case kModemStateEnabled:
    case kModemStateSearching:
    case kModemStateRegistered:
    case kModemStateDisconnecting:
    case kModemStateConnecting:
    case kModemStateConnected:
      return true;
  }
  return false;
}

void Cellular::StartModem(Error* error,
                          const EnabledStateChangedCallback& callback) {
  DCHECK(capability_);
  SLOG(this, 1) << __func__;
  SetCapabilityState(CapabilityState::kModemStarting);
  capability_->StartModem(error,
                          base::Bind(&Cellular::StartModemCallback,
                                     weak_ptr_factory_.GetWeakPtr(), callback));
}

void Cellular::StartModemCallback(const EnabledStateChangedCallback& callback,
                                  const Error& error) {
  SLOG(this, 1) << __func__ << ": " << GetStateString(state_);
  SetCapabilityState(CapabilityState::kModemStarted);

  if (inhibited_) {
    inhibited_ = false;
    adaptor()->EmitBoolChanged(kInhibitedProperty, inhibited_);
  }

  if (!error.IsSuccess()) {
    if (callback)
      callback.Run(error);
    return;
  }

  if (state_ == kStateDisabled) {
    SetState(kStateEnabled);
    // Registration state updates may have been ignored while the
    // modem was not yet marked enabled.
    HandleNewRegistrationState();
  }

  // Request Device property for setting uid_.
  std::unique_ptr<DBusPropertiesProxy> dbus_properties_proxy =
      control_interface()->CreateDBusPropertiesProxy(dbus_path_, dbus_service_);
  dbus_properties_proxy->GetAsync(
      modemmanager::kModemManager1ModemInterface, MM_MODEM_PROPERTY_DEVICE,
      base::Bind(&Cellular::StartModemGetDeviceCallback,
                 weak_ptr_factory_.GetWeakPtr(), callback),
      base::Bind(
          [](const EnabledStateChangedCallback& callback, const Error& error) {
            LOG(ERROR) << "Error geeting Device property from Modem: " << error;
            if (callback)
              callback.Run(Error(Error::kOperationFailed));
          },
          callback));
}

void Cellular::StartModemGetDeviceCallback(
    const EnabledStateChangedCallback& callback, const brillo::Any& device) {
  if (!device.IsEmpty())
    uid_ = device.Get<std::string>();

  if (callback)
    callback.Run(Error());
}

void Cellular::StopModem(Error* error,
                         const EnabledStateChangedCallback& callback) {
  DCHECK(capability_);
  SLOG(this, 1) << __func__;
  SetCapabilityState(CapabilityState::kModemStopping);
  capability_->StopModem(error,
                         base::Bind(&Cellular::StopModemCallback,
                                    weak_ptr_factory_.GetWeakPtr(), callback));
}

void Cellular::StopModemCallback(const EnabledStateChangedCallback& callback,
                                 const Error& error) {
  SLOG(this, 1) << __func__ << ": " << GetStateString(state_);
  SetCapabilityState(CapabilityState::kCellularStopped);
  // Destroy the cellular service regardless of any errors that occur during
  // the stop process since we do not know the state of the modem at this
  // point.
  DestroyService();
  if (state_ != kStateDisabled)
    SetState(kStateDisabled);
  callback.Run(error);
  // In case no termination action was executed (and TerminationActionComplete
  // was not invoked) in response to a suspend request, any registered
  // termination action needs to be removed explicitly.
  manager()->RemoveTerminationAction(link_name());
}

void Cellular::CompleteActivation(Error* error) {
  if (capability_)
    capability_->CompleteActivation(error);
}

void Cellular::RegisterOnNetwork(const string& network_id,
                                 Error* error,
                                 const ResultCallback& callback) {
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->RegisterOnNetwork(network_id, error, callback);
}

void Cellular::RequirePin(const string& pin,
                          bool require,
                          Error* error,
                          const ResultCallback& callback) {
  SLOG(this, 2) << __func__ << "(" << require << ")";
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->RequirePin(pin, require, error, callback);
}

void Cellular::EnterPin(const string& pin,
                        Error* error,
                        const ResultCallback& callback) {
  SLOG(this, 2) << __func__;
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->EnterPin(pin, error, callback);
}

void Cellular::UnblockPin(const string& unblock_code,
                          const string& pin,
                          Error* error,
                          const ResultCallback& callback) {
  SLOG(this, 2) << __func__;
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->UnblockPin(unblock_code, pin, error, callback);
}

void Cellular::ChangePin(const string& old_pin,
                         const string& new_pin,
                         Error* error,
                         const ResultCallback& callback) {
  SLOG(this, 2) << __func__;
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->ChangePin(old_pin, new_pin, error, callback);
}

void Cellular::Reset(Error* error, const ResultCallback& callback) {
  SLOG(this, 2) << __func__;
  if (!capability_)
    callback.Run(Error(Error::Type::kOperationFailed));
  capability_->Reset(error, callback);
}

void Cellular::DropConnection() {
  if (ppp_device_) {
    // For PPP dongles, IP configuration is handled on the |ppp_device_|,
    // rather than the netdev plumbed into |this|.
    ppp_device_->DropConnection();
  } else {
    Device::DropConnection();
  }
}

void Cellular::SetServiceState(Service::ConnectState state) {
  if (ppp_device_) {
    ppp_device_->SetServiceState(state);
  } else if (selected_service()) {
    Device::SetServiceState(state);
  } else if (service_) {
    service_->SetState(state);
  } else {
    LOG(WARNING) << "State change with no Service.";
  }
}

void Cellular::SetServiceFailure(Service::ConnectFailure failure_state) {
  if (ppp_device_) {
    ppp_device_->SetServiceFailure(failure_state);
  } else if (selected_service()) {
    Device::SetServiceFailure(failure_state);
  } else if (service_) {
    service_->SetFailure(failure_state);
  } else {
    LOG(WARNING) << "State change with no Service.";
  }
}

void Cellular::SetServiceFailureSilent(Service::ConnectFailure failure_state) {
  if (ppp_device_) {
    ppp_device_->SetServiceFailureSilent(failure_state);
  } else if (selected_service()) {
    Device::SetServiceFailureSilent(failure_state);
  } else if (service_) {
    service_->SetFailureSilent(failure_state);
  } else {
    LOG(WARNING) << "State change with no Service.";
  }
}

void Cellular::OnBeforeSuspend(const ResultCallback& callback) {
  LOG(INFO) << __func__;
  Error error;
  StopPPP();
  SetEnabledNonPersistent(false, &error, callback);
  if (error.IsFailure() && error.type() != Error::kInProgress) {
    // If we fail to disable the modem right away, proceed instead of wasting
    // the time to wait for the suspend/termination delay to expire.
    LOG(WARNING) << "Proceed with suspend/termination even though the modem "
                 << "is not yet disabled: " << error;
    callback.Run(error);
  }
}

void Cellular::OnAfterResume() {
  SLOG(this, 2) << __func__;
  if (enabled_persistent()) {
    LOG(INFO) << "Restarting modem after resume.";

    // If we started disabling the modem before suspend, but that
    // suspend is still in progress, then we are not yet in
    // kStateDisabled. That's a problem, because Cellular::Start
    // returns immediately in that case. Hack around that by forcing
    // |state_| here.
    //
    // TODO(quiche): Remove this hack. Maybe
    // CellularCapability3gpp should generate separate
    // notifications for Stop_Disable, and Stop_PowerDown. Then we'd
    // update our state to kStateDisabled when Stop_Disable completes.
    state_ = kStateDisabled;

    Error error;
    SetEnabledUnchecked(true, &error, Bind(LogRestartModemResult));
    if (error.IsSuccess()) {
      LOG(INFO) << "Modem restart completed immediately.";
    } else if (error.IsOngoing()) {
      LOG(INFO) << "Modem restart in progress.";
    } else {
      LOG(WARNING) << "Modem restart failed: " << error;
    }
  }

  // Re-enable IPv6 so we can renegotiate an IP address.
  StartIPv6();

  // TODO(quiche): Consider if this should be conditional. If, e.g.,
  // the device was still disabling when we suspended, will trying to
  // renew DHCP here cause problems?
  Device::OnAfterResume();
}

void Cellular::Scan(Error* error, const string& /*reason*/) {
  SLOG(this, 2) << __func__;
  CHECK(error);
  if (proposed_scan_in_progress_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Already scanning");
    return;
  }

  if (!capability_)
    return;

  ResultStringmapsCallback cb =
      Bind(&Cellular::OnScanReply, weak_ptr_factory_.GetWeakPtr());
  capability_->Scan(error, cb);
  // An immediate failure in |cabapility_->Scan(...)| is indicated through the
  // |error| argument.
  if (error->IsFailure())
    return;

  proposed_scan_in_progress_ = true;
  UpdateScanning();
}

void Cellular::OnScanReply(const Stringmaps& found_networks,
                           const Error& error) {
  proposed_scan_in_progress_ = false;
  UpdateScanning();

  // TODO(jglasgow): fix error handling.
  // At present, there is no way of notifying user of this asynchronous error.
  if (error.IsFailure()) {
    clear_found_networks();
    return;
  }

  set_found_networks(found_networks);
}

// Called from an asyc D-Bus function
// Relies on location handler to fetch relevant value from map
void Cellular::GetLocationCallback(const string& gpp_lac_ci_string,
                                   const Error& error) {
  // Expects string of form "MCC,MNC,LAC,CI"
  SLOG(this, 2) << __func__ << ": " << gpp_lac_ci_string;
  vector<string> location_vec = SplitString(
      gpp_lac_ci_string, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (location_vec.size() < 4) {
    LOG(ERROR) << "Unable to parse location string " << gpp_lac_ci_string;
    return;
  }
  location_info_.mcc = location_vec[0];
  location_info_.mnc = location_vec[1];
  location_info_.lac = location_vec[2];
  location_info_.ci = location_vec[3];

  // Alert manager that location has been updated.
  manager()->OnDeviceGeolocationInfoUpdated(this);
}

void Cellular::PollLocationTask() {
  SLOG(this, 4) << __func__;

  PollLocation();

  dispatcher()->PostDelayedTask(FROM_HERE, poll_location_task_.callback(),
                                kPollLocationIntervalMilliseconds);
}

void Cellular::PollLocation() {
  if (!capability_)
    return;
  StringCallback cb =
      Bind(&Cellular::GetLocationCallback, weak_ptr_factory_.GetWeakPtr());
  capability_->GetLocation(cb);
}

void Cellular::HandleNewRegistrationState() {
  SLOG(this, 2) << __func__ << ": (new state " << GetStateString(state_) << ")";
  CHECK(capability_);
  if (!capability_->IsRegistered()) {
    if (!explicit_disconnect_ &&
        capability_state_ != CapabilityState::kModemStopping &&
        (state_ == kStateLinked || state_ == kStateConnected) &&
        service_.get()) {
      metrics()->NotifyCellularDeviceDrop(
          capability_->GetNetworkTechnologyString(), service_->strength());
    }
    if (state_ == kStateLinked || state_ == kStateConnected ||
        state_ == kStateRegistered) {
      SetState(kStateEnabled);
    }
    StopLocationPolling();
    return;
  }
  // In Disabled state, defer creating a service until fully
  // enabled. UI will ignore the appearance of a new service
  // on a disabled device.
  if (state_ == kStateDisabled) {
    return;
  }
  if (state_ == kStateEnabled) {
    SetState(kStateRegistered);

    // Once modem enters registered state, begin polling location:
    // registered means we've successfully connected
    StartLocationPolling();
  }
  if (!service_) {
    metrics()->NotifyDeviceScanFinished(interface_index());
    CreateService();
  }
  if (state_ == kStateRegistered && modem_state_ == kModemStateConnected)
    OnConnected();
  service_->SetNetworkTechnology(capability_->GetNetworkTechnologyString());
  service_->SetRoamingState(capability_->GetRoamingStateString());
  manager()->UpdateService(service_);
}

void Cellular::HandleNewSignalQuality(uint32_t strength) {
  SLOG(this, 2) << "Signal strength: " << strength;
  if (service_) {
    service_->SetStrength(strength);
  }
}

void Cellular::CreateService() {
  SLOG(this, 2) << __func__;
  if (service_) {
    LOG(ERROR) << "Service already exists.";
    return;
  }

  CHECK(capability_);
  DCHECK(manager()->cellular_service_provider());
  service_ =
      manager()->cellular_service_provider()->LoadServicesForDevice(this);
  capability_->OnServiceCreated();

  // We might have missed a property update because the service wasn't created
  // ealier.
  UpdateScanning();
  OnOperatorChanged();
}

void Cellular::DestroyService() {
  SLOG(this, 2) << __func__;
  DropConnection();
  if (service_) {
    DCHECK(manager()->cellular_service_provider());
    manager()->cellular_service_provider()->RemoveServicesForDevice(this);
    service_ = nullptr;
  }
}

void Cellular::CreateCapability(ModemInfo* modem_info) {
  SLOG(this, 1) << __func__;
  CHECK(!capability_);
  capability_ = CellularCapability::Create(type_, this, modem_info);

  // If Cellular::Start has not been called, or Cellular::Stop has been called,
  // we still want to create the capability, but not call StartModem.
  if (capability_state_ == CapabilityState::kModemStopping ||
      capability_state_ == CapabilityState::kCellularStopped) {
    return;
  }

  StartModem(/*error=*/nullptr, EnabledStateChangedCallback());
}

void Cellular::DestroyCapability() {
  capability_.reset();
  modem_state_ = kModemStateUnknown;
  if (capability_state_ == CapabilityState::kModemStopping ||
      capability_state_ == CapabilityState::kCellularStopped) {
    // If Cellular::StopModem has been called, nothing more to do.
    return;
  }
  // Clear any modem starting/started/stopped state by resetting the capability
  // state to kCellularStarted.
  SetCapabilityState(CapabilityState::kCellularStarted);
}

void Cellular::Connect(Error* error) {
  SLOG(this, 2) << __func__;
  if (state_ == kStateConnected || state_ == kStateLinked) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kAlreadyConnected,
                          "Already connected; connection request ignored.");
    return;
  } else if (state_ != kStateRegistered) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotRegistered,
                          "Modem not registered; connection request ignored.");
    return;
  }

  if (!capability_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Modem not available.");
    return;
  }

  if (!IsRoamingAllowedOrRequired() &&
      service_->roaming_state() == kRoamingStateRoaming) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotOnHomeNetwork,
                          "Roaming disallowed; connection request ignored.");
    return;
  }

  KeyValueStore properties;
  capability_->SetupConnectProperties(&properties);
  ResultCallback cb =
      Bind(&Cellular::OnConnectReply, weak_ptr_factory_.GetWeakPtr());
  OnConnecting();
  capability_->Connect(properties, error, cb);
  if (!error->IsSuccess())
    return;

  bool is_auto_connecting = service_.get() && service_->is_auto_connecting();
  metrics()->NotifyDeviceConnectStarted(interface_index(), is_auto_connecting);
}

// Note that there's no ResultCallback argument to this,
// since Connect() isn't yet passed one.
void Cellular::OnConnectReply(const Error& error) {
  SLOG(this, 2) << __func__ << "(" << error << ")";
  if (error.IsSuccess()) {
    metrics()->NotifyDeviceConnectFinished(interface_index());
    OnConnected();
  } else {
    metrics()->NotifyCellularDeviceConnectionFailure();
    OnConnectFailed(error);
  }
}

void Cellular::OnDisabled() {
  SLOG(this, 1) << __func__;
  SetEnabled(false);
}

void Cellular::OnEnabled() {
  SLOG(this, 1) << __func__;
  manager()->AddTerminationAction(
      link_name(),
      Bind(&Cellular::StartTermination, weak_ptr_factory_.GetWeakPtr()));
  SetEnabled(true);
}

void Cellular::OnConnecting() {
  if (service_)
    service_->SetState(Service::kStateAssociating);
}

void Cellular::OnConnected() {
  SLOG(this, 2) << __func__;
  if (state_ == kStateConnected || state_ == kStateLinked) {
    SLOG(this, 2) << "Already connected";
    return;
  }
  SetState(kStateConnected);
  if (!service_) {
    LOG(INFO) << "Disconnecting due to no cellular service.";
    Disconnect(nullptr, "no celluar service");
  } else if (!IsRoamingAllowedOrRequired() &&
             service_->roaming_state() == kRoamingStateRoaming) {
    LOG(INFO) << "Disconnecting due to roaming.";
    Disconnect(nullptr, "roaming");
  } else {
    EstablishLink();
  }
}

void Cellular::OnConnectFailed(const Error& error) {
  if (service_)
    service_->SetFailure(Service::kFailureUnknown);
}

void Cellular::Disconnect(Error* error, const char* reason) {
  SLOG(this, 2) << __func__ << ": " << reason;
  if (state_ != kStateConnected && state_ != kStateLinked) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotConnected,
                          "Not connected; request ignored.");
    return;
  }
  if (!capability_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Modem not available.");
    return;
  }
  StopPPP();
  explicit_disconnect_ = true;
  ResultCallback cb =
      Bind(&Cellular::OnDisconnectReply, weak_ptr_factory_.GetWeakPtr());
  capability_->Disconnect(error, cb);
}

void Cellular::OnDisconnectReply(const Error& error) {
  SLOG(this, 2) << __func__ << "(" << error << ")";
  explicit_disconnect_ = false;
  if (error.IsSuccess()) {
    OnDisconnected();
  } else {
    metrics()->NotifyCellularDeviceDisconnectionFailure();
    OnDisconnectFailed();
  }
}

void Cellular::OnDisconnected() {
  SLOG(this, 2) << __func__;
  if (!DisconnectCleanup()) {
    LOG(WARNING) << "Disconnect occurred while in state "
                 << GetStateString(state_);
  }
}

void Cellular::OnDisconnectFailed() {
  SLOG(this, 2) << __func__;
  // If the modem is in the disconnecting state, then
  // the disconnect should eventually succeed, so do
  // nothing.
  if (modem_state_ == kModemStateDisconnecting) {
    LOG(WARNING) << "Ignoring failed disconnect while modem is disconnecting.";
    return;
  }

  // OnDisconnectFailed got called because no bearers
  // to disconnect were found. Which means that we shouldn't
  // really remain in the connected/linked state if we
  // are in one of those.
  if (!DisconnectCleanup()) {
    // otherwise, no-op
    LOG(WARNING) << "Ignoring failed disconnect while in state "
                 << GetStateString(state_);
  }

  // TODO(armansito): In either case, shill ends up thinking
  // that it's disconnected, while for some reason the underlying
  // modem might still actually be connected. In that case the UI
  // would be reflecting an incorrect state and a further connection
  // request would fail. We should perhaps tear down the modem and
  // restart it here.
}

void Cellular::EstablishLink() {
  SLOG(this, 2) << __func__;
  CHECK_EQ(kStateConnected, state_);
  CHECK(capability_);

  CellularBearer* bearer = capability_->GetActiveBearer();
  if (bearer && bearer->ipv4_config_method() == IPConfig::kMethodPPP) {
    LOG(INFO) << "Start PPP connection on " << bearer->data_interface();
    StartPPP(bearer->data_interface());
    return;
  }

  unsigned int flags = 0;
  if (manager()->device_info()->GetFlags(interface_index(), &flags) &&
      (flags & IFF_UP) != 0) {
    LinkEvent(flags, IFF_UP);
    return;
  }
  // TODO(petkov): Provide a timeout for a failed link-up request.
  rtnl_handler()->SetInterfaceFlags(interface_index(), IFF_UP, IFF_UP);

  // Set state to associating.
  OnConnecting();
}

void Cellular::LinkEvent(unsigned int flags, unsigned int change) {
  Device::LinkEvent(flags, change);
  if (ppp_task_) {
    LOG(INFO) << "Ignoring LinkEvent on device with PPP interface.";
    return;
  }

  if ((flags & IFF_UP) != 0 && state_ == kStateConnected) {
    LOG(INFO) << link_name() << " is up.";
    SetState(kStateLinked);

    // TODO(benchan): IPv6 support is currently disabled for cellular devices.
    // Check and obtain IPv6 configuration from the bearer when we later enable
    // IPv6 support on cellular devices.
    CHECK(capability_);
    CellularBearer* bearer = capability_->GetActiveBearer();
    if (bearer && bearer->ipv4_config_method() == IPConfig::kMethodStatic) {
      SLOG(this, 2) << "Assign static IP configuration from bearer.";
      SelectService(service_);
      SetServiceState(Service::kStateConfiguring);
      // Override the MTU with a given limit for a specific serving operator.
      // TODO(b:138390944): Revisit this override once b:138390944 is resolved.
      IPConfig::Properties properties = *bearer->ipv4_config_properties();
      if (serving_operator_info_ && serving_operator_info_->mtu() != 0) {
        properties.mtu = serving_operator_info_->mtu();
      }
      AssignIPConfig(properties);
      return;
    }

    if (AcquireIPConfig()) {
      SLOG(this, 2) << "Start DHCP to acquire IP configuration.";
      SelectService(service_);
      SetServiceState(Service::kStateConfiguring);
      return;
    }

    LOG(ERROR) << "Unable to acquire IP configuration over DHCP.";
    return;
  }

  if ((flags & IFF_UP) == 0 && state_ == kStateLinked) {
    LOG(INFO) << link_name() << " is down.";
    SetState(kStateConnected);
    DropConnection();
  }
}

void Cellular::OnPropertiesChanged(const string& interface,
                                   const KeyValueStore& changed_properties) {
  CHECK(capability_);
  capability_->OnPropertiesChanged(interface, changed_properties);
}

string Cellular::CreateDefaultFriendlyServiceName() {
  SLOG(this, 2) << __func__;
  return base::StringPrintf("%s_%u", kGenericServiceNamePrefix,
                            friendly_service_name_id_++);
}

bool Cellular::IsDefaultFriendlyServiceName(const string& service_name) const {
  return base::StartsWith(service_name, kGenericServiceNamePrefix,
                          base::CompareCase::SENSITIVE);
}

void Cellular::OnModemStateChanged(ModemState new_state) {
  ModemState old_state = modem_state_;
  if (old_state == new_state) {
    SLOG(this, 3) << "The new state matches the old state. Nothing to do.";
    return;
  }

  CHECK(capability_);
  SLOG(this, 1) << __func__ << ": " << GetModemStateString(old_state) << " -> "
                << GetModemStateString(new_state);
  set_modem_state(new_state);

  // Skip calls to OnDisabled|Enabled|Connected|Disconnected while the
  // capability is starting or stopping the modem since ModemState transitions
  // may be invalid while in those states.
  if (capability_state_ == CapabilityState::kModemStarting) {
    SLOG(this, 2) << "Modem state change while capability starting, "
                  << " ModemState: " << GetModemStateString(new_state);
    UpdateScanning();
    return;
  }
  if (capability_state_ == CapabilityState::kModemStopping) {
    SLOG(this, 2) << "Modem state change while capability stopping, "
                  << " ModemState: " << GetModemStateString(new_state);
    UpdateScanning();
    return;
  }

  if (old_state >= kModemStateRegistered && new_state < kModemStateRegistered) {
    capability_->SetUnregistered(new_state == kModemStateSearching);
    HandleNewRegistrationState();
  }

  if (new_state == kModemStateDisabled) {
    OnDisabled();
  } else if (new_state >= kModemStateEnabled) {
    if (old_state < kModemStateEnabled) {
      // Just became enabled, update enabled state.
      OnEnabled();
    }
    if (new_state == kModemStateEnabled || new_state == kModemStateSearching ||
        new_state == kModemStateRegistered) {
      if (old_state == kModemStateConnected ||
          old_state == kModemStateConnecting ||
          old_state == kModemStateDisconnecting) {
        OnDisconnected();
      }
    } else if (new_state == kModemStateConnecting) {
      OnConnecting();
    } else if (new_state == kModemStateConnected) {
      if (old_state == kModemStateConnecting)
        OnConnected();
    }
  }

  // Update the kScanningProperty property after we've handled the current state
  // update completely.
  UpdateScanning();
}

bool Cellular::IsActivating() const {
  return capability_ && capability_->IsActivating();
}

bool Cellular::IsRoamingAllowedOrRequired() const {
  return allow_roaming_ || provider_requires_roaming_;
}

bool Cellular::GetAllowRoaming(Error* /*error*/) {
  return allow_roaming_;
}

bool Cellular::SetAllowRoaming(const bool& value, Error* error) {
  SLOG(this, 2) << __func__ << "(" << allow_roaming_ << "->" << value << ")";
  if (allow_roaming_ == value) {
    return false;
  }

  if (!capability_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Modem not available.");
    return false;
  }

  allow_roaming_ = value;
  manager()->UpdateDevice(this);

  // Use IsRoamingAllowedOrRequired() instead of |allow_roaming_| in order to
  // incorporate provider preferences when evaluating if a disconnect is
  // required.
  if (!IsRoamingAllowedOrRequired() &&
      capability_->GetRoamingStateString() == kRoamingStateRoaming) {
    Error error;
    Disconnect(&error, __func__);
  }
  adaptor()->EmitBoolChanged(kCellularAllowRoamingProperty, value);
  return true;
}

bool Cellular::SetUseAttachApn(const bool& value, Error* error) {
  SLOG(this, 2) << __func__ << "(" << use_attach_apn_ << "->" << value << ")";
  if (use_attach_apn_ == value) {
    return false;
  }

  use_attach_apn_ = value;

  if (capability_) {
    // Re-creating the service will set again the attach APN
    // and eventually re-attach if needed.
    DestroyService();
    CreateService();
  }

  adaptor()->EmitBoolChanged(kUseAttachAPNProperty, value);
  return true;
}

bool Cellular::GetInhibited(Error* error) {
  return inhibited_;
}

bool Cellular::SetInhibited(const bool& inhibited, Error* error) {
  if (inhibited == inhibited_)
    return false;

  if (!mm1_proxy_)
    return false;

  mm1_proxy_->InhibitDevice(
      uid_, inhibited,
      base::Bind(&Cellular::OnInhibitDevice, weak_ptr_factory_.GetWeakPtr(),
                 inhibited));
  return true;
}

void Cellular::OnInhibitDevice(bool inhibited, const Error& error) {
  if (!error.IsSuccess()) {
    LOG(ERROR) << __func__ << " Failed: " << error;
    return;
  }
  LOG(INFO) << __func__ << " Succeeded. Inhibited= " << inhibited;
  inhibited_ = inhibited;
  adaptor()->EmitBoolChanged(kInhibitedProperty, inhibited_);
}

KeyValueStore Cellular::GetSimLockStatus(Error* error) {
  if (!capability_) {
    // modemmanager might be inhibited or restarting.
    LOG(ERROR) << __func__ << " called with null capability.";
    return KeyValueStore();
  }
  return capability_->SimLockStatusToProperty(error);
}

void Cellular::StartTermination() {
  SLOG(this, 2) << __func__;
  OnBeforeSuspend(
      Bind(&Cellular::OnTerminationCompleted, weak_ptr_factory_.GetWeakPtr()));
}

void Cellular::OnTerminationCompleted(const Error& error) {
  LOG(INFO) << __func__ << ": " << error;
  manager()->TerminationActionComplete(link_name());
  manager()->RemoveTerminationAction(link_name());
}

bool Cellular::DisconnectCleanup() {
  if (state_ != kStateConnected && state_ != kStateLinked)
    return false;
  SetState(kStateRegistered);
  SetServiceFailureSilent(Service::kFailureNone);
  DestroyIPConfig();
  return true;
}

// static
void Cellular::LogRestartModemResult(const Error& error) {
  if (error.IsSuccess()) {
    LOG(INFO) << "Modem restart completed.";
  } else {
    LOG(WARNING) << "Attempt to restart modem failed: " << error;
  }
}

void Cellular::StartPPP(const string& serial_device) {
  SLOG(PPP, this, 2) << __func__ << " on " << serial_device;
  // Detach any SelectedService from this device. It will be grafted onto
  // the PPPDevice after PPP is up (in Cellular::Notify).
  //
  // This has two important effects: 1) kills dhcpcd if it is running.
  // 2) stops Cellular::LinkEvent from driving changes to the
  // SelectedService.
  if (selected_service()) {
    CHECK_EQ(service_.get(), selected_service().get());
    // Save and restore |service_| state, as DropConnection calls
    // SelectService, and SelectService will move selected_service()
    // to kStateIdle.
    Service::ConnectState original_state(service_->state());
    Device::DropConnection();  // Don't redirect to PPPDevice.
    service_->SetState(original_state);
  } else {
    CHECK(!ipconfig());  // Shouldn't have ipconfig without selected_service().
  }

  PPPDaemon::DeathCallback death_callback(
      Bind(&Cellular::OnPPPDied, weak_ptr_factory_.GetWeakPtr()));

  PPPDaemon::Options options;
  options.no_detach = true;
  options.no_default_route = true;
  options.use_peer_dns = true;
  options.max_fail = 1;

  is_ppp_authenticating_ = false;

  Error error;
  std::unique_ptr<ExternalTask> new_ppp_task(PPPDaemon::Start(
      control_interface(), process_manager_, weak_ptr_factory_.GetWeakPtr(),
      options, serial_device, death_callback, &error));
  if (new_ppp_task) {
    LOG(INFO) << "Forked pppd process.";
    ppp_task_ = std::move(new_ppp_task);
  }
}

void Cellular::StopPPP() {
  SLOG(PPP, this, 2) << __func__;
  if (!ppp_device_)
    return;
  DropConnection();
  ppp_task_.reset();
  ppp_device_ = nullptr;
}

// called by |ppp_task_|
void Cellular::GetLogin(string* user, string* password) {
  SLOG(PPP, this, 2) << __func__;
  if (!service()) {
    LOG(ERROR) << __func__ << " with no service ";
    return;
  }
  CHECK(user);
  CHECK(password);
  *user = service()->ppp_username();
  *password = service()->ppp_password();
}

// Called by |ppp_task_|.
void Cellular::Notify(const string& reason, const map<string, string>& dict) {
  SLOG(PPP, this, 2) << __func__ << " " << reason << " on " << link_name();

  if (reason == kPPPReasonAuthenticating) {
    OnPPPAuthenticating();
  } else if (reason == kPPPReasonAuthenticated) {
    OnPPPAuthenticated();
  } else if (reason == kPPPReasonConnect) {
    OnPPPConnected(dict);
  } else if (reason == kPPPReasonDisconnect) {
    // Ignore; we get disconnect information when pppd exits.
  } else {
    NOTREACHED();
  }
}

void Cellular::OnPPPAuthenticated() {
  SLOG(PPP, this, 2) << __func__;
  is_ppp_authenticating_ = false;
}

void Cellular::OnPPPAuthenticating() {
  SLOG(PPP, this, 2) << __func__;
  is_ppp_authenticating_ = true;
}

void Cellular::OnPPPConnected(const map<string, string>& params) {
  SLOG(PPP, this, 2) << __func__;
  string interface_name = PPPDevice::GetInterfaceName(params);
  DeviceInfo* device_info = manager()->device_info();
  int interface_index = device_info->GetIndex(interface_name);
  if (interface_index < 0) {
    // TODO(quiche): Consider handling the race when the RTNL notification about
    // the new PPP device has not been received yet. crbug.com/246832.
    NOTIMPLEMENTED() << ": No device info for " << interface_name << ".";
    return;
  }

  if (!ppp_device_ || ppp_device_->interface_index() != interface_index) {
    if (ppp_device_) {
      ppp_device_->SelectService(nullptr);  // No longer drives |service_|.
      // Destroy the existing device before creating a new one to avoid the
      // possibility of multiple DBus Objects with the same interface name.
      // See https://crbug.com/1032030 for details.
      ppp_device_ = nullptr;
    }
    ppp_device_ = ppp_device_factory_->CreatePPPDevice(
        manager(), interface_name, interface_index);
    device_info->RegisterDevice(ppp_device_);
  }

  CHECK(service_);
  // For PPP, we only SelectService on the |ppp_device_|.
  CHECK(!selected_service());
  ppp_device_->SetEnabled(true);
  ppp_device_->SelectService(service_);
  ppp_device_->UpdateIPConfigFromPPP(params, false /* blackhole_ipv6 */);
}

void Cellular::OnPPPDied(pid_t pid, int exit) {
  LOG(INFO) << __func__ << " on " << link_name();
  ppp_task_.reset();
  if (is_ppp_authenticating_) {
    SetServiceFailure(Service::kFailurePPPAuth);
  } else {
    SetServiceFailure(PPPDevice::ExitStatusToFailure(exit));
  }
  Error error;
  Disconnect(&error, __func__);
}

void Cellular::UpdateScanning() {
  if (proposed_scan_in_progress_) {
    set_scanning(true);
    return;
  }

  if (modem_state_ == kModemStateEnabling) {
    set_scanning(true);
    return;
  }

  if (service_ && service_->activation_state() != kActivationStateActivated) {
    set_scanning(false);
    return;
  }

  if (modem_state_ == kModemStateEnabled ||
      modem_state_ == kModemStateSearching) {
    set_scanning(true);
    return;
  }

  set_scanning(false);
}

void Cellular::RegisterProperties() {
  PropertyStore* store = this->mutable_store();

  // These properties do not have setters, and events are not generated when
  // they are changed.
  store->RegisterConstString(kDBusServiceProperty, &dbus_service_);
  store->RegisterConstString(kDBusObjectProperty, &dbus_path_str_);

  store->RegisterUint16(kScanIntervalProperty, &scan_interval_);

  // These properties have setters that should be used to change their values.
  // Events are generated whenever the values change.
  store->RegisterConstStringmap(kHomeProviderProperty, &home_provider_);
  store->RegisterConstBool(kSupportNetworkScanProperty, &scanning_supported_);
  store->RegisterConstString(kEidProperty, &eid_);
  store->RegisterConstString(kEsnProperty, &esn_);
  store->RegisterConstString(kFirmwareRevisionProperty, &firmware_revision_);
  store->RegisterConstString(kHardwareRevisionProperty, &hardware_revision_);
  store->RegisterConstString(kImeiProperty, &imei_);
  store->RegisterConstString(kImsiProperty, &imsi_);
  store->RegisterConstString(kMdnProperty, &mdn_);
  store->RegisterConstString(kMeidProperty, &meid_);
  store->RegisterConstString(kMinProperty, &min_);
  store->RegisterConstString(kManufacturerProperty, &manufacturer_);
  store->RegisterConstString(kModelIdProperty, &model_id_);
  store->RegisterConstString(kEquipmentIdProperty, &equipment_id_);
  store->RegisterConstBool(kScanningProperty, &scanning_);

  store->RegisterConstString(kSelectedNetworkProperty, &selected_network_);
  store->RegisterConstStringmaps(kFoundNetworksProperty, &found_networks_);
  store->RegisterConstBool(kProviderRequiresRoamingProperty,
                           &provider_requires_roaming_);
  store->RegisterConstBool(kSIMPresentProperty, &sim_present_);
  store->RegisterConstStringmaps(kCellularApnListProperty, &apn_list_);
  store->RegisterConstString(kIccidProperty, &iccid_);

  // TODO(pprabhu): Decide whether these need their own custom setters.
  HelpRegisterConstDerivedString(kTechnologyFamilyProperty,
                                 &Cellular::GetTechnologyFamily);
  HelpRegisterConstDerivedString(kDeviceIdProperty, &Cellular::GetDeviceId);
  HelpRegisterDerivedBool(kCellularAllowRoamingProperty,
                          &Cellular::GetAllowRoaming,
                          &Cellular::SetAllowRoaming);
  HelpRegisterDerivedBool(kUseAttachAPNProperty, &Cellular::GetUseAttachApn,
                          &Cellular::SetUseAttachApn);
  HelpRegisterDerivedBool(kInhibitedProperty, &Cellular::GetInhibited,
                          &Cellular::SetInhibited);

  store->RegisterDerivedKeyValueStore(
      kSIMLockStatusProperty,
      KeyValueStoreAccessor(new CustomAccessor<Cellular, KeyValueStore>(
          this, &Cellular::GetSimLockStatus, /*error=*/nullptr)));
}

void Cellular::UpdateModemProperties(const RpcIdentifier& dbus_path,
                                     const std::string& mac_address) {
  if (dbus_path_ == dbus_path)
    return;
  dbus_path_ = dbus_path;
  dbus_path_str_ = dbus_path.value();
  set_modem_state(kModemStateUnknown);
  set_mac_address(mac_address);
}

const std::string& Cellular::GetSimCardId() const {
  if (!eid_.empty())
    return eid_;
  return iccid_;
}

std::deque<Stringmap> Cellular::BuildApnTryList() const {
  std::deque<Stringmap> apn_try_list;

  if (service_) {
    const Stringmap* apn_info = service_->GetUserSpecifiedApn();
    if (apn_info)
      apn_try_list.push_back(*apn_info);

    apn_info = service_->GetLastGoodApn();
    if (apn_info)
      apn_try_list.push_back(*apn_info);
  }

  apn_try_list.insert(apn_try_list.end(), apn_list_.begin(), apn_list_.end());
  return apn_try_list;
}

void Cellular::set_home_provider(const Stringmap& home_provider) {
  if (home_provider_ == home_provider)
    return;

  home_provider_ = home_provider;
  adaptor()->EmitStringmapChanged(kHomeProviderProperty, home_provider_);
}

void Cellular::set_scanning_supported(bool scanning_supported) {
  if (scanning_supported_ == scanning_supported)
    return;

  scanning_supported_ = scanning_supported;
  if (adaptor())
    adaptor()->EmitBoolChanged(kSupportNetworkScanProperty,
                               scanning_supported_);
  else
    SLOG(this, 2) << "Could not emit signal for property |"
                  << kSupportNetworkScanProperty
                  << "| change. DBus adaptor is NULL!";
}

void Cellular::SetEid(const string& eid) {
  if (eid_ == eid)
    return;

  eid_ = eid;
  adaptor()->EmitStringChanged(kEidProperty, eid_);
}

void Cellular::set_equipment_id(const string& equipment_id) {
  if (equipment_id_ == equipment_id)
    return;

  equipment_id_ = equipment_id;
  adaptor()->EmitStringChanged(kEquipmentIdProperty, equipment_id_);
}

void Cellular::set_esn(const string& esn) {
  if (esn_ == esn)
    return;

  esn_ = esn;
  adaptor()->EmitStringChanged(kEsnProperty, esn_);
}

void Cellular::set_firmware_revision(const string& firmware_revision) {
  if (firmware_revision_ == firmware_revision)
    return;

  firmware_revision_ = firmware_revision;
  adaptor()->EmitStringChanged(kFirmwareRevisionProperty, firmware_revision_);
}

void Cellular::set_hardware_revision(const string& hardware_revision) {
  if (hardware_revision_ == hardware_revision)
    return;

  hardware_revision_ = hardware_revision;
  adaptor()->EmitStringChanged(kHardwareRevisionProperty, hardware_revision_);
}

void Cellular::set_device_id(std::unique_ptr<DeviceId> device_id) {
  device_id_ = std::move(device_id);
}

void Cellular::SetIccid(const string& iccid) {
  if (iccid_ == iccid)
    return;

  iccid_ = iccid;
  adaptor()->EmitStringChanged(kIccidProperty, iccid_);

  home_provider_info()->UpdateICCID(iccid);
  // Provide ICCID to serving operator as well to aid in MVNO identification.
  serving_operator_info()->UpdateICCID(iccid);
}

void Cellular::SetImei(const string& imei) {
  if (imei_ == imei)
    return;

  imei_ = imei;
  adaptor()->EmitStringChanged(kImeiProperty, imei_);
}

void Cellular::SetImsi(const string& imsi) {
  if (imsi_ == imsi)
    return;

  imsi_ = imsi;
  adaptor()->EmitStringChanged(kImsiProperty, imsi_);

  home_provider_info()->UpdateIMSI(imsi);
  // We do not obtain IMSI OTA right now. Provide the value to serving
  // operator as well, to aid in MVNO identification.
  serving_operator_info()->UpdateIMSI(imsi);
}

void Cellular::set_mdn(const string& mdn) {
  if (mdn_ == mdn)
    return;

  mdn_ = mdn;
  adaptor()->EmitStringChanged(kMdnProperty, mdn_);
}

void Cellular::set_meid(const string& meid) {
  if (meid_ == meid)
    return;

  meid_ = meid;
  adaptor()->EmitStringChanged(kMeidProperty, meid_);
}

void Cellular::set_min(const string& min) {
  if (min_ == min)
    return;

  min_ = min;
  adaptor()->EmitStringChanged(kMinProperty, min_);
}

void Cellular::set_manufacturer(const string& manufacturer) {
  if (manufacturer_ == manufacturer)
    return;

  manufacturer_ = manufacturer;
  adaptor()->EmitStringChanged(kManufacturerProperty, manufacturer_);
}

void Cellular::set_model_id(const string& model_id) {
  if (model_id_ == model_id)
    return;

  model_id_ = model_id;
  adaptor()->EmitStringChanged(kModelIdProperty, model_id_);
}

void Cellular::set_mm_plugin(const string& mm_plugin) {
  mm_plugin_ = mm_plugin;
}

void Cellular::StartLocationPolling() {
  CHECK(capability_);
  if (!capability_->IsLocationUpdateSupported()) {
    SLOG(this, 2) << "Location polling not enabled for " << mm_plugin_
                  << " plugin.";
    return;
  }

  if (polling_location_)
    return;

  polling_location_ = true;

  CHECK(poll_location_task_.IsCancelled());
  SLOG(this, 2) << __func__ << ": "
                << "Starting location polling tasks.";
  poll_location_task_.Reset(
      Bind(&Cellular::PollLocationTask, weak_ptr_factory_.GetWeakPtr()));

  // Schedule an immediate task
  dispatcher()->PostTask(FROM_HERE, poll_location_task_.callback());
}

void Cellular::StopLocationPolling() {
  if (!polling_location_)
    return;
  polling_location_ = false;

  if (!poll_location_task_.IsCancelled()) {
    SLOG(this, 2) << __func__ << ": "
                  << "Cancelling outstanding timeout.";
    poll_location_task_.Cancel();
  }
}

void Cellular::set_scanning(bool scanning) {
  if (scanning_ == scanning)
    return;

  scanning_ = scanning;
  adaptor()->EmitBoolChanged(kScanningProperty, scanning_);

  // kScanningProperty is a sticky-false property.
  // Every time it is set to |true|, it will remain |true| up to a maximum of
  // |kScanningTimeout| time, after which it will be reset to |false|.
  if (!scanning_ && !scanning_timeout_callback_.IsCancelled()) {
    SLOG(this, 2) << "Scanning set to false. "
                  << "Cancelling outstanding timeout.";
    scanning_timeout_callback_.Cancel();
  } else {
    CHECK(scanning_timeout_callback_.IsCancelled());
    SLOG(this, 2) << "Scanning set to true. "
                  << "Starting timeout to reset to false.";
    scanning_timeout_callback_.Reset(
        Bind(&Cellular::set_scanning, weak_ptr_factory_.GetWeakPtr(), false));
    dispatcher()->PostDelayedTask(FROM_HERE,
                                  scanning_timeout_callback_.callback(),
                                  scanning_timeout_milliseconds_);
  }
}

void Cellular::set_selected_network(const string& selected_network) {
  if (selected_network_ == selected_network)
    return;

  selected_network_ = selected_network;
  adaptor()->EmitStringChanged(kSelectedNetworkProperty, selected_network_);
}

void Cellular::set_found_networks(const Stringmaps& found_networks) {
  // There is no canonical form of a Stringmaps value.
  // So don't check for redundant updates.
  found_networks_ = found_networks;
  adaptor()->EmitStringmapsChanged(kFoundNetworksProperty, found_networks_);
}

void Cellular::clear_found_networks() {
  if (found_networks_.empty())
    return;

  found_networks_.clear();
  adaptor()->EmitStringmapsChanged(kFoundNetworksProperty, found_networks_);
}

void Cellular::set_provider_requires_roaming(bool provider_requires_roaming) {
  if (provider_requires_roaming_ == provider_requires_roaming)
    return;

  provider_requires_roaming_ = provider_requires_roaming;
  adaptor()->EmitBoolChanged(kProviderRequiresRoamingProperty,
                             provider_requires_roaming_);
}

void Cellular::set_sim_present(bool sim_present) {
  if (sim_present_ == sim_present)
    return;

  sim_present_ = sim_present;
  adaptor()->EmitBoolChanged(kSIMPresentProperty, sim_present_);
}

void Cellular::set_apn_list(const Stringmaps& apn_list) {
  // There is no canonical form of a Stringmaps value.
  // So don't check for redundant updates.
  apn_list_ = apn_list;
  // See crbug.com/215581: Sometimes adaptor may be nullptr when |set_apn_list|
  // is called.
  if (adaptor())
    adaptor()->EmitStringmapsChanged(kCellularApnListProperty, apn_list_);
  else
    SLOG(this, 2) << "Could not emit signal for property |"
                  << kCellularApnListProperty
                  << "| change. DBus adaptor is NULL!";
}

void Cellular::set_home_provider_info(MobileOperatorInfo* home_provider_info) {
  home_provider_info_.reset(home_provider_info);
}

void Cellular::set_serving_operator_info(
    MobileOperatorInfo* serving_operator_info) {
  serving_operator_info_.reset(serving_operator_info);
}

void Cellular::UpdateHomeProvider(const MobileOperatorInfo* operator_info) {
  SLOG(this, 3) << __func__;

  Stringmap home_provider;
  if (!operator_info->sid().empty()) {
    home_provider[kOperatorCodeKey] = operator_info->sid();
  }
  if (!operator_info->nid().empty()) {
    home_provider[kOperatorCodeKey] = operator_info->nid();
  }
  if (!operator_info->mccmnc().empty()) {
    home_provider[kOperatorCodeKey] = operator_info->mccmnc();
  }
  if (!operator_info->operator_name().empty()) {
    home_provider[kOperatorNameKey] = operator_info->operator_name();
  }
  if (!operator_info->country().empty()) {
    home_provider[kOperatorCountryKey] = operator_info->country();
  }
  if (!operator_info->uuid().empty()) {
    home_provider[kOperatorUuidKey] = operator_info->uuid();
  }
  set_home_provider(home_provider);

  ApnList apn_list;
  apn_list.AddApns(capability_->GetProfiles());
  apn_list.AddApns(operator_info->apn_list());
  set_apn_list(apn_list.GetList());

  set_provider_requires_roaming(operator_info->requires_roaming());
}

void Cellular::UpdateServingOperator(
    const MobileOperatorInfo* operator_info,
    const MobileOperatorInfo* home_provider_info) {
  SLOG(this, 3) << __func__;
  if (!service()) {
    return;
  }

  Stringmap serving_operator;
  if (!operator_info->sid().empty()) {
    serving_operator[kOperatorCodeKey] = operator_info->sid();
  }
  if (!operator_info->nid().empty()) {
    serving_operator[kOperatorCodeKey] = operator_info->nid();
  }
  if (!operator_info->mccmnc().empty()) {
    serving_operator[kOperatorCodeKey] = operator_info->mccmnc();
  }
  if (!operator_info->operator_name().empty()) {
    serving_operator[kOperatorNameKey] = operator_info->operator_name();
  }
  if (!operator_info->country().empty()) {
    serving_operator[kOperatorCountryKey] = operator_info->country();
  }
  if (!operator_info->uuid().empty()) {
    serving_operator[kOperatorUuidKey] = operator_info->uuid();
  }
  service()->SetServingOperator(serving_operator);

  // Set friendly name of service.
  string service_name;
  if (service()->roaming_state() == kRoamingStateHome && home_provider_info &&
      !home_provider_info->operator_name().empty()) {
    // Home and serving operators are the same. Use the name of the home
    // operator as that comes from the subscriber module.
    service_name = home_provider_info->operator_name();
  } else if (!operator_info->operator_name().empty()) {
    // If roaming, try to show "<home-provider> | <serving-operator>", per 3GPP
    // rules (TS 31.102 and annex A of 122.101).
    if (service()->roaming_state() == kRoamingStateRoaming &&
        home_provider_info && !home_provider_info->operator_name().empty() &&
        home_provider_info->operator_name() != operator_info->operator_name()) {
      service_name += home_provider_info->operator_name() + " | ";
    }
    service_name += operator_info->operator_name();
  } else if (!operator_info->mccmnc().empty()) {
    // We could not get a name for the operator, just use the code.
    service_name = "cellular_" + operator_info->mccmnc();
  } else {
    // We do not have any information, so must fallback to default service name.
    // Only assign a new default name if the service doesn't already have one,
    // because we we generate a new name each time.
    service_name = service()->friendly_name();
    if (!IsDefaultFriendlyServiceName(service_name)) {
      service_name = CreateDefaultFriendlyServiceName();
    }
  }
  service()->SetFriendlyName(service_name);
}

vector<GeolocationInfo> Cellular::GetGeolocationObjects() const {
  const string& mcc = location_info_.mcc;
  const string& mnc = location_info_.mnc;
  const string& lac = location_info_.lac;
  const string& cid = location_info_.ci;

  GeolocationInfo geolocation_info;

  if (!(mcc.empty() || mnc.empty() || lac.empty() || cid.empty())) {
    geolocation_info[kGeoMobileCountryCodeProperty] = mcc;
    geolocation_info[kGeoMobileNetworkCodeProperty] = mnc;
    geolocation_info[kGeoLocationAreaCodeProperty] = lac;
    geolocation_info[kGeoCellIdProperty] = cid;
    // kGeoTimingAdvanceProperty currently unused in geolocation API
  }
  // Else we have either an incomplete location, no location yet,
  // or some unsupported location type, so don't return something incorrect.

  return {geolocation_info};
}

void Cellular::OnOperatorChanged() {
  SLOG(this, 3) << __func__;
  CHECK(capability_);

  if (service()) {
    capability_->UpdateServiceOLP();
  }

  const bool home_provider_known =
      home_provider_info_->IsMobileNetworkOperatorKnown();
  const bool serving_operator_known =
      serving_operator_info_->IsMobileNetworkOperatorKnown();

  if (home_provider_known) {
    UpdateHomeProvider(home_provider_info_.get());
  } else if (serving_operator_known) {
    SLOG(this, 2) << "Serving provider proxying in for home provider.";
    UpdateHomeProvider(serving_operator_info_.get());
  }

  if (serving_operator_known) {
    if (home_provider_known) {
      UpdateServingOperator(serving_operator_info_.get(),
                            home_provider_info_.get());
    } else {
      UpdateServingOperator(serving_operator_info_.get(), nullptr);
    }
  } else if (home_provider_known) {
    UpdateServingOperator(home_provider_info_.get(), home_provider_info_.get());
  }
}

void Cellular::SetCapabilityState(CapabilityState capability_state) {
  // TODO(stevenjb): Lower this SLOG to 2 once b/172064665 is thoroughly vetted.
  SLOG(this, 1) << __func__ << ": "
                << GetCapabilityStateString(capability_state);
  capability_state_ = capability_state;
}

}  // namespace shill
