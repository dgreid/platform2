// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_service.h"

#include <algorithm>
#include <limits>
#include <string>

#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/certificate_file.h"
#include "shill/control_interface.h"
#include "shill/device.h"
#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/ieee80211.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_endpoint.h"
#include "shill/wifi/wifi_provider.h"

using std::set;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kService;
static string ObjectID(const WiFiService* w) {
  return w->log_name();
}
}  // namespace Logging

namespace {
// Deprecated to migrate from ROT47 to plaintext.
// TODO(crbug.com/1084279) Remove after migration is complete.
const char kStorageDeprecatedPassphrase[] = "Passphrase";
}  // namespace

const char WiFiService::kAutoConnNoEndpoint[] = "no endpoints";
const char WiFiService::kAnyDeviceAddress[] = "any";
const int WiFiService::kSuspectedCredentialFailureThreshold = 3;

const char WiFiService::kStorageCredentialPassphrase[] = "WiFi.Passphrase";
const char WiFiService::kStorageHiddenSSID[] = "WiFi.HiddenSSID";
const char WiFiService::kStorageMode[] = "WiFi.Mode";
const char WiFiService::kStorageSecurityClass[] = "WiFi.SecurityClass";
const char WiFiService::kStorageSSID[] = "SSID";

bool WiFiService::logged_signal_warning = false;

WiFiService::WiFiService(Manager* manager,
                         WiFiProvider* provider,
                         const vector<uint8_t>& ssid,
                         const string& mode,
                         const string& security_class,
                         bool hidden_ssid)
    : Service(manager, Technology::kWifi),
      need_passphrase_(false),
      security_(security_class),
      mode_(mode),
      hidden_ssid_(hidden_ssid),
      frequency_(0),
      physical_mode_(Metrics::kWiFiNetworkPhyModeUndef),
      raw_signal_strength_(0),
      cipher_8021x_(kCryptoNone),
      suspected_credential_failures_(0),
      ssid_(ssid),
      expecting_disconnect_(false),
      certificate_file_(new CertificateFile()),
      provider_(provider) {
  // Must be constructed with a SecurityClass. We only detect (for internal and
  // informational purposes) the specific mode in use later.
  CHECK(IsValidSecurityClass(security_)) << base::StringPrintf(
      "Security \"%s\" is not a SecurityClass", security_.c_str());
  set_log_name("wifi_" + security_ + "_" +
               base::NumberToString(serial_number()));

  PropertyStore* store = this->mutable_store();
  store->RegisterConstString(kModeProperty, &mode_);
  HelpRegisterWriteOnlyDerivedString(kPassphraseProperty,
                                     &WiFiService::SetPassphrase,
                                     &WiFiService::ClearPassphrase, nullptr);
  store->RegisterBool(kPassphraseRequiredProperty, &need_passphrase_);
  HelpRegisterConstDerivedString(kSecurityProperty, &WiFiService::GetSecurity);
  HelpRegisterConstDerivedString(kSecurityClassProperty,
                                 &WiFiService::GetSecurityClass);

  store->RegisterBool(kWifiHiddenSsid, &hidden_ssid_);
  store->RegisterConstUint16(kWifiFrequency, &frequency_);
  store->RegisterConstUint16s(kWifiFrequencyListProperty, &frequency_list_);
  store->RegisterConstUint16(kWifiPhyMode, &physical_mode_);
  store->RegisterConstString(kWifiBSsid, &bssid_);
  store->RegisterConstString(kCountryProperty, &country_code_);
  store->RegisterConstStringmap(kWifiVendorInformationProperty,
                                &vendor_information_);

  hex_ssid_ = base::HexEncode(ssid_.data(), ssid_.size());
  store->RegisterConstString(kWifiHexSsid, &hex_ssid_);

  string ssid_string(reinterpret_cast<const char*>(ssid_.data()), ssid_.size());
  WiFi::SanitizeSSID(&ssid_string);
  set_friendly_name(ssid_string);

  SetEapCredentials(new EapCredentials());

  // TODO(quiche): determine if it is okay to set EAP.KeyManagement for
  // a service that is not 802.1x.
  if (Is8021x()) {
    // Passphrases are not mandatory for 802.1X.
    need_passphrase_ = false;
  } else if (security_ == kSecurityPsk) {
    // TODO(crbug.com/942973): include SAE, once it's validated.
    SetEAPKeyManagement(WPASupplicant::kKeyManagementWPAPSK);
  } else if (security_ == kSecurityWep) {
    SetEAPKeyManagement(WPASupplicant::kKeyModeNone);
  } else if (security_ == kSecurityNone) {
    SetEAPKeyManagement(WPASupplicant::kKeyModeNone);
  } else {
    LOG(ERROR) << "Unsupported security method " << security_;
  }

  // Until we know better (at Profile load time), use the generic name.
  storage_identifier_ = GetDefaultStorageIdentifier();
  UpdateConnectable();
  UpdateSecurity();

  // Now that |this| is a fully constructed WiFiService, synchronize observers
  // with our current state, and emit the appropriate change notifications.
  // (Initial observer state may have been set in our base class.)
  NotifyIfVisibilityChanged();

  IgnoreParameterForConfigure(kModeProperty);
  IgnoreParameterForConfigure(kSSIDProperty);
  IgnoreParameterForConfigure(kSecurityProperty);
  IgnoreParameterForConfigure(kSecurityClassProperty);
  IgnoreParameterForConfigure(kWifiHexSsid);

  InitializeCustomMetrics();

  // Log the |log_name| to |friendly_name| mapping for debugging purposes.
  // The latter will be tagged for scrubbing.
  SLOG(this, 1) << "Constructed WiFi service " << log_name() << ": "
                << WiFi::LogSSID(friendly_name());
}

