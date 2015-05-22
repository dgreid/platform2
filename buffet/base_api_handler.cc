// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/base_api_handler.h"

#include "buffet/commands/command_instance.h"
#include "buffet/commands/command_manager.h"
#include "buffet/device_registration_info.h"
#include "buffet/states/state_manager.h"

namespace buffet {

namespace {

// Helps to get parameters from native_types::Object representing
// CommandInstance parameters.
class ParametersReader final {
 public:
  explicit ParametersReader(const native_types::Object* parameters)
      : parameters_{parameters} {}

  bool GetParameter(const std::string& name, std::string* value) const {
    auto it = parameters_->find(name);
    if (it == parameters_->end())
      return false;
    const StringValue* string_value = it->second->GetString();
    if (!string_value)
      return false;
    *value = string_value->GetValue();
    return true;
  }

  bool GetParameter(const std::string& name, bool* value) const {
    auto it = parameters_->find(name);
    if (it == parameters_->end())
      return false;
    const BooleanValue* bool_value = it->second->GetBoolean();
    if (!bool_value)
      return false;
    *value = bool_value->GetValue();
    return true;
  }

 private:
  const native_types::Object* parameters_;
};

}  // namespace

BaseApiHandler::BaseApiHandler(
    const base::WeakPtr<DeviceRegistrationInfo>& device_info,
    const std::shared_ptr<StateManager>& state_manager,
    const std::shared_ptr<CommandManager>& command_manager)
    : device_info_{device_info}, state_manager_{state_manager} {
  command_manager->AddOnCommandAddedCallback(base::Bind(
      &BaseApiHandler::OnCommandAdded, weak_ptr_factory_.GetWeakPtr()));
}

void BaseApiHandler::OnCommandAdded(CommandInstance* command) {
  if (command->GetStatus() != CommandInstance::kStatusQueued)
    return;

  if (command->GetName() == "base.updateBaseConfiguration")
    return UpdateBaseConfiguration(command);

  if (command->GetName() == "base.updateDeviceInfo")
    return UpdateDeviceInfo(command);
}

void BaseApiHandler::UpdateBaseConfiguration(CommandInstance* command) {
  command->SetProgress({});

  const BuffetConfig& config{device_info_->GetConfig()};
  std::string anonymous_access_role{config.local_anonymous_access_role()};
  bool discovery_enabled{config.local_discovery_enabled()};
  bool pairing_enabled{config.local_pairing_enabled()};

  ParametersReader parameters{&command->GetParameters()};
  parameters.GetParameter("localAnonymousAccessMaxRole",
                          &anonymous_access_role);
  parameters.GetParameter("localDiscoveryEnabled", &discovery_enabled);
  parameters.GetParameter("localPairingEnabled", &pairing_enabled);

  chromeos::VariantDictionary state{
      {"base.localAnonymousAccessMaxRole", anonymous_access_role},
      {"base.localDiscoveryEnabled", discovery_enabled},
      {"base.localPairingEnabled", pairing_enabled},
  };
  if (!state_manager_->SetProperties(state, nullptr)) {
    return command->Abort();
  }

  if (!device_info_->UpdateBaseConfig(anonymous_access_role, discovery_enabled,
                                      pairing_enabled, nullptr)) {
    return command->Abort();
  }

  command->Done();
}

void BaseApiHandler::UpdateDeviceInfo(CommandInstance* command) {
  command->SetProgress({});

  const BuffetConfig& config{device_info_->GetConfig()};
  std::string name{config.name()};
  std::string description{config.description()};
  std::string location{config.location()};

  ParametersReader parameters(&command->GetParameters());
  parameters.GetParameter("name", &name);
  parameters.GetParameter("description", &description);
  parameters.GetParameter("location", &location);

  if (!device_info_->UpdateDeviceInfo(name, description, location, nullptr)) {
    return command->Abort();
  }

  command->Done();
}

}  // namespace buffet
