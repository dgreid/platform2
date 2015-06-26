// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/manager.h"

#include <map>
#include <set>
#include <string>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>
#include <chromeos/dbus/async_event_sequencer.h>
#include <chromeos/dbus/exported_object_manager.h>
#include <chromeos/errors/error.h>
#include <chromeos/http/http_transport.h>
#include <chromeos/key_value_store.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/values_util.h>

#include "buffet/base_api_handler.h"
#include "buffet/commands/command_instance.h"
#include "buffet/commands/schema_constants.h"
#include "buffet/privet/shill_client.h"
#include "buffet/states/state_change_queue.h"
#include "buffet/states/state_manager.h"
#include "buffet/storage_impls.h"

using chromeos::dbus_utils::AsyncEventSequencer;
using chromeos::dbus_utils::ExportedObjectManager;

namespace buffet {

namespace {
// Max of 100 state update events should be enough in the queue.
const size_t kMaxStateChangeQueueSize = 100;
// The number of seconds each HTTP request will be allowed before timing out.
const int kRequestTimeoutSeconds = 30;
}  // anonymous namespace

Manager::Manager(const base::WeakPtr<ExportedObjectManager>& object_manager)
    : dbus_object_(object_manager.get(),
                   object_manager->GetBus(),
                   org::chromium::Buffet::ManagerAdaptor::GetObjectPath()) {
}

Manager::~Manager() {
}

void Manager::Start(const base::FilePath& config_path,
                    const base::FilePath& state_path,
                    const base::FilePath& test_definitions_path,
                    bool xmpp_enabled,
                    const AsyncEventSequencer::CompletionAction& cb) {
  command_manager_ =
      std::make_shared<CommandManager>(dbus_object_.GetObjectManager());
  command_manager_->AddOnCommandDefChanged(base::Bind(
      &Manager::OnCommandDefsChanged, weak_ptr_factory_.GetWeakPtr()));
  command_manager_->Startup(base::FilePath{"/etc/buffet"},
                            test_definitions_path);
  state_change_queue_.reset(new StateChangeQueue(kMaxStateChangeQueueSize));
  state_manager_ = std::make_shared<StateManager>(state_change_queue_.get());
  state_manager_->AddOnChangedCallback(
      base::Bind(&Manager::OnStateChanged, weak_ptr_factory_.GetWeakPtr()));
  state_manager_->Startup();

  std::unique_ptr<BuffetConfig> config{new BuffetConfig{state_path}};
  config->AddOnChangedCallback(
      base::Bind(&Manager::OnConfigChanged, weak_ptr_factory_.GetWeakPtr()));
  config->Load(config_path);

  auto transport = chromeos::http::Transport::CreateDefault();
  transport->SetDefaultTimeout(base::TimeDelta::FromSeconds(
      kRequestTimeoutSeconds));

  shill_client_.reset(new privetd::ShillClient(dbus_object_.GetBus(), {}));

  // TODO(avakulenko): Figure out security implications of storing
  // device info state data unencrypted.
  device_info_.reset(new DeviceRegistrationInfo(
      command_manager_, state_manager_, std::move(config), transport,
      base::MessageLoop::current()->task_runner(),
      xmpp_enabled, shill_client_.get()));
  device_info_->AddOnRegistrationChangedCallback(base::Bind(
      &Manager::OnRegistrationChanged, weak_ptr_factory_.GetWeakPtr()));

  base_api_handler_.reset(new BaseApiHandler{
      device_info_->AsWeakPtr(), state_manager_, command_manager_});

  device_info_->Start();
  dbus_adaptor_.RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void Manager::CheckDeviceRegistered(
    DBusMethodResponsePtr<std::string> response) {
  LOG(INFO) << "Received call to Manager.CheckDeviceRegistered()";
  chromeos::ErrorPtr error;
  bool registered = device_info_->VerifyRegistrationCredentials(&error);
  // If it fails due to any reason other than 'device not registered',
  // treat it as a real error and report it to the caller.
  if (!registered &&
      !error->HasError(kErrorDomainGCD, "device_not_registered")) {
    response->ReplyWithError(error.get());
    return;
  }

  response->Return(registered ? device_info_->GetConfig().device_id()
                              : std::string());
}

void Manager::GetDeviceInfo(DBusMethodResponsePtr<std::string> response) {
  LOG(INFO) << "Received call to Manager.GetDeviceInfo()";
  std::shared_ptr<DBusMethodResponse<std::string>> shared_response =
      std::move(response);

  device_info_->GetDeviceInfo(
      base::Bind(&Manager::OnGetDeviceInfoSuccess,
                 weak_ptr_factory_.GetWeakPtr(), shared_response),
      base::Bind(&Manager::OnGetDeviceInfoError,
                 weak_ptr_factory_.GetWeakPtr(), shared_response));
}

void Manager::OnGetDeviceInfoSuccess(
    const std::shared_ptr<DBusMethodResponse<std::string>>& response,
    const base::DictionaryValue& device_info) {
  std::string device_info_str;
  base::JSONWriter::WriteWithOptions(
      &device_info, base::JSONWriter::OPTIONS_PRETTY_PRINT, &device_info_str);
  response->Return(device_info_str);
}

void Manager::OnGetDeviceInfoError(
    const std::shared_ptr<DBusMethodResponse<std::string>>& response,
    const chromeos::Error* error) {
  response->ReplyWithError(error);
}

void Manager::RegisterDevice(DBusMethodResponsePtr<std::string> response,
                             const std::string& ticket_id) {
  LOG(INFO) << "Received call to Manager.RegisterDevice()";

  chromeos::ErrorPtr error;
  std::string device_id = device_info_->RegisterDevice(ticket_id, &error);
  if (!device_id.empty()) {
    response->Return(device_id);
    return;
  }
  if (!error) {
    // TODO(zeuthen): This can be changed to CHECK(error) once
    // RegisterDevice() has been fixed to set |error| when failing.
    chromeos::Error::AddTo(&error, FROM_HERE, kErrorDomainGCD, "internal_error",
                           "device_id empty but error not set");
  }
  response->ReplyWithError(error.get());
}

void Manager::UpdateState(DBusMethodResponsePtr<> response,
                          const chromeos::VariantDictionary& property_set) {
  chromeos::ErrorPtr error;
  if (!state_manager_->SetProperties(property_set, &error))
    response->ReplyWithError(error.get());
  else
    response->Return();
}

bool Manager::GetState(chromeos::ErrorPtr* error, std::string* state) {
  auto json = state_manager_->GetStateValuesAsJson(error);
  if (!json)
    return false;
  base::JSONWriter::WriteWithOptions(
      json.get(), base::JSONWriter::OPTIONS_PRETTY_PRINT, state);
  return true;
}

void Manager::AddCommand(DBusMethodResponsePtr<std::string> response,
                         const std::string& json_command) {
  static int next_id = 0;
  std::string error_message;
  std::unique_ptr<base::Value> value(base::JSONReader::ReadAndReturnError(
      json_command, base::JSON_PARSE_RFC, nullptr, &error_message));
  if (!value) {
    response->ReplyWithError(FROM_HERE, chromeos::errors::json::kDomain,
                             chromeos::errors::json::kParseError,
                             error_message);
    return;
  }
  chromeos::ErrorPtr error;
  auto command_instance = buffet::CommandInstance::FromJson(
      value.get(), commands::attributes::kCommand_Visibility_Local,
      command_manager_->GetCommandDictionary(), nullptr, &error);
  if (!command_instance) {
    response->ReplyWithError(error.get());
    return;
  }
  std::string id = std::to_string(++next_id);
  command_instance->SetID(id);
  command_manager_->AddCommand(std::move(command_instance));
  response->Return(id);
}

void Manager::GetCommand(DBusMethodResponsePtr<std::string> response,
                         const std::string& id) {
  const CommandInstance* command = command_manager_->FindCommand(id);
  if (!command) {
    response->ReplyWithError(FROM_HERE, kErrorDomainGCD, "unknown_command",
                             "Can't find command with id: " + id);
    return;
  }
  std::string command_str;
  base::JSONWriter::WriteWithOptions(command->ToJson().get(),
      base::JSONWriter::OPTIONS_PRETTY_PRINT, &command_str);
  response->Return(command_str);
}

void Manager::SetCommandVisibility(DBusMethodResponsePtr<> response,
                                   const std::vector<std::string>& in_names,
                                   const std::string& in_visibility) {
  CommandDefinition::Visibility visibility;
  chromeos::ErrorPtr error;
  if (!visibility.FromString(in_visibility, &error)) {
    response->ReplyWithError(error.get());
    return;
  }
  if (!command_manager_->SetCommandVisibility(in_names, visibility, &error)) {
    response->ReplyWithError(error.get());
    return;
  }
  response->Return();
}

std::string Manager::TestMethod(const std::string& message) {
  LOG(INFO) << "Received call to test method: " << message;
  return message;
}

bool Manager::UpdateDeviceInfo(chromeos::ErrorPtr* error,
                               const std::string& in_name,
                               const std::string& in_description,
                               const std::string& in_location) {
  return device_info_->UpdateDeviceInfo(in_name, in_description, in_location,
                                        error);
}

bool Manager::UpdateServiceConfig(chromeos::ErrorPtr* error,
                                  const std::string& client_id,
                                  const std::string& client_secret,
                                  const std::string& api_key,
                                  const std::string& oauth_url,
                                  const std::string& service_url) {
  return device_info_->UpdateServiceConfig(client_id, client_secret, api_key,
                                           oauth_url, service_url, error);
}

void Manager::OnCommandDefsChanged() {
  // Limit only to commands that are visible to the local clients.
  auto commands = command_manager_->GetCommandDictionary().GetCommandsAsJson(
      [](const buffet::CommandDefinition* def) {
        return def->GetVisibility().local;
      }, true, nullptr);
  CHECK(commands);
  std::string json;
  base::JSONWriter::WriteWithOptions(commands.get(),
      base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  dbus_adaptor_.SetCommandDefs(json);
}

void Manager::OnStateChanged() {
  auto state = state_manager_->GetStateValuesAsJson(nullptr);
  CHECK(state);
  std::string json;
  base::JSONWriter::WriteWithOptions(
      state.get(), base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  dbus_adaptor_.SetState(json);
}

void Manager::OnRegistrationChanged(RegistrationStatus status) {
  dbus_adaptor_.SetStatus(StatusToString(status));
}

void Manager::OnConfigChanged(const BuffetConfig& config) {
  dbus_adaptor_.SetDeviceId(config.device_id());
  dbus_adaptor_.SetOemName(config.oem_name());
  dbus_adaptor_.SetModelName(config.model_name());
  dbus_adaptor_.SetModelId(config.model_id());
  dbus_adaptor_.SetName(config.name());
  dbus_adaptor_.SetDescription(config.description());
  dbus_adaptor_.SetLocation(config.location());
  dbus_adaptor_.SetAnonymousAccessRole(config.local_anonymous_access_role());
}

}  // namespace buffet