WiFiService::~WiFiService() = default;

bool WiFiService::IsAutoConnectable(const char** reason) const {
  if (!Service::IsAutoConnectable(reason)) {
    return false;
  }

  // Only auto-connect to Services which have visible Endpoints.
  // (Needed because hidden Services may remain registered with
  // Manager even without visible Endpoints.)
  if (!HasEndpoints()) {
    *reason = kAutoConnNoEndpoint;
    return false;
  }

  CHECK(wifi_) << "We have endpoints but no WiFi device is selected?";

  // Do not preempt an existing connection (whether pending, or
  // connected, and whether to this service, or another).
  if (!wifi_->IsIdle()) {
    *reason = kAutoConnBusy;
    return false;
  }

  return true;
}

std::string WiFiService::GetWiFiPassphrase(Error* error) {
  if (Is8021x() || passphrase_.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Service doesn't have a passphrase.");
    return std::string();
  }

  return passphrase_;
}

void WiFiService::SetEAPKeyManagement(const string& key_management) {
  Service::SetEAPKeyManagement(key_management);
  UpdateSecurity();
}

void WiFiService::AddEndpoint(const WiFiEndpointConstRefPtr& endpoint) {
  DCHECK(endpoint->ssid() == ssid());
  DCHECK(ComputeSecurityClass(endpoint->security_mode()) ==
         ComputeSecurityClass(security_));
  endpoints_.insert(endpoint);
  UpdateFromEndpoints();
}

void WiFiService::RemoveEndpoint(const WiFiEndpointConstRefPtr& endpoint) {
  set<WiFiEndpointConstRefPtr>::iterator i = endpoints_.find(endpoint);
  DCHECK(i != endpoints_.end());
  if (i == endpoints_.end()) {
    LOG(WARNING) << "In " << __func__ << "(): "
                 << "ignoring non-existent endpoint "
                 << endpoint->bssid_string();
    return;
  }
  endpoints_.erase(i);
  if (current_endpoint_ == endpoint) {
    current_endpoint_ = nullptr;
  }
  UpdateFromEndpoints();
}

void WiFiService::NotifyCurrentEndpoint(
    const WiFiEndpointConstRefPtr& endpoint) {
  DCHECK(!endpoint || (endpoints_.find(endpoint) != endpoints_.end()));
  DCHECK(!endpoint || (ComputeSecurityClass(endpoint->security_mode()) ==
                       ComputeSecurityClass(security_)));
  current_endpoint_ = endpoint;
  UpdateFromEndpoints();
}

void WiFiService::NotifyEndpointUpdated(
    const WiFiEndpointConstRefPtr& endpoint) {
  DCHECK(endpoints_.find(endpoint) != endpoints_.end());
  UpdateFromEndpoints();
}

string WiFiService::GetStorageIdentifier() const {
  return storage_identifier_;
}

bool WiFiService::SetPassphrase(const string& passphrase, Error* error) {
  if (security_ == kSecurityWep) {
    ValidateWEPPassphrase(passphrase, error);
  } else if (security_ == kSecurityPsk || security_ == kSecurityWpa ||
             security_ == kSecurityRsn || security_ == kSecurityWpa3) {
    ValidateWPAPassphrase(passphrase, error);
  } else {
    error->Populate(Error::kNotSupported);
  }

  if (!error->IsSuccess()) {
    LOG(ERROR) << "Passphrase could not be set: " << error->message();
    return false;
  }

  return SetPassphraseInternal(passphrase, Service::kReasonPropertyUpdate);
}

bool WiFiService::SetPassphraseInternal(
    const string& passphrase, Service::UpdateCredentialsReason reason) {
  if (passphrase_ == passphrase) {
    // After a user logs in, Chrome may reconfigure a Service with the
    // same credentials as before login. When that occurs, we don't
    // want to bump the user off the network. Hence, we MUST return
    // early. (See crbug.com/231456#c17)
    return false;
  }
  passphrase_ = passphrase;
  OnCredentialChange(reason);
  return true;
}

// ClearPassphrase is separate from SetPassphrase, because the default
// value for |passphrase_| would not pass validation.
void WiFiService::ClearPassphrase(Error* /*error*/) {
  passphrase_.clear();
  ClearCachedCredentials();
  UpdateConnectable();
}

string WiFiService::GetTethering(Error* /*error*/) const {
  if (IsConnected() && wifi_ && wifi_->IsConnectedViaTether()) {
    return kTetheringConfirmedState;
  }

  // Only perform BSSID tests if there is exactly one matching endpoint,
  // so we ignore campuses that may use locally administered BSSIDs.
  if (endpoints_.size() == 1 &&
      (*endpoints_.begin())->has_tethering_signature()) {
    return kTetheringSuspectedState;
  }

  return kTetheringNotDetectedState;
}

