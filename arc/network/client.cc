// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/client.h"

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

namespace patchpanel {

// static
std::unique_ptr<Client> Client::New() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return nullptr;
  }

  dbus::ObjectProxy* proxy = bus->GetObjectProxy(
      kPatchPanelServiceName, dbus::ObjectPath(kPatchPanelServicePath));
  if (!proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for " << kPatchPanelServiceName;
    return nullptr;
  }

  return std::make_unique<Client>(std::move(bus), proxy);
}

Client::~Client() {
  if (bus_)
    bus_->ShutdownAndBlock();
}

bool Client::NotifyArcStartup(pid_t pid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcStartupRequest request;
  request.set_pid(static_cast<uint32_t>(pid));

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool Client::NotifyArcShutdown() {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcShutdownRequest request;
  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

std::vector<patchpanel::Device> Client::NotifyArcVmStartup(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmStartupRequest proto";
    return {};
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return {};
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return {};
  }

  std::vector<patchpanel::Device> devices;
  for (const auto& d : response.devices()) {
    devices.emplace_back(d);
  }
  return devices;
}

bool Client::NotifyArcVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kArcVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  ArcVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode ArcVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  ArcVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool Client::NotifyTerminaVmStartup(uint32_t cid,
                                    patchpanel::Device* device,
                                    patchpanel::IPv4Subnet* container_subnet) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmStartupRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No device found";
    return false;
  }
  *device = response.device();

  if (response.has_container_subnet()) {
    *container_subnet = response.container_subnet();
  } else {
    LOG(WARNING) << "No container subnet found";
  }

  return true;
}

bool Client::NotifyTerminaVmShutdown(uint32_t cid) {
  dbus::MethodCall method_call(kPatchPanelInterface, kTerminaVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  TerminaVmShutdownRequest request;
  request.set_cid(cid);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode TerminaVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  TerminaVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool Client::NotifyPluginVmStartup(uint64_t vm_id,
                                   int subnet_index,
                                   patchpanel::Device* device) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmStartupMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmStartupRequest request;
  request.set_id(vm_id);
  request.set_subnet_index(subnet_index);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmStartupRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmStartupResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  if (!response.has_device()) {
    LOG(ERROR) << "No device found";
    return false;
  }
  *device = response.device();

  return true;
}

bool Client::NotifyPluginVmShutdown(uint64_t vm_id) {
  dbus::MethodCall method_call(kPatchPanelInterface, kPluginVmShutdownMethod);
  dbus::MessageWriter writer(&method_call);

  PluginVmShutdownRequest request;
  request.set_id(vm_id);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode PluginVmShutdownRequest proto";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  PluginVmShutdownResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response proto";
    return false;
  }

  return true;
}

bool Client::DefaultVpnRouting(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::DEFAULT_ROUTING);
}

bool Client::RouteOnVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::ROUTE_ON_VPN);
}

bool Client::BypassVpn(int socket) {
  return SendSetVpnIntentRequest(socket, SetVpnIntentRequest::BYPASS_VPN);
}

bool Client::SendSetVpnIntentRequest(
    int socket, SetVpnIntentRequest::VpnRoutingPolicy policy) {
  dbus::MethodCall method_call(kPatchPanelInterface, kSetVpnIntentMethod);
  dbus::MessageWriter writer(&method_call);

  SetVpnIntentRequest request;
  SetVpnIntentResponse response;
  request.set_policy(policy);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode SetVpnIntentRequest proto";
    return false;
  }
  writer.AppendFileDescriptor(socket);

  std::unique_ptr<dbus::Response> dbus_response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!dbus_response) {
    LOG(ERROR)
        << "Failed to send SetVpnIntentRequest message to patchpanel service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse SetVpnIntentResponse proto";
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "SetVpnIntentRequest failed";
    return false;
  }
  return true;
}

}  // namespace patchpanel
