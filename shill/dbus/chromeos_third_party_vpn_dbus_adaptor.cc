// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_third_party_vpn_dbus_adaptor.h"

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"
#include "shill/service.h"
#include "shill/vpn/third_party_vpn_driver.h"

using chromeos::dbus_utils::AsyncEventSequencer;
using chromeos::dbus_utils::ExportedObjectManager;

namespace shill {

namespace Logging {

static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const ChromeosThirdPartyVpnDBusAdaptor* v) {
  return "(third_party_vpn_dbus_adaptor)";
}

}  // namespace Logging

namespace {

// The API converts external connection state to internal one
bool ConvertConnectState(
    ChromeosThirdPartyVpnDBusAdaptor::ExternalConnectState external_state,
    Service::ConnectState* internal_state) {
  switch (external_state) {
    case ChromeosThirdPartyVpnDBusAdaptor::kStateConnected:
      *internal_state = Service::kStateOnline;
      break;
    case ChromeosThirdPartyVpnDBusAdaptor::kStateFailure:
      *internal_state = Service::kStateFailure;
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace

ChromeosThirdPartyVpnDBusAdaptor::ChromeosThirdPartyVpnDBusAdaptor(
    const base::WeakPtr<ExportedObjectManager>& object_manager,
    const scoped_refptr<dbus::Bus>& bus,
    ThirdPartyVpnDriver* client)
    : org::chromium::flimflam::ThirdPartyVpnAdaptor(this),
      ChromeosDBusAdaptor(object_manager,
                          bus,
                          kObjectPathBase + client->object_path_suffix()),
      client_(client) {
  // Register DBus object.
  RegisterWithDBusObject(dbus_object());
  dbus_object()->RegisterAsync(
      AsyncEventSequencer::GetDefaultCompletionAction());
}

ChromeosThirdPartyVpnDBusAdaptor::~ChromeosThirdPartyVpnDBusAdaptor() {}

void ChromeosThirdPartyVpnDBusAdaptor::EmitPacketReceived(
    const std::vector<uint8_t>& packet) {
  SLOG(this, 2) << __func__;
  SendOnPacketReceivedSignal(packet);
}

void ChromeosThirdPartyVpnDBusAdaptor::EmitPlatformMessage(uint32_t message) {
  SLOG(this, 2) << __func__ << "(" << message << ")";
  SendOnPlatformMessageSignal(message);
}

bool ChromeosThirdPartyVpnDBusAdaptor::SetParameters(
    chromeos::ErrorPtr* error,
    const std::map<std::string, std::string>& parameters,
    std::string* warning_message) {
  SLOG(this, 2) << __func__;
  std::string error_message;
  Error e;
  client_->SetParameters(parameters, &error_message, warning_message);
  if (!error_message.empty()) {
    e.Populate(Error::kInvalidArguments, error_message);
  }
  return !e.ToChromeosError(error);
}

bool ChromeosThirdPartyVpnDBusAdaptor::UpdateConnectionState(
    chromeos::ErrorPtr* error, uint32_t connection_state) {
  SLOG(this, 2) << __func__ << "(" << connection_state << ")";
  // Externally supported states are from Service::kStateConnected to
  // Service::kStateOnline.
  Service::ConnectState internal_state;
  std::string error_message;
  Error e;
  if (ConvertConnectState(static_cast<ExternalConnectState>(connection_state),
                          &internal_state)) {
    client_->UpdateConnectionState(internal_state, &error_message);
    if (!error_message.empty()) {
      e.Populate(Error::kInvalidArguments, error_message);
    }
  } else {
    e.Populate(Error::kNotSupported, "Connection state is not supported");
  }
  return !e.ToChromeosError(error);
}

bool ChromeosThirdPartyVpnDBusAdaptor::SendPacket(
    chromeos::ErrorPtr* error,
    const std::vector<uint8_t>& ip_packet) {
  SLOG(this, 2) << __func__;
  std::string error_message;
  client_->SendPacket(ip_packet, &error_message);
  Error e;
  if (!error_message.empty()) {
    e.Populate(Error::kWrongState, error_message);
  }
  return !e.ToChromeosError(error);
}

}  // namespace shill