string WiFiService::GetLoadableStorageIdentifier(
    const StoreInterface& storage) const {
  set<string> groups = storage.GetGroupsWithProperties(GetStorageProperties());
  if (groups.empty()) {
    LOG(WARNING) << "Configuration for service " << log_name()
                 << " is not available in the persistent store";
    return "";
  }
  if (groups.size() > 1) {
    LOG(WARNING) << "More than one configuration for service " << log_name()
                 << " is available; choosing the first.";
  }
  return *groups.begin();
}

bool WiFiService::IsLoadableFrom(const StoreInterface& storage) const {
  return !storage.GetGroupsWithProperties(GetStorageProperties()).empty();
}

bool WiFiService::IsVisible() const {
  // WiFi Services should be displayed only if they are in range (have
  // endpoints that have shown up in a scan) or if the service is actively
  // being connected.
  return HasEndpoints() || IsConnected() || IsConnecting();
}

bool WiFiService::Load(const StoreInterface* storage) {
  string id = GetLoadableStorageIdentifier(*storage);
  if (id.empty()) {
    return false;
  }

  // Set our storage identifier to match the storage name in the Profile.
  storage_identifier_ = id;

  // Load properties common to all Services.
  if (!Service::Load(storage)) {
    return false;
  }

  // Load properties specific to WiFi services.
  storage->GetBool(id, kStorageHiddenSSID, &hidden_ssid_);

  // NB: mode, security and ssid parameters are never read in from
  // Load() as they are provided from the scan.

  string passphrase;
  if (storage->GetCryptedString(id, kStorageDeprecatedPassphrase,
                                kStorageCredentialPassphrase, &passphrase)) {
    if (SetPassphraseInternal(passphrase, Service::kReasonCredentialsLoaded)) {
      SLOG(this, 3) << "Loaded passphrase in WiFiService::Load.";
    }
  }

  expecting_disconnect_ = false;
  return true;
}

void WiFiService::MigrateDeprecatedStorage(StoreInterface* storage) {
  Service::MigrateDeprecatedStorage(storage);

  const string id = GetStorageIdentifier();
  CHECK(storage->ContainsGroup(id));

  // Deprecated keys that have not been loaded from storage since at least M84.
  // TODO(crbug.com/1120161): Remove code after M89.
  storage->DeleteKey(id, "WiFi.Security");
  storage->DeleteKey(id, "WiFi.FTEnabled");

  // Save the plaintext passphrase in M86+. TODO: Remove code after M89.
  storage->SetString(id, kStorageCredentialPassphrase, passphrase_);

  // M85 key to delete after M89:
  // kStorageDeprecatedPassphrase (crbug.com/1084279)
}

bool WiFiService::Save(StoreInterface* storage) {
  // Save properties common to all Services.
  if (!Service::Save(storage)) {
    return false;
  }

  // Save properties specific to WiFi services.
  // IMPORTANT: Changes must be backwards compatible with the four previous
  // versions. New keys may be added, but existing keys must be preserved.
  // See crbug.com/1120161 and go/rollback-data-restore for details.
  const string id = GetStorageIdentifier();
  storage->SetBool(id, kStorageHiddenSSID, hidden_ssid_);
  storage->SetString(id, kStorageMode, mode_);
  // This saves both the plaintext and rot47 versions of the passphrase.
  // TODO(crbug.com/1084279): Save just the plaintext passphrase after M89.
  storage->SetCryptedString(id, kStorageDeprecatedPassphrase,
                            kStorageCredentialPassphrase, passphrase_);
  storage->SetString(id, kStorageSecurityClass,
                     ComputeSecurityClass(security_));
  storage->SetString(id, kStorageSSID, hex_ssid_);

  return true;
}

bool WiFiService::Unload() {
  // Expect the service to be disconnected if is currently connected or
  // in the process of connecting.
  if (IsConnected() || IsConnecting()) {
    expecting_disconnect_ = true;
  } else {
    expecting_disconnect_ = false;
  }
  Service::Unload();
  if (wifi_) {
    wifi_->DestroyServiceLease(*this);
  }
  hidden_ssid_ = false;
  ResetSuspectedCredentialFailures();
  Error unused_error;
  ClearPassphrase(&unused_error);
  return provider_->OnServiceUnloaded(this);
}

void WiFiService::SetState(ConnectState state) {
  Service::SetState(state);
  NotifyIfVisibilityChanged();
}

bool WiFiService::IsSecurityMatch(const string& security) const {
  return ComputeSecurityClass(security) == ComputeSecurityClass(security_);
}

bool WiFiService::AddSuspectedCredentialFailure() {
  if (!has_ever_connected()) {
    return true;
  }
  ++suspected_credential_failures_;
  return suspected_credential_failures_ >= kSuspectedCredentialFailureThreshold;
}

void WiFiService::ResetSuspectedCredentialFailures() {
  suspected_credential_failures_ = 0;
}

