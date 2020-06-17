// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/agent_manager_interface_handler.h"

#include <string>

#include <base/bind.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "bluetooth/newblued/util.h"

namespace bluetooth {

AgentManagerInterfaceHandler::AgentManagerInterfaceHandler(
    scoped_refptr<dbus::Bus> bus,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper)
    : bus_(bus),
      exported_object_manager_wrapper_(exported_object_manager_wrapper),
      weak_ptr_factory_(this) {}

void AgentManagerInterfaceHandler::Init() {
  dbus::ObjectPath agent_manager_object_path(
      bluetooth_agent_manager::kBluetoothAgentManagerServicePath);
  exported_object_manager_wrapper_->AddExportedInterface(
      agent_manager_object_path,
      bluetooth_agent_manager::kBluetoothAgentManagerInterface,
      base::Bind(&ExportedObjectManagerWrapper::SetupStandardPropertyHandlers));
  ExportedInterface* agent_manager_interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          agent_manager_object_path,
          bluetooth_agent_manager::kBluetoothAgentManagerInterface);

  agent_manager_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_agent_manager::kRegisterAgent, base::Unretained(this),
      &AgentManagerInterfaceHandler::HandleRegisterAgent);
  agent_manager_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_agent_manager::kUnregisterAgent, base::Unretained(this),
      &AgentManagerInterfaceHandler::HandleUnregisterAgent);
  agent_manager_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_agent_manager::kRequestDefaultAgent, base::Unretained(this),
      &AgentManagerInterfaceHandler::HandleRequestDefaultAgent);

  agent_manager_interface->ExportAndBlock();
}

void AgentManagerInterfaceHandler::DisplayPasskey(
    const std::string& device_address, uint32_t passkey) {
  LOG(INFO) << "Please enter passkey " << passkey << " on the device";

  if (default_agent_client_.empty() ||
      !base::Contains(agent_object_paths_, default_agent_client_)) {
    LOG(WARNING) << "No agent available to display passkey";
    return;
  }

  dbus::MethodCall method_call(bluetooth_agent::kBluetoothAgentInterface,
                               bluetooth_agent::kDisplayPasskey);

  dbus::MessageWriter writer(&method_call);
  writer.AppendObjectPath(
      dbus::ObjectPath(ConvertDeviceAddressToObjectPath(device_address)));
  writer.AppendUint32(passkey);
  // The number of keys that have been pressed. Currently hardcode to 0 until
  // we have support for this information in libnewblue.
  writer.AppendUint16(0);

  dbus::ObjectProxy* agent_object_proxy = bus_->GetObjectProxy(
      default_agent_client_, agent_object_paths_[default_agent_client_]);
  agent_object_proxy->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(&AgentManagerInterfaceHandler::OnDisplayPasskeySent,
                 weak_ptr_factory_.GetWeakPtr()));
}

void AgentManagerInterfaceHandler::OnDisplayPasskeySent(
    dbus::Response* response) {
  VLOG(1) << __func__;
}

void AgentManagerInterfaceHandler::RequestAuthorization(
    const std::string& device_address) {
  LOG(INFO) << "Request authorization on the device";

  if (default_agent_client_.empty() ||
      !base::Contains(agent_object_paths_, default_agent_client_)) {
    LOG(WARNING) << "No agent available to request authorization";
    return;
  }

  dbus::MethodCall method_call(bluetooth_agent::kBluetoothAgentInterface,
                               bluetooth_agent::kRequestAuthorization);

  dbus::MessageWriter writer(&method_call);
  writer.AppendObjectPath(
      dbus::ObjectPath(ConvertDeviceAddressToObjectPath(device_address)));

  dbus::ObjectProxy* agent_object_proxy = bus_->GetObjectProxy(
      default_agent_client_, agent_object_paths_[default_agent_client_]);
  agent_object_proxy->CallMethodWithErrorCallback(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(&AgentManagerInterfaceHandler::OnRequestAuthorizationResponse,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&AgentManagerInterfaceHandler::OnRequestAuthorizationError,
                 weak_ptr_factory_.GetWeakPtr()));
}

void AgentManagerInterfaceHandler::OnRequestAuthorizationResponse(
    dbus::Response* response) {
  VLOG(1) << __func__;
  // TODO(yudiliu): feed the response back to libnewblue
}

void AgentManagerInterfaceHandler::OnRequestAuthorizationError(
    dbus::ErrorResponse* response) {
  VLOG(1) << __func__;
  // TODO(yudiliu): feed the response back to libnewblue
}

bool AgentManagerInterfaceHandler::HandleRegisterAgent(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    dbus::ObjectPath agent_object_path,
    std::string capability) {
  VLOG(1) << "Registering agent " << agent_object_path.value()
          << " with capability = " << capability;
  agent_object_paths_[message->GetSender()] = agent_object_path;
  return true;
}

bool AgentManagerInterfaceHandler::HandleUnregisterAgent(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    dbus::ObjectPath agent_object_path) {
  VLOG(1) << __func__;

  if (base::Contains(agent_object_paths_, message->GetSender())) {
    if (agent_object_paths_[message->GetSender()] != agent_object_path)
      LOG(WARNING) << "Agent path does not match.";
    agent_object_paths_.erase(message->GetSender());
  }

  if (default_agent_client_ == message->GetSender())
    default_agent_client_ = "";

  return true;
}

bool AgentManagerInterfaceHandler::HandleRequestDefaultAgent(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    dbus::ObjectPath agent_object_path) {
  VLOG(1) << "Setting default agent " << agent_object_path.value();

  if (!base::Contains(agent_object_paths_, message->GetSender())) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_adapter::kErrorFailed,
        base::StringPrintf("Client %s has not registered agent.",
                           message->GetSender().c_str()));
    return false;
  }

  default_agent_client_ = message->GetSender();

  if (agent_object_path != agent_object_paths_[default_agent_client_])
    LOG(WARNING) << "Agent path does not match.";

  return true;
}

}  // namespace bluetooth
