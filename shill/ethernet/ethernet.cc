// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/ethernet.h"

#include <linux/ethtool.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/ether.h
#include <linux/sockios.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <map>
#include <string>
#include <vector>

#include <base/bind.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/eap_credentials.h"
#include "shill/eap_listener.h"
#include "shill/ethernet/ethernet_eap_provider.h"
#include "shill/ethernet/ethernet_service.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/rtnl_handler.h"
#include "shill/pppoe/pppoe_service.h"
#include "shill/profile.h"
#include "shill/property_accessor.h"
#include "shill/proxy_factory.h"
#include "shill/refptr_types.h"
#include "shill/store_interface.h"
#include "shill/supplicant/supplicant_interface_proxy_interface.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

using std::map;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kEthernet;
static string ObjectID(Ethernet *e) { return e->GetRpcIdentifier(); }
}

Ethernet::Ethernet(ControlInterface *control_interface,
                   EventDispatcher *dispatcher,
                   Metrics *metrics,
                   Manager *manager,
                   const string &link_name,
                   const string &address,
                   int interface_index)
    : Device(control_interface,
             dispatcher,
             metrics,
             manager,
             link_name,
             address,
             interface_index,
             Technology::kEthernet),
      control_interface_(control_interface),
      link_up_(false),
      is_eap_authenticated_(false),
      is_eap_detected_(false),
      eap_listener_(new EapListener(dispatcher, interface_index)),
      proxy_factory_(ProxyFactory::GetInstance()),
      sockets_(new Sockets()),
      weak_ptr_factory_(this) {
  PropertyStore *store = this->mutable_store();
  store->RegisterConstBool(kEapAuthenticationCompletedProperty,
                           &is_eap_authenticated_);
  store->RegisterConstBool(kEapAuthenticatorDetectedProperty,
                           &is_eap_detected_);
  store->RegisterConstBool(kLinkUpProperty, &link_up_);
  store->RegisterDerivedBool(kPPPoEProperty, BoolAccessor(
      new CustomAccessor<Ethernet, bool>(this,
                                         &Ethernet::GetPPPoEMode,
                                         &Ethernet::ConfigurePPPoEMode,
                                         &Ethernet::ClearPPPoEMode)));

  eap_listener_->set_request_received_callback(
      base::Bind(&Ethernet::OnEapDetected, weak_ptr_factory_.GetWeakPtr()));
  service_ = CreateEthernetService();
  SLOG(this, 2) << "Ethernet device " << link_name << " initialized.";
}

Ethernet::~Ethernet() {
}

void Ethernet::Start(Error *error,
                     const EnabledStateChangedCallback &/*callback*/) {
  rtnl_handler()->SetInterfaceFlags(interface_index(), IFF_UP, IFF_UP);
  OnEnabledStateChanged(EnabledStateChangedCallback(), Error());
  LOG(INFO) << "Registering " << link_name() << " with manager.";
  if (!manager()->HasService(service_)) {
    manager()->RegisterService(service_);
  }
  if (error)
    error->Reset();       // indicate immediate completion
}

void Ethernet::Stop(Error *error,
                    const EnabledStateChangedCallback &/*callback*/) {
  manager()->DeregisterService(service_);
  StopSupplicant();
  OnEnabledStateChanged(EnabledStateChangedCallback(), Error());
  if (error)
    error->Reset();       // indicate immediate completion
}

void Ethernet::LinkEvent(unsigned int flags, unsigned int change) {
  Device::LinkEvent(flags, change);
  if ((flags & IFF_LOWER_UP) != 0 && !link_up_) {
    link_up_ = true;
    adaptor()->EmitBoolChanged(kLinkUpProperty, link_up_);
    // We SetupWakeOnLan() here, instead of in Start(), because with
    // r8139, "ethtool -s eth0 wol g" fails when no cable is plugged
    // in.
    manager()->UpdateService(service_);
    service_->OnVisibilityChanged();
    SetupWakeOnLan();
    eap_listener_->Start();
  } else if ((flags & IFF_LOWER_UP) == 0 && link_up_) {
    link_up_ = false;
    adaptor()->EmitBoolChanged(kLinkUpProperty, link_up_);
    is_eap_detected_ = false;
    DestroyIPConfig();
    SelectService(nullptr);
    manager()->UpdateService(service_);
    service_->OnVisibilityChanged();
    GetEapProvider()->ClearCredentialChangeCallback(this);
    SetIsEapAuthenticated(false);
    StopSupplicant();
    eap_listener_->Stop();
  }
}