void WiFiService::InitializeCustomMetrics() const {
  SLOG(Metrics, this, 2) << __func__ << " for " << log_name();
  string histogram = metrics()->GetFullMetricName(
      Metrics::kMetricTimeToJoinMillisecondsSuffix, technology());
  metrics()->AddServiceStateTransitionTimer(
      *this, histogram, Service::kStateAssociating, Service::kStateConfiguring);
}

void WiFiService::SendPostReadyStateMetrics(
    int64_t time_resume_to_ready_milliseconds) const {
  metrics()->SendEnumToUMA(
      metrics()->GetFullMetricName(Metrics::kMetricNetworkChannelSuffix,
                                   technology()),
      Metrics::WiFiFrequencyToChannel(frequency_),
      Metrics::kMetricNetworkChannelMax);

  DCHECK(physical_mode_ < Metrics::kWiFiNetworkPhyModeMax);
  metrics()->SendEnumToUMA(
      metrics()->GetFullMetricName(Metrics::kMetricNetworkPhyModeSuffix,
                                   technology()),
      static_cast<Metrics::WiFiNetworkPhyMode>(physical_mode_),
      Metrics::kWiFiNetworkPhyModeMax);

  Metrics::WiFiSecurity security_uma =
      Metrics::WiFiSecurityStringToEnum(security_);
  DCHECK(security_uma != Metrics::kWiFiSecurityUnknown);
  metrics()->SendEnumToUMA(
      metrics()->GetFullMetricName(Metrics::kMetricNetworkSecuritySuffix,
                                   technology()),
      security_uma, Metrics::kMetricNetworkSecurityMax);

  if (Is8021x()) {
    eap()->OutputConnectionMetrics(metrics(), technology());
  }

  // We invert the sign of the signal strength value, since UMA histograms
  // cannot represent negative numbers (it stores them but cannot display
  // them), and dBm values of interest start at 0 and go negative from there.
  metrics()->SendToUMA(
      metrics()->GetFullMetricName(Metrics::kMetricNetworkSignalStrengthSuffix,
                                   technology()),
      -raw_signal_strength_, Metrics::kMetricNetworkSignalStrengthMin,
      Metrics::kMetricNetworkSignalStrengthMax,
      Metrics::kMetricNetworkSignalStrengthNumBuckets);

  if (time_resume_to_ready_milliseconds > 0) {
    metrics()->SendToUMA(
        metrics()->GetFullMetricName(
            Metrics::kMetricTimeResumeToReadyMillisecondsSuffix, technology()),
        time_resume_to_ready_milliseconds,
        Metrics::kTimerHistogramMillisecondsMin,
        Metrics::kTimerHistogramMillisecondsMax,
        Metrics::kTimerHistogramNumBuckets);
  }
}

// private methods
void WiFiService::HelpRegisterConstDerivedString(
    const string& name, string (WiFiService::*get)(Error*)) {
  mutable_store()->RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<WiFiService, string>(this, get, nullptr)));
}

void WiFiService::HelpRegisterDerivedString(
    const string& name,
    string (WiFiService::*get)(Error* error),
    bool (WiFiService::*set)(const string&, Error*)) {
  mutable_store()->RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<WiFiService, string>(this, get, set)));
}

void WiFiService::HelpRegisterWriteOnlyDerivedString(
    const string& name,
    bool (WiFiService::*set)(const string&, Error*),
    void (WiFiService::*clear)(Error* error),
    const string* default_value) {
  mutable_store()->RegisterDerivedString(
      name, StringAccessor(new CustomWriteOnlyAccessor<WiFiService, string>(
                this, set, clear, default_value)));
}

void WiFiService::HelpRegisterDerivedUint16(
    const string& name,
    uint16_t (WiFiService::*get)(Error* error),
    bool (WiFiService::*set)(const uint16_t& value, Error* error),
    void (WiFiService::*clear)(Error* error)) {
  mutable_store()->RegisterDerivedUint16(
      name, Uint16Accessor(new CustomAccessor<WiFiService, uint16_t>(
                this, get, set, clear)));
}

void WiFiService::OnConnect(Error* error) {
  WiFiRefPtr wifi = wifi_;
  if (!wifi) {
    // If this is a hidden service before it has been found in a scan, we
    // may need to late-bind to any available WiFi Device.  We don't actually
    // set |wifi_| in this case since we do not yet see any endpoints.  This
    // will mean this service is not disconnectable until an endpoint is
    // found.
    wifi = ChooseDevice();
    if (!wifi) {
      LOG(ERROR) << "Can't connect to: " << log_name()
                 << ": Cannot find a WiFi device.";
      Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                            Error::GetDefaultMessage(Error::kOperationFailed));
      return;
    }
  }

  if (wifi->IsCurrentService(this)) {
    LOG(WARNING) << "Can't connect to: " << log_name()
                 << ": IsCurrentService, but not connected. State: "
                 << GetStateString();
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          Error::GetDefaultMessage(Error::kInProgress));
    return;
  }

  // Report number of BSSes available for this service.
  metrics()->NotifyWifiAvailableBSSes(endpoints_.size());

  if (Is8021x()) {
    // If EAP key management is not set, set to a default.
    if (GetEAPKeyManagement().empty())
      SetEAPKeyManagement(WPASupplicant::kKeyManagementWPAEAP);
    ClearEAPCertification();
  }

  expecting_disconnect_ = false;
  wifi->ConnectTo(this, error);
}

