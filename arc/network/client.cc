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

  return std::make_unique<Client>(bus, proxy);
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

}  // namespace patchpanel
