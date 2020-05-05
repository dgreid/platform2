// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/pppoe/pppoe_service.h"

#include <algorithm>
#include <map>
#include <string>

#include <base/callback.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/ethernet/ethernet.h"
#include "shill/event_dispatcher.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/ppp_daemon.h"
#include "shill/ppp_device.h"
#include "shill/ppp_device_factory.h"
#include "shill/process_manager.h"
#include "shill/store_interface.h"

using base::StringPrintf;
using std::map;
using std::string;

namespace shill {

namespace {

constexpr int kDefaultLCPEchoInterval = 30;
constexpr int kDefaultLCPEchoFailure = 3;
constexpr int kDefaultMaxFailure = 3;

}  // namespace

PPPoEService::PPPoEService(Manager* manager, base::WeakPtr<Ethernet> ethernet)
    : EthernetService(manager, Technology::kPPPoE, Properties(ethernet)),
      ppp_device_factory_(PPPDeviceFactory::GetInstance()),
      process_manager_(ProcessManager::GetInstance()),
      lcp_echo_interval_(kDefaultLCPEchoInterval),
      lcp_echo_failure_(kDefaultLCPEchoFailure),
      max_failure_(kDefaultMaxFailure),
      authenticating_(false),
      weak_ptr_factory_(this) {
  set_log_name("ppoe_" + base::NumberToString(serial_number()));
  PropertyStore* store = this->mutable_store();
  store->RegisterString(kPPPoEUsernameProperty, &username_);
  store->RegisterString(kPPPoEPasswordProperty, &password_);
  store->RegisterInt32(kPPPoELCPEchoIntervalProperty, &lcp_echo_interval_);
  store->RegisterInt32(kPPPoELCPEchoFailureProperty, &lcp_echo_failure_);
  store->RegisterInt32(kPPPoEMaxAuthFailureProperty, &max_failure_);

  set_friendly_name("PPPoE");
  SetConnectable(true);
  SetAutoConnect(true);
  NotifyIfVisibilityChanged();
}

PPPoEService::~PPPoEService() = default;

void PPPoEService::OnConnect(Error* error) {
  CHECK(ethernet());

  if (!ethernet()->link_up()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        StringPrintf("PPPoE Service %s does not have Ethernet link.",
                     log_name().c_str()));
    return;
  }

  PPPDaemon::DeathCallback callback(
      base::Bind(&PPPoEService::OnPPPDied, weak_ptr_factory_.GetWeakPtr()));

  PPPDaemon::Options options;
  options.no_detach = true;
  options.no_default_route = true;
  options.use_peer_dns = true;
  options.use_pppoe_plugin = true;
  options.lcp_echo_interval = lcp_echo_interval_;
  options.lcp_echo_failure = lcp_echo_failure_;
  options.max_fail = max_failure_;
  options.use_ipv6 = true;

  pppd_ = PPPDaemon::Start(control_interface(), process_manager_,
                           weak_ptr_factory_.GetWeakPtr(), options,
                           ethernet()->link_name(), callback, error);
  if (pppd_ == nullptr) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInternalError,
        StringPrintf("PPPoE service %s can't start pppd.", log_name().c_str()));
    return;
  }

  SetState(Service::kStateAssociating);
}

void PPPoEService::OnDisconnect(Error* error, const char* reason) {
  EthernetService::OnDisconnect(error, reason);
  if (ppp_device_) {
    ppp_device_->DropConnection();
  } else {
    // If no PPPDevice has been associated with this service then nothing will
    // drive this service's transition into the idle state.  This must be forced
    // here to ensure that the service is not left in any intermediate state.
    SetState(Service::kStateIdle);
  }
  ppp_device_ = nullptr;
  pppd_.reset();
  manager()->OnInnerDevicesChanged();
}

bool PPPoEService::Load(const StoreInterface* storage) {
  if (!Service::Load(storage)) {
    return false;
  }

  const string id = GetStorageIdentifier();
  storage->GetString(id, kPPPoEUsernameProperty, &username_);
  storage->GetString(id, kPPPoEPasswordProperty, &password_);
  storage->GetInt(id, kPPPoELCPEchoIntervalProperty, &lcp_echo_interval_);
  storage->GetInt(id, kPPPoELCPEchoFailureProperty, &lcp_echo_failure_);
  storage->GetInt(id, kPPPoEMaxAuthFailureProperty, &max_failure_);

  return true;
}

bool PPPoEService::Save(StoreInterface* storage) {
  if (!Service::Save(storage)) {
    return false;
  }

  const string id = GetStorageIdentifier();
  storage->SetString(id, kPPPoEUsernameProperty, username_);
  storage->SetString(id, kPPPoEPasswordProperty, password_);
  storage->SetInt(id, kPPPoELCPEchoIntervalProperty, lcp_echo_interval_);
  storage->SetInt(id, kPPPoELCPEchoFailureProperty, lcp_echo_failure_);
  storage->SetInt(id, kPPPoEMaxAuthFailureProperty, max_failure_);

  return true;
}

bool PPPoEService::Unload() {
  username_.clear();
  password_.clear();
  return Service::Unload();
}

const RpcIdentifier& PPPoEService::GetInnerDeviceRpcIdentifier() const {
  static RpcIdentifier null_identifier;
  return ppp_device_ ? ppp_device_->GetRpcIdentifier() : null_identifier;
}

void PPPoEService::GetLogin(string* user, string* password) {
  CHECK(user && password);
  *user = username_;
  *password = password_;
}

void PPPoEService::Notify(const string& reason,
                          const map<string, string>& dict) {
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

void PPPoEService::OnPPPAuthenticating() {
  authenticating_ = true;
}

void PPPoEService::OnPPPAuthenticated() {
  authenticating_ = false;
}

void PPPoEService::OnPPPConnected(const map<string, string>& params) {
  const string interface_name = PPPDevice::GetInterfaceName(params);

  DeviceInfo* device_info = manager()->device_info();
  const int interface_index = device_info->GetIndex(interface_name);
  if (interface_index < 0) {
    NOTIMPLEMENTED() << ": No device info for " << interface_name;
    return;
  }

  if (ppp_device_) {
    // PPPDevice instance registered with device_info is getting
    // destroyed when the pppd exit or at shill stop. This constraint
    // makes, reusing the existing PPPDevice instance to handle the
    // back to back ip_up_notifier from the pppd.

    // PPPDevice's interface_index and interface_name remains unchanged
    CHECK((interface_name == ppp_device_->link_name()) &&
          (interface_index == ppp_device_->interface_index()));
    ppp_device_->DropConnection();
  } else {
    ppp_device_ = ppp_device_factory_->CreatePPPDevice(
        manager(), interface_name, interface_index);
    device_info->RegisterDevice(ppp_device_);
    ppp_device_->SetEnabled(true);
  }

  ppp_device_->SelectService(this);
  ppp_device_->UpdateIPConfigFromPPP(params, false /* blackhole_ipv6 */);
#ifndef DISABLE_DHCPV6
  // Acquire DHCPv6 configurations through the PPPoE (virtual) interface
  // if it is enabled for DHCPv6.
  if (manager()->IsDHCPv6EnabledForDevice(ppp_device_->link_name())) {
    ppp_device_->AcquireIPv6Config();
  }
#endif
  manager()->OnInnerDevicesChanged();
}

void PPPoEService::OnPPPDied(pid_t pid, int exit) {
  Error unused_error;
  Disconnect(&unused_error, __func__);

  if (authenticating_) {
    SetFailure(Service::kFailurePPPAuth);
  } else {
    SetFailure(PPPDevice::ExitStatusToFailure(exit));
  }
}

}  // namespace shill