KeyValueStore WiFiService::GetSupplicantConfigurationParameters() const {
  KeyValueStore params;

  params.Set<uint32_t>(WPASupplicant::kNetworkPropertyMode,
                       WiFiEndpoint::ModeStringToUint(mode_));

  if (Is8021x()) {
    eap()->PopulateSupplicantProperties(certificate_file_.get(), &params);
  } else if (security_ == kSecurityPsk || security_ == kSecurityWpa3 ||
             security_ == kSecurityRsn || security_ == kSecurityWpa) {
    // NB: WPA3-SAE uses RSN protocol.
    const string psk_proto =
        base::StringPrintf("%s %s", WPASupplicant::kSecurityModeWPA,
                           WPASupplicant::kSecurityModeRSN);
    params.Set<string>(WPASupplicant::kPropertySecurityProtocol, psk_proto);
    vector<uint8_t> passphrase_bytes;
    Error error;
    ParseWPAPassphrase(passphrase_, &passphrase_bytes, &error);
    if (!error.IsSuccess()) {
      LOG(ERROR) << "Invalid passphrase";
    } else if (!passphrase_bytes.empty()) {
      params.Set<vector<uint8_t>>(WPASupplicant::kPropertyPreSharedKey,
                                  passphrase_bytes);
    } else {
      params.Set<string>(WPASupplicant::kPropertyPreSharedKey, passphrase_);
    }
  } else if (security_ == kSecurityWep) {
    params.Set<string>(WPASupplicant::kPropertyAuthAlg,
                       WPASupplicant::kSecurityAuthAlg);
    Error unused_error;
    int key_index;
    std::vector<uint8_t> password_bytes;
    ParseWEPPassphrase(passphrase_, &key_index, &password_bytes, &unused_error);
    params.Set<vector<uint8_t>>(
        WPASupplicant::kPropertyWEPKey + base::NumberToString(key_index),
        password_bytes);
    params.Set<uint32_t>(WPASupplicant::kPropertyWEPTxKeyIndex, key_index);
  } else if (security_ == kSecurityNone) {
    // Nothing special to do here.
  } else {
    NOTIMPLEMENTED() << "Unsupported security method " << security_;
  }

  string key_mgmt = key_management();
  if (manager()->GetFTEnabled(nullptr)) {
    if (key_mgmt == WPASupplicant::kKeyManagementWPAPSK)
      key_mgmt =
          base::StringPrintf("%s %s", WPASupplicant::kKeyManagementWPAPSK,
                             WPASupplicant::kKeyManagementFTPSK);
    else if (key_mgmt == WPASupplicant::kKeyManagementWPAEAP)
      key_mgmt =
          base::StringPrintf("%s %s", WPASupplicant::kKeyManagementWPAEAP,
                             WPASupplicant::kKeyManagementFTEAP);
  }
  params.Set<string>(WPASupplicant::kNetworkPropertyEapKeyManagement, key_mgmt);

  // "Enabled" means "negotiate." Let's always do that.
  params.Set<uint32_t>(WPASupplicant::kNetworkPropertyIeee80211w,
                       WPASupplicant::kNetworkIeee80211wEnabled);

  params.Set<vector<uint8_t>>(WPASupplicant::kNetworkPropertySSID, ssid_);

  return params;
}

void WiFiService::OnDisconnect(Error* error, const char* /*reason*/) {
  wifi_->DisconnectFrom(this);
}

bool WiFiService::IsDisconnectable(Error* error) const {
  if (!wifi_) {
    CHECK(!IsConnected())
        << "WiFi device does not exist. Cannot disconnect service "
        << log_name();
    // If we are connecting to a hidden service, but have not yet found
    // any endpoints, we could end up with a disconnect request without
    // a wifi_ reference.
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "WiFi endpoints do not (yet) exist. Cannot disconnect service %s",
            log_name().c_str()));
    return false;
  }
  return wifi_->IsPendingService(this) || wifi_->IsCurrentService(this);
}

RpcIdentifier WiFiService::GetDeviceRpcId(Error* error) const {
  if (!wifi_) {
    error->Populate(Error::kNotFound, "Not associated with a device");
    return control_interface()->NullRpcIdentifier();
  }
  return wifi_->GetRpcIdentifier();
}

void WiFiService::UpdateConnectable() {
  bool is_connectable = false;
  if (security_ == kSecurityNone) {
    DCHECK(passphrase_.empty());
    need_passphrase_ = false;
    is_connectable = true;
  } else if (Is8021x()) {
    is_connectable = Is8021xConnectable();
  } else if (security_ == kSecurityWep || security_ == kSecurityWpa ||
             security_ == kSecurityPsk || security_ == kSecurityRsn ||
             security_ == kSecurityWpa3) {
    need_passphrase_ = passphrase_.empty();
    is_connectable = !need_passphrase_;
  }
  SetConnectable(is_connectable);
}