bool Ethernet::Load(StoreInterface *storage) {
  const string id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    LOG(WARNING) << "Device is not available in the persistent store: " << id;
    return false;
  }

  bool pppoe = false;
  storage->GetBool(id, kPPPoEProperty, &pppoe);

  Error error;
  ConfigurePPPoEMode(pppoe, &error);
  if (!error.IsSuccess()) {
    LOG(WARNING) << "Error configuring PPPoE mode.  Ignoring!";
  }

  return Device::Load(storage);
}

bool Ethernet::Save(StoreInterface *storage) {
  const string id = GetStorageIdentifier();
  storage->SetBool(id, kPPPoEProperty, GetPPPoEMode(nullptr));
  return true;
}

void Ethernet::ConnectTo(EthernetService *service) {
  CHECK(service == service_.get()) << "Ethernet was asked to connect the "
                                   << "wrong service?";
  CHECK(!GetPPPoEMode(nullptr)) << "We should never connect in PPPoE mode!";
  if (!link_up_) {
    return;
  }
  SelectService(service);
  if (AcquireIPConfigWithLeaseName(service->GetStorageIdentifier())) {
    SetServiceState(Service::kStateConfiguring);
  } else {
    LOG(ERROR) << "Unable to acquire DHCP config.";
    SetServiceState(Service::kStateFailure);
    DestroyIPConfig();
  }
}

void Ethernet::DisconnectFrom(EthernetService *service) {
  CHECK(service == service_.get()) << "Ethernet was asked to disconnect the "
                                   << "wrong service?";
  DropConnection();
}

