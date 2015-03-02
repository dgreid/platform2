// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/pppoe/pppoe_service.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include <base/callback.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/ethernet/ethernet.h"
#include "shill/event_dispatcher.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/ppp_daemon.h"
#include "shill/ppp_device.h"
#include "shill/ppp_device_factory.h"
#include "shill/store_interface.h"

using base::StringPrintf;
using std::map;
using std::string;
using std::unique_ptr;

namespace shill {

PPPoEService::PPPoEService(ControlInterface *control_interface,
                           EventDispatcher *dispatcher,
                           Metrics *metrics,
                           Manager *manager,
                           base::WeakPtr<Ethernet> ethernet)
    : EthernetService(control_interface, dispatcher, metrics, manager,
                      Technology::kPPPoE, ethernet),
      control_interface_(control_interface),
      authenticating_(false),
      weak_ptr_factory_(this) {
  PropertyStore *store = this->mutable_store();
  store->RegisterString(kPPPoEUsernameProperty, &username_);
  store->RegisterString(kPPPoEPasswordProperty, &password_);

  set_friendly_name("PPPoE");
  SetConnectable(true);
  SetAutoConnect(true);
  NotifyPropertyChanges();
}

PPPoEService::~PPPoEService() {}

void PPPoEService::Connect(Error *error, const char *reason) {
  Service::Connect(error, reason);

  CHECK(ethernet());

  if (!ethernet()->link_up()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed, StringPrintf(
            "PPPoE Service %s does not have Ethernet link.",
            unique_name().c_str()));
    return;
  }

  if (IsConnected()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kAlreadyConnected,
                          StringPrintf("PPPoE service %s already connected.",
                                       unique_name().c_str()));
    return;
  }

  if (IsConnecting()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          StringPrintf("PPPoE service %s already connecting.",
                                       unique_name().c_str()));
    return;
  }

  PPPDaemon::DeathCallback callback(base::Bind(&PPPoEService::OnPPPDied,
                                               weak_ptr_factory_.GetWeakPtr()));

  PPPDaemon::Options options;
  options.no_detach = true;
  options.no_default_route = true;
  options.use_peer_dns = true;
  options.use_pppoe_plugin = true;

  pppd_ = PPPDaemon::Start(
      control_interface_, manager()->glib(), weak_ptr_factory_.GetWeakPtr(),
      options, ethernet()->link_name(), callback, error);
  if (pppd_ == nullptr) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          StringPrintf("PPPoE service %s can't start pppd.",
                                       unique_name().c_str()));
    return;
  }

  SetState(Service::kStateAssociating);
}

void PPPoEService::Disconnect(Error *error, const char *reason) {
  EthernetService::Disconnect(error, reason);
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
}

bool PPPoEService::Load(StoreInterface *storage) {
  if (!Service::Load(storage)) {
    return false;
  }

  const string id = GetStorageIdentifier();
  storage->GetString(id, kPPPoEUsernameProperty, &username_);
  storage->GetString(id, kPPPoEPasswordProperty, &password_);

  return true;
}

bool PPPoEService::Save(StoreInterface *storage) {
  if (!Service::Save(storage)) {
    return false;
  }

  const string id = GetStorageIdentifier();
  storage->SetString(id, kPPPoEUsernameProperty, username_);
  storage->SetString(id, kPPPoEPasswordProperty, password_);

  return true;
}

bool PPPoEService::Unload() {
  username_.clear();
  password_.clear();
  return Service::Unload();
}

void PPPoEService::GetLogin(string *user, string *password) {
  CHECK(user && password);
  *user = username_;
  *password = password_;
}

void PPPoEService::Notify(const string &reason,
                          const map<string, string> &dict) {
  if (reason == kPPPReasonAuthenticating) {
    OnPPPAuthenticating();
  } else if (reason == kPPPReasonAuthenticated) {
    OnPPPAuthenticated();
  } else if (reason == kPPPReasonConnect) {
    OnPPPConnected(dict);
  } else if (reason == kPPPReasonDisconnect) {
    OnPPPDisconnected();
  } else {
    NOTREACHED();
  }
}

void PPPoEService::OnPPPDied(pid_t pid, int exit) {
  OnPPPDisconnected();
}

void PPPoEService::OnPPPAuthenticating() {
  authenticating_ = true;
}

void PPPoEService::OnPPPAuthenticated() {
  authenticating_ = false;
}

void PPPoEService::OnPPPConnected(const map<string, string> &params) {
  const string interface_name = PPPDevice::GetInterfaceName(params);

  DeviceInfo *device_info = manager()->device_info();
  const int interface_index = device_info->GetIndex(interface_name);
  if (interface_index < 0) {
    NOTIMPLEMENTED() << ": No device info for " << interface_name;
    return;
  }

  if (ppp_device_) {
    ppp_device_->SelectService(nullptr);
  }

  const auto factory = PPPDeviceFactory::GetInstance();
  ppp_device_ = factory->CreatePPPDevice(
      control_interface_, dispatcher(), metrics(), manager(), interface_name,
      interface_index);
  device_info->RegisterDevice(ppp_device_);
  ppp_device_->SetEnabled(true);
  ppp_device_->SelectService(this);
  ppp_device_->UpdateIPConfigFromPPP(params, false);
}

void PPPoEService::OnPPPDisconnected() {
  pppd_.release()->DestroyLater(dispatcher());

  Error unused_error;
  Disconnect(&unused_error, __func__);

  if (authenticating_) {
    SetFailure(Service::kFailurePPPAuth);
  } else {
    SetFailure(Service::kFailureUnknown);
  }
}

}  // namespace shill