void WiFiService::UpdateFromEndpoints() {
  const WiFiEndpoint* representative_endpoint = nullptr;

  if (current_endpoint_) {
    representative_endpoint = current_endpoint_.get();
  } else {
    int16_t best_signal = std::numeric_limits<int16_t>::min();
    for (const auto& endpoint : endpoints_) {
      if (endpoint->signal_strength() >= best_signal) {
        best_signal = endpoint->signal_strength();
        representative_endpoint = endpoint.get();
      }
    }
  }

  WiFiRefPtr wifi;
  if (representative_endpoint) {
    wifi = representative_endpoint->device();
    if (((current_endpoint_ == representative_endpoint) &&
         (bssid_ != representative_endpoint->bssid_string() ||
          frequency_ != representative_endpoint->frequency())) ||
        abs(representative_endpoint->signal_strength() - raw_signal_strength_) >
            10) {
      LOG(INFO) << "Rep endpoint updated for " << log_name() << ". "
                << "sig: " << representative_endpoint->signal_strength() << ", "
                << "sec: " << representative_endpoint->security_mode() << ", "
                << "freq: " << representative_endpoint->frequency();
    }
  } else if (IsConnected() || IsConnecting()) {
    LOG(WARNING) << "Service " << log_name()
                 << " will disconnect due to no remaining endpoints.";
  }

  SetWiFi(wifi);

  set<uint16_t> frequency_set;
  for (const auto& endpoint : endpoints_) {
    frequency_set.insert(endpoint->frequency());
  }
  frequency_list_.assign(frequency_set.begin(), frequency_set.end());

  if (Is8021x())
    cipher_8021x_ = ComputeCipher8021x(endpoints_);

  uint16_t frequency = 0;
  int16_t signal = std::numeric_limits<int16_t>::min();
  string bssid;
  string country_code;
  Stringmap vendor_information;
  uint16_t physical_mode = Metrics::kWiFiNetworkPhyModeUndef;
  string security;
  // Represent "unknown raw signal strength" as 0.
  raw_signal_strength_ = 0;
  if (representative_endpoint) {
    frequency = representative_endpoint->frequency();
    signal = representative_endpoint->signal_strength();
    raw_signal_strength_ = signal;
    bssid = representative_endpoint->bssid_string();
    country_code = representative_endpoint->country_code();
    vendor_information = representative_endpoint->GetVendorInformation();
    physical_mode = representative_endpoint->physical_mode();
    security = representative_endpoint->security_mode();
  } else {
    // If all endpoints disappear, reset back to the general Class.
    security = ComputeSecurityClass(security_);
  }
  CHECK(!security.empty());

  if (frequency_ != frequency) {
    frequency_ = frequency;
    adaptor()->EmitUint16Changed(kWifiFrequency, frequency_);
  }
  if (bssid_ != bssid) {
    bssid_ = bssid;
    adaptor()->EmitStringChanged(kWifiBSsid, bssid_);
  }
  if (country_code_ != country_code) {
    country_code_ = country_code;
    adaptor()->EmitStringChanged(kCountryProperty, country_code_);
  }
  if (vendor_information_ != vendor_information) {
    vendor_information_ = vendor_information;
    adaptor()->EmitStringmapChanged(kWifiVendorInformationProperty,
                                    vendor_information_);
  }
  if (physical_mode_ != physical_mode) {
    physical_mode_ = physical_mode;
    adaptor()->EmitUint16Changed(kWifiPhyMode, physical_mode_);
  }
  adaptor()->EmitUint16sChanged(kWifiFrequencyListProperty, frequency_list_);
  SetStrength(SignalToStrength(signal));

  if (security != security_) {
    security_ = security;
  }

  // Either cipher_8021x_ or security_ may have changed. Recomputing is
  // harmless.
  UpdateSecurity();

  NotifyIfVisibilityChanged();
}

void WiFiService::UpdateSecurity() {
  CryptoAlgorithm algorithm = kCryptoNone;
  bool key_rotation = false;
  bool endpoint_auth = false;

  if (security_ == kSecurityNone) {
    // initial values apply
  } else if (security_ == kSecurityWep) {
    algorithm = kCryptoRc4;
    key_rotation = Is8021x();
    endpoint_auth = Is8021x();
  } else if (security_ == kSecurityPsk || security_ == kSecurityWpa) {
    algorithm = kCryptoRc4;
    key_rotation = true;
    endpoint_auth = false;
  } else if (security_ == kSecurityRsn || security_ == kSecurityWpa3) {
    // TODO(crbug.com/942973): weigh WPA3 more highly?
    algorithm = kCryptoAes;
    key_rotation = true;
    endpoint_auth = false;
  } else if (security_ == kSecurity8021x) {
    algorithm = cipher_8021x_;
    key_rotation = true;
    endpoint_auth = true;
  }
  SetSecurity(algorithm, key_rotation, endpoint_auth);
}

// static
Service::CryptoAlgorithm WiFiService::ComputeCipher8021x(
    const set<WiFiEndpointConstRefPtr>& endpoints) {
  if (endpoints.empty())
    return kCryptoNone;  // Will update after scan results.

  // Find weakest cipher (across endpoints) of the strongest ciphers
  // (per endpoint).
  Service::CryptoAlgorithm cipher = Service::kCryptoAes;
  for (const auto& endpoint : endpoints) {
    Service::CryptoAlgorithm endpoint_cipher;
    if (endpoint->has_rsn_property()) {
      endpoint_cipher = Service::kCryptoAes;
    } else if (endpoint->has_wpa_property()) {
      endpoint_cipher = Service::kCryptoRc4;
    } else {
      // We could be in the Dynamic WEP case here. But that's okay,
      // because |cipher_8021x_| is not defined in that case.
      endpoint_cipher = Service::kCryptoNone;
    }
    cipher = std::min(cipher, endpoint_cipher);
  }
  return cipher;
}