void Ethernet::TryEapAuthentication() {
  try_eap_authentication_callback_.Reset(
      Bind(&Ethernet::TryEapAuthenticationTask,
           weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostTask(try_eap_authentication_callback_.callback());
}

void Ethernet::BSSAdded(const ::DBus::Path &path,
                        const map<string, ::DBus::Variant> &properties) {
  NOTREACHED() << __func__ << " is not implemented for Ethernet";
}

void Ethernet::BSSRemoved(const ::DBus::Path &path) {
  NOTREACHED() << __func__ << " is not implemented for Ethernet";
}

void Ethernet::Certification(const map<string, ::DBus::Variant> &properties) {
  string subject;
  uint32_t depth;
  if (WPASupplicant::ExtractRemoteCertification(properties, &subject, &depth)) {
    dispatcher()->PostTask(Bind(&Ethernet::CertificationTask,
                                weak_ptr_factory_.GetWeakPtr(),
                                subject, depth));
  }
}

void Ethernet::EAPEvent(const string &status, const string &parameter) {
  dispatcher()->PostTask(Bind(&Ethernet::EAPEventTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              status,
                              parameter));
}

void Ethernet::PropertiesChanged(
  const map<string, ::DBus::Variant> &properties) {
  const map<string, ::DBus::Variant>::const_iterator properties_it =
      properties.find(WPASupplicant::kInterfacePropertyState);
  if (properties_it == properties.end()) {
    return;
  }
  dispatcher()->PostTask(Bind(&Ethernet::SupplicantStateChangedTask,
                              weak_ptr_factory_.GetWeakPtr(),
                              properties_it->second.reader().get_string()));
}

void Ethernet::ScanDone() {
  NOTREACHED() << __func__ << " is not implented for Ethernet";
}

EthernetEapProvider *Ethernet::GetEapProvider() {
  EthernetEapProvider *eap_provider = manager()->ethernet_eap_provider();
  CHECK(eap_provider);
  return eap_provider;
}

ServiceConstRefPtr Ethernet::GetEapService() {
  ServiceConstRefPtr eap_service = GetEapProvider()->service();
  CHECK(eap_service);
  return eap_service;
}

void Ethernet::OnEapDetected() {
  is_eap_detected_ = true;
  eap_listener_->Stop();
  GetEapProvider()->SetCredentialChangeCallback(
      this,
      base::Bind(&Ethernet::TryEapAuthentication,
                 weak_ptr_factory_.GetWeakPtr()));
  TryEapAuthentication();
}

bool Ethernet::StartSupplicant() {
  if (supplicant_process_proxy_.get()) {
    return true;
  }

  supplicant_process_proxy_.reset(
      proxy_factory_->CreateSupplicantProcessProxy(
          WPASupplicant::kDBusPath, WPASupplicant::kDBusAddr));
  ::DBus::Path interface_path;
  try {
    map<string, DBus::Variant> create_interface_args;
    create_interface_args[WPASupplicant::kInterfacePropertyName].writer().
        append_string(link_name().c_str());
    create_interface_args[WPASupplicant::kInterfacePropertyDriver].writer().
        append_string(WPASupplicant::kDriverWired);
    create_interface_args[
        WPASupplicant::kInterfacePropertyConfigFile].writer().
        append_string(WPASupplicant::kSupplicantConfPath);
    interface_path =
        supplicant_process_proxy_->CreateInterface(create_interface_args);
  } catch (const DBus::Error &e) {  // NOLINT
    if (!strcmp(e.name(), WPASupplicant::kErrorInterfaceExists)) {
      interface_path = supplicant_process_proxy_->GetInterface(link_name());
    } else {
      LOG(ERROR) << __func__ << ": Failed to create interface with supplicant.";
      StopSupplicant();
      return false;
    }
  }
  supplicant_interface_proxy_.reset(
      proxy_factory_->CreateSupplicantInterfaceProxy(
          this, interface_path, WPASupplicant::kDBusAddr));
  supplicant_interface_path_ = interface_path;
  return true;
}

bool Ethernet::StartEapAuthentication() {
  map<string, DBus::Variant> params;
  GetEapService()->eap()->PopulateSupplicantProperties(
      &certificate_file_, &params);
  params[WPASupplicant::kNetworkPropertyEapKeyManagement].writer().
      append_string(WPASupplicant::kKeyManagementIeee8021X);
  params[WPASupplicant::kNetworkPropertyEapolFlags].writer().
      append_uint32(0);
  params[WPASupplicant::kNetworkPropertyScanSSID].writer().
      append_uint32(0);
  service_->ClearEAPCertification();
  eap_state_handler_.Reset();

  try {
    if (!supplicant_network_path_.empty()) {
      supplicant_interface_proxy_->RemoveNetwork(supplicant_network_path_);
    }
    supplicant_network_path_ = supplicant_interface_proxy_->AddNetwork(params);
    CHECK(!supplicant_network_path_.empty());
  } catch (const DBus::Error &e) {  // NOLINT
    LOG(ERROR) << "exception while adding network: " << e.what();
    return false;
  }

  supplicant_interface_proxy_->SelectNetwork(supplicant_network_path_);
  supplicant_interface_proxy_->EAPLogon();
  return true;
}

void Ethernet::StopSupplicant() {
  if (supplicant_interface_proxy_.get()) {
    supplicant_interface_proxy_->EAPLogoff();
  }
  supplicant_interface_proxy_.reset();
  if (!supplicant_interface_path_.empty() && supplicant_process_proxy_.get()) {
    try {
      supplicant_process_proxy_->RemoveInterface(
          ::DBus::Path(supplicant_interface_path_));
    } catch (const DBus::Error &e) {  // NOLINT
      LOG(ERROR) << __func__ << ": Failed to remove interface from supplicant.";
    }
  }
  supplicant_network_path_ = "";
  supplicant_interface_path_ = "";
  supplicant_process_proxy_.reset();
  SetIsEapAuthenticated(false);
}

void Ethernet::SetIsEapAuthenticated(bool is_eap_authenticated) {
  if (is_eap_authenticated == is_eap_authenticated_) {
    return;
  }

  // If our EAP authentication state changes, we have now joined a different
  // network.  Restart the DHCP process and any other connection state.
  DisconnectFrom(service_.get());
  ConnectTo(service_.get());
  is_eap_authenticated_ = is_eap_authenticated;
  adaptor()->EmitBoolChanged(kEapAuthenticationCompletedProperty,
                             is_eap_authenticated_);
}

void Ethernet::CertificationTask(const string &subject, uint32_t depth) {
  CHECK(service_) << "Ethernet " << link_name() << " " << __func__
                  << " with no service.";
  service_->AddEAPCertification(subject, depth);
}

void Ethernet::EAPEventTask(const string &status, const string &parameter) {
  LOG(INFO) << "In " << __func__ << " with status " << status
            << ", parameter " << parameter;
  Service::ConnectFailure failure = Service::kFailureUnknown;
  if (eap_state_handler_.ParseStatus(status, parameter, &failure)) {
    LOG(INFO) << "EAP authentication succeeded!";
    SetIsEapAuthenticated(true);
  } else if (failure != Service::Service::kFailureUnknown) {
    LOG(INFO) << "EAP authentication failed!";
    SetIsEapAuthenticated(false);
  }
}

void Ethernet::SupplicantStateChangedTask(const string &state) {
  LOG(INFO) << "Supplicant state changed to " << state;
}

void Ethernet::TryEapAuthenticationTask() {
  if (!GetEapService()->Is8021xConnectable()) {
    if (is_eap_authenticated_) {
      LOG(INFO) << "EAP Service lost 802.1X credentials; "
                << "terminating EAP authentication.";
    } else {
      LOG(INFO) << "EAP Service lacks 802.1X credentials; "
                << "not doing EAP authentication.";
    }
    StopSupplicant();
    return;
  }

  if (!is_eap_detected_) {
    LOG(WARNING) << "EAP authenticator not detected; "
                 << "not doing EAP authentication.";
    return;
  }
  if (!StartSupplicant()) {
    LOG(ERROR) << "Failed to start supplicant.";
    return;
  }
  StartEapAuthentication();
}

void Ethernet::SetupWakeOnLan() {
  int sock;
  struct ifreq interface_command;
  struct ethtool_wolinfo wake_on_lan_command;

  if (link_name().length() >= sizeof(interface_command.ifr_name)) {
    LOG(WARNING) << "Interface name " << link_name() << " too long: "
                 << link_name().size() << " >= "
                 << sizeof(interface_command.ifr_name);
    return;
  }

  sock = sockets_->Socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    LOG(WARNING) << "Failed to allocate socket: "
                 << sockets_->ErrorString() << ".";
    return;
  }
  ScopedSocketCloser socket_closer(sockets_.get(), sock);

  memset(&interface_command, 0, sizeof(interface_command));
  memset(&wake_on_lan_command, 0, sizeof(wake_on_lan_command));
  wake_on_lan_command.cmd = ETHTOOL_SWOL;
  if (manager()->IsWakeOnLanEnabled()) {
    wake_on_lan_command.wolopts = WAKE_MAGIC;
  }
  interface_command.ifr_data = &wake_on_lan_command;
  memcpy(interface_command.ifr_name,
         link_name().data(), link_name().length());

  int res = sockets_->Ioctl(sock, SIOCETHTOOL, &interface_command);
  if (res < 0) {
    LOG(WARNING) << "Failed to enable wake-on-lan: "
                 << sockets_->ErrorString() << ".";
    return;
  }
}

bool Ethernet::ConfigurePPPoEMode(const bool &enable, Error *error) {
#if defined(DISABLE_PPPOE)
  LOG(WARNING) << "PPPoE support is not implemented.  Ignoring attempt "
               << "to configure " << link_name();
  error->Populate(Error::kNotSupported);
  return false;
#else
  CHECK(service_);

  EthernetServiceRefPtr service = nullptr;
  if (enable && service_->technology() != Technology::kPPPoE) {
    service = CreatePPPoEService();
  } else if (!enable && service_->technology() == Technology::kPPPoE) {
    service = CreateEthernetService();
  } else {
    return false;
  }

  CHECK(service);
  service_->Disconnect(error, nullptr);
  manager()->DeregisterService(service_);
  service_ = service;
  manager()->RegisterService(service_);

  return true;
#endif  // DISABLE_PPPOE
}

bool Ethernet::GetPPPoEMode(Error *error) {
  if (service_ == nullptr) {
    return false;
  }
  return service_->technology() == Technology::kPPPoE;
}

void Ethernet::ClearPPPoEMode(Error *error) {
  ConfigurePPPoEMode(false, error);
}

EthernetServiceRefPtr Ethernet::CreateEthernetService() {
  return new EthernetService(control_interface_,
                             dispatcher(),
                             metrics(),
                             manager(),
                             weak_ptr_factory_.GetWeakPtr());
}

EthernetServiceRefPtr Ethernet::CreatePPPoEService() {
  return new PPPoEService(control_interface_,
                          dispatcher(),
                          metrics(),
                          manager(),
                          weak_ptr_factory_.GetWeakPtr());
}

}  // namespace shill