// static
void WiFiService::ValidateWEPPassphrase(const std::string& passphrase,
                                        Error* error) {
  ParseWEPPassphrase(passphrase, nullptr, nullptr, error);
}

// static
void WiFiService::ValidateWPAPassphrase(const std::string& passphrase,
                                        Error* error) {
  ParseWPAPassphrase(passphrase, nullptr, error);
}

// static
void WiFiService::ParseWEPPassphrase(const string& passphrase,
                                     int* key_index,
                                     std::vector<uint8_t>* password_bytes,
                                     Error* error) {
  unsigned int length = passphrase.length();
  int key_index_local;
  std::string password_text;
  bool is_hex = false;

  switch (length) {
    case IEEE_80211::kWEP40AsciiLen:
    case IEEE_80211::kWEP104AsciiLen:
      key_index_local = 0;
      password_text = passphrase;
      break;
    case IEEE_80211::kWEP40AsciiLen + 2:
    case IEEE_80211::kWEP104AsciiLen + 2:
      if (CheckWEPKeyIndex(passphrase, error)) {
        base::StringToInt(passphrase.substr(0, 1), &key_index_local);
        password_text = passphrase.substr(2);
      }
      break;
    case IEEE_80211::kWEP40HexLen:
    case IEEE_80211::kWEP104HexLen:
      if (CheckWEPIsHex(passphrase, error)) {
        key_index_local = 0;
        password_text = passphrase;
        is_hex = true;
      }
      break;
    case IEEE_80211::kWEP40HexLen + 2:
    case IEEE_80211::kWEP104HexLen + 2:
      if (CheckWEPKeyIndex(passphrase, error) &&
          CheckWEPIsHex(passphrase.substr(2), error)) {
        base::StringToInt(passphrase.substr(0, 1), &key_index_local);
        password_text = passphrase.substr(2);
        is_hex = true;
      } else if (CheckWEPPrefix(passphrase, error) &&
                 CheckWEPIsHex(passphrase.substr(2), error)) {
        key_index_local = 0;
        password_text = passphrase.substr(2);
        is_hex = true;
      }
      break;
    case IEEE_80211::kWEP40HexLen + 4:
    case IEEE_80211::kWEP104HexLen + 4:
      if (CheckWEPKeyIndex(passphrase, error) &&
          CheckWEPPrefix(passphrase.substr(2), error) &&
          CheckWEPIsHex(passphrase.substr(4), error)) {
        base::StringToInt(passphrase.substr(0, 1), &key_index_local);
        password_text = passphrase.substr(4);
        is_hex = true;
      }
      break;
    default:
      error->Populate(Error::kInvalidPassphrase);
      break;
  }

  if (error->IsSuccess()) {
    if (key_index)
      *key_index = key_index_local;
    if (password_bytes) {
      if (is_hex)
        base::HexStringToBytes(password_text, password_bytes);
      else
        password_bytes->insert(password_bytes->end(), password_text.begin(),
                               password_text.end());
    }
  }
}

// static
void WiFiService::ParseWPAPassphrase(const std::string& passphrase,
                                     vector<uint8_t>* passphrase_bytes,
                                     Error* error) {
  unsigned int length = passphrase.length();
  vector<uint8_t> temp_bytes;

  // ASCII passphrase. No conversions needed.
  if (length >= IEEE_80211::kWPAAsciiMinLen &&
      length <= IEEE_80211::kWPAAsciiMaxLen) {
    return;
  }
  if (length == IEEE_80211::kWPAHexLen &&
      base::HexStringToBytes(passphrase, &temp_bytes)) {
    if (passphrase_bytes) {
      base::HexStringToBytes(passphrase, passphrase_bytes);
    }
    return;
  }
  // None of the above.
  error->Populate(Error::kInvalidPassphrase);
}

// static
bool WiFiService::CheckWEPIsHex(const string& passphrase, Error* error) {
  vector<uint8_t> passphrase_bytes;
  if (base::HexStringToBytes(passphrase, &passphrase_bytes)) {
    return true;
  } else {
    error->Populate(Error::kInvalidPassphrase);
    return false;
  }
}

// static
bool WiFiService::CheckWEPKeyIndex(const string& passphrase, Error* error) {
  const auto kCaseInsensitive = base::CompareCase::INSENSITIVE_ASCII;
  if (base::StartsWith(passphrase, "0:", kCaseInsensitive) ||
      base::StartsWith(passphrase, "1:", kCaseInsensitive) ||
      base::StartsWith(passphrase, "2:", kCaseInsensitive) ||
      base::StartsWith(passphrase, "3:", kCaseInsensitive)) {
    return true;
  } else {
    error->Populate(Error::kInvalidPassphrase);
    return false;
  }
}

// static
bool WiFiService::CheckWEPPrefix(const string& passphrase, Error* error) {
  if (base::StartsWith(passphrase, "0x",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return true;
  } else {
    error->Populate(Error::kInvalidPassphrase);
    return false;
  }
}

// static
string WiFiService::ComputeSecurityClass(const string& security) {
  if (security == kSecurityRsn || security == kSecurityWpa ||
      security == kSecurityWpa3) {
    return kSecurityPsk;
  } else {
    return security;
  }
}

int16_t WiFiService::SignalLevel() const {
  return current_endpoint_ ? current_endpoint_->signal_strength()
                           : std::numeric_limits<int16_t>::min();
}

// static
bool WiFiService::IsValidMode(const string& mode) {
  return mode == kModeManaged;
}

// static
bool WiFiService::IsValidSecurityMethod(const string& method) {
  return method == kSecurityNone || method == kSecurityWep ||
         method == kSecurityPsk || method == kSecurityWpa ||
         method == kSecurityRsn || method == kSecurityWpa3 ||
         method == kSecurity8021x;
}

// static
bool WiFiService::IsValidSecurityClass(const string& security_class) {
  return IsValidSecurityMethod(security_class) &&
         ComputeSecurityClass(security_class) == security_class;
}

// static
uint8_t WiFiService::SignalToStrength(int16_t signal_dbm) {
  int16_t strength;
  if (signal_dbm > 0) {
    if (!logged_signal_warning) {
      LOG(WARNING) << "Signal strength is suspiciously high. "
                   << "Assuming value " << signal_dbm << " is not in dBm.";
      logged_signal_warning = true;
    }
    strength = signal_dbm;
  } else {
    strength = 120 + signal_dbm;  // Call -20dBm "perfect".
  }

  if (strength > kStrengthMax) {
    strength = kStrengthMax;
  } else if (strength < kStrengthMin) {
    strength = kStrengthMin;
  }
  return strength;
}

KeyValueStore WiFiService::GetStorageProperties() const {
  KeyValueStore args;
  args.Set<string>(kStorageType, kTypeWifi);
  args.Set<string>(kStorageSSID, hex_ssid_);
  args.Set<string>(kStorageMode, mode_);
  args.Set<string>(kStorageSecurityClass, ComputeSecurityClass(security_));
  return args;
}

string WiFiService::GetDefaultStorageIdentifier() const {
  string security = ComputeSecurityClass(security_);
  return base::ToLowerASCII(
      base::StringPrintf("%s_%s_%s_%s_%s", kTypeWifi, kAnyDeviceAddress,
                         hex_ssid_.c_str(), mode_.c_str(), security.c_str()));
}

string WiFiService::GetSecurity(Error* /*error*/) {
  return security();
}

string WiFiService::GetSecurityClass(Error* /*error*/) {
  return security_class();
}

void WiFiService::ClearCachedCredentials() {
  if (wifi_) {
    wifi_->ClearCachedCredentials(this);
  }
}

void WiFiService::OnEapCredentialsChanged(
    Service::UpdateCredentialsReason reason) {
  if (Is8021x()) {
    OnCredentialChange(reason);
  }
}

void WiFiService::OnCredentialChange(Service::UpdateCredentialsReason reason) {
  ClearCachedCredentials();
  // Credential changes due to a property update are new and have not
  // necessarily been used for a successful connection.
  if (reason == kReasonPropertyUpdate)
    SetHasEverConnected(false);
  UpdateConnectable();
  ResetSuspectedCredentialFailures();
}

void WiFiService::OnProfileConfigured() {
  if (profile() || !hidden_ssid()) {
    return;
  }
  // This situation occurs when a hidden WiFi service created via GetService
  // has been persisted to a profile in Manager::ConfigureService().  Now
  // that configuration is saved, we must join the service with its profile,
  // which will make this SSID eligible for directed probes during scans.
  manager()->RegisterService(this);
}

bool WiFiService::Is8021x() const {
  if (security_ == kSecurity8021x)
    return true;

  // Dynamic WEP + 802.1x.
  if (security_ == kSecurityWep &&
      GetEAPKeyManagement() == WPASupplicant::kKeyManagementIeee8021X)
    return true;
  return false;
}

WiFiRefPtr WiFiService::ChooseDevice() {
  DeviceRefPtr device =
      manager()->GetEnabledDeviceWithTechnology(Technology::kWifi);
  CHECK(!device || device->technology() == Technology::kWifi)
      << "Unexpected device technology: " << device->technology();
  return static_cast<WiFi*>(device.get());
}

void WiFiService::ResetWiFi() {
  SetWiFi(nullptr);
}

void WiFiService::SetWiFi(const WiFiRefPtr& new_wifi) {
  if (wifi_ == new_wifi) {
    return;
  }
  ClearCachedCredentials();
  if (wifi_) {
    wifi_->DisassociateFromService(this);
  }
  if (new_wifi) {
    adaptor()->EmitRpcIdentifierChanged(kDeviceProperty,
                                        new_wifi->GetRpcIdentifier());
  } else {
    adaptor()->EmitRpcIdentifierChanged(
        kDeviceProperty, control_interface()->NullRpcIdentifier());
  }
  wifi_ = new_wifi;
}

}  // namespace shill
