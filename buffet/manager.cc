// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/manager.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/time/time.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_utils.h>
#include <brillo/key_value_store.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/mime_utils.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/values_util.h>
#include <weave/enum_to_string.h>

#include "buffet/buffet_config.h"
#include "buffet/dbus_command_dispatcher.h"
#include "buffet/dbus_conversion.h"
#include "buffet/http_transport_client.h"
#include "buffet/shill_client.h"
#include "buffet/weave_error_conversion.h"

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::ExportedObjectManager;

namespace buffet {

namespace {

const char kPairingSessionIdKey[] = "sessionId";
const char kPairingModeKey[] = "mode";
const char kPairingCodeKey[] = "code";

const char kErrorDomain[] = "buffet";
const char kFileReadError[] = "file_read_error";

bool LoadFile(const base::FilePath& file_path,
              std::string* data,
              brillo::ErrorPtr* error) {
  if (!base::ReadFileToString(file_path, data)) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    brillo::Error::AddToPrintf(error, FROM_HERE, kErrorDomain, kFileReadError,
                               "Failed to read file '%s'",
                               file_path.value().c_str());
    return false;
  }
  return true;
}

void LoadCommandDefinitions(const BuffetConfig::Options& options,
                            weave::Device* device) {
  auto load_packages = [device](const base::FilePath& root,
                                const std::string& pattern) {
    base::FilePath dir{root.Append("commands")};
    LOG(INFO) << "Looking for command schemas in " << dir.value();
    base::FileEnumerator enumerator(dir, false, base::FileEnumerator::FILES,
                                    pattern);
    for (base::FilePath path = enumerator.Next(); !path.empty();
         path = enumerator.Next()) {
      LOG(INFO) << "Loading command schema from " << path.value();
      std::string json;
      CHECK(LoadFile(path, &json, nullptr));
      device->AddCommandDefinitionsFromJson(json);
    }
  };
  load_packages(options.definitions, "*.json");
  load_packages(options.test_definitions, "*test.json");
}

void LoadStateDefinitions(const BuffetConfig::Options& options,
                          weave::Device* device) {
  // Load component-specific device state definitions.
  base::FilePath dir{options.definitions.Append("states")};
  LOG(INFO) << "Looking for state definitions in " << dir.value();
  base::FileEnumerator enumerator(dir, false, base::FileEnumerator::FILES,
                                  "*.schema.json");
  std::vector<std::string> result;
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    LOG(INFO) << "Loading state definition from " << path.value();
    std::string json;
    CHECK(LoadFile(path, &json, nullptr));
    device->AddStateDefinitionsFromJson(json);
  }
}

void LoadStateDefaults(const BuffetConfig::Options& options,
                       weave::Device* device) {
  // Load component-specific device state defaults.
  base::FilePath dir{options.definitions.Append("states")};
  LOG(INFO) << "Looking for state defaults in " << dir.value();
  base::FileEnumerator enumerator(dir, false, base::FileEnumerator::FILES,
                                  "*.defaults.json");
  std::vector<std::string> result;
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    LOG(INFO) << "Loading state defaults from " << path.value();
    std::string json;
    CHECK(LoadFile(path, &json, nullptr));
    CHECK(device->SetStatePropertiesFromJson(json, nullptr));
  }
}

}  // anonymous namespace

class Manager::TaskRunner : public weave::provider::TaskRunner {
 public:
  void PostDelayedTask(const base::Location& from_here,
                       const base::Closure& task,
                       base::TimeDelta delay) override {
    brillo::MessageLoop::current()->PostDelayedTask(from_here, task, delay);
  }
};

Manager::Manager(const Options& options,
                 const base::WeakPtr<ExportedObjectManager>& object_manager)
    : options_{options},
      dbus_object_(object_manager.get(),
                   object_manager->GetBus(),
                   org::chromium::Buffet::ManagerAdaptor::GetObjectPath()) {}

Manager::~Manager() {}

void Manager::Start(AsyncEventSequencer* sequencer) {
  RestartWeave(sequencer);

  dbus_adaptor_.RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(
      sequencer->GetHandler("Manager.RegisterAsync() failed.", true));
}

void Manager::RestartWeave(AsyncEventSequencer* sequencer) {
  Stop();

  task_runner_.reset(new TaskRunner{});
  config_.reset(new BuffetConfig{options_.config_options});
  http_client_.reset(new HttpTransportClient);
  shill_client_.reset(new ShillClient{dbus_object_.GetBus(),
                                      options_.device_whitelist,
                                      !options_.xmpp_enabled});
  shill_client_->AddConnectionChangedCallback(base::Bind(
      &Manager::OnConnectionStateChanged, weak_ptr_factory_.GetWeakPtr()));

  CreateDevice();
}

void Manager::CreateDevice() {
  if (device_)
    return;

  device_ = weave::Device::Create(
      config_.get(), task_runner_.get(), http_client_.get(),
      shill_client_.get(), nullptr, nullptr, shill_client_.get(), nullptr);

  LoadCommandDefinitions(options_.config_options, device_.get());
  LoadStateDefinitions(options_.config_options, device_.get());
  LoadStateDefaults(options_.config_options, device_.get());

  device_->AddSettingsChangedCallback(
      base::Bind(&Manager::OnConfigChanged, weak_ptr_factory_.GetWeakPtr()));

  command_dispatcher_.reset(
      new DBusCommandDispacher{dbus_object_.GetObjectManager(), device_.get()});

  device_->AddStateChangedCallback(
      base::Bind(&Manager::OnStateChanged, weak_ptr_factory_.GetWeakPtr()));

  device_->AddGcdStateChangedCallback(
      base::Bind(&Manager::OnGcdStateChanged, weak_ptr_factory_.GetWeakPtr()));

  device_->AddPairingChangedCallbacks(
      base::Bind(&Manager::OnPairingStart, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&Manager::OnPairingEnd, weak_ptr_factory_.GetWeakPtr()));
}

void Manager::Stop() {
  command_dispatcher_.reset();
  device_.reset();
  shill_client_.reset();
  http_client_.reset();
  config_.reset();
  task_runner_.reset();
}

// TODO(vitalybuka): Remove, it's just duplicate of property.
void Manager::CheckDeviceRegistered(
    DBusMethodResponsePtr<std::string> response) {
  LOG(INFO) << "Received call to Manager.CheckDeviceRegistered()";
  response->Return(dbus_adaptor_.GetDeviceId());
}

void Manager::RegisterDevice(DBusMethodResponsePtr<std::string> response,
                             const std::string& ticket_id) {
  LOG(INFO) << "Received call to Manager.RegisterDevice()";

  device_->Register(ticket_id, base::Bind(&Manager::RegisterDeviceDone,
                                          weak_ptr_factory_.GetWeakPtr(),
                                          base::Passed(&response)));
}

void Manager::RegisterDeviceDone(DBusMethodResponsePtr<std::string> response,
                                 weave::ErrorPtr error) {
  if (error) {
    brillo::ErrorPtr cros_error;
    ConvertError(*error, &cros_error);
    return response->ReplyWithError(cros_error.get());
  }
  LOG(INFO) << "Device registered: " << device_->GetSettings().cloud_id;
  response->Return(device_->GetSettings().cloud_id);
}

void Manager::UpdateState(DBusMethodResponsePtr<> response,
                          const brillo::VariantDictionary& property_set) {
  brillo::ErrorPtr brillo_error;
  auto properties =
      DictionaryFromDBusVariantDictionary(property_set, &brillo_error);
  if (!properties)
    return response->ReplyWithError(brillo_error.get());

  weave::ErrorPtr error;
  if (!device_->SetStateProperties(*properties, &error)) {
    ConvertError(*error, &brillo_error);
    return response->ReplyWithError(brillo_error.get());
  }
  response->Return();
}

bool Manager::GetState(brillo::ErrorPtr* error, std::string* state) {
  auto json = device_->GetState();
  CHECK(json);
  base::JSONWriter::WriteWithOptions(
      *json, base::JSONWriter::OPTIONS_PRETTY_PRINT, state);
  return true;
}

void Manager::SetXmppChannel(DBusMethodResponsePtr<> response,
                             const std::string& channel) {
  device_->SetXmppChannel(channel);
  response->Return();
}

void Manager::AddCommand(DBusMethodResponsePtr<std::string> response,
                         const std::string& json_command) {
  auto value = base::JSONReader::ReadAndReturnValueWithError(
      json_command, base::JSON_PARSE_RFC);
  const base::DictionaryValue* command{nullptr};
  if (value.error_code != base::JSONReader::JSON_NO_ERROR ||
      !value.value->GetAsDictionary(&command)) {
    return response->ReplyWithError(FROM_HERE, brillo::errors::json::kDomain,
                                    brillo::errors::json::kParseError,
                                    value.error_message);
  }

  std::string id;
  weave::ErrorPtr error;
  if (!device_->AddCommand(*command, &id, &error)) {
    brillo::ErrorPtr brillo_error;
    ConvertError(*error, &brillo_error);
    return response->ReplyWithError(brillo_error.get());
  }

  response->Return(id);
}

std::string Manager::TestMethod(const std::string& message) {
  LOG(INFO) << "Received call to test method: " << message;
  return message;
}

bool Manager::UpdateDeviceInfo(brillo::ErrorPtr* brillo_error,
                               const std::string& name,
                               const std::string& description,
                               const std::string& location) {
  base::DictionaryValue command;
  command.SetString("name", "base.updateDeviceInfo");
  std::unique_ptr<base::DictionaryValue> parameters{new base::DictionaryValue};
  parameters->SetString("name", name);
  parameters->SetString("description", description);
  parameters->SetString("location", location);
  command.Set("parameters", std::move(parameters));

  std::string id;
  weave::ErrorPtr weave_error;
  if (!device_->AddCommand(command, &id, &weave_error)) {
    ConvertError(*weave_error, brillo_error);
    return false;
  }
  // TODO(vitalybuka): Wait for command DONE. Currently we know that command
  // will be handled inside of AddCommand. But this could be changed in future.
  CHECK_EQ(device_->GetSettings().name, name);
  CHECK_EQ(device_->GetSettings().description, description);
  CHECK_EQ(device_->GetSettings().location, location);
  return true;
}

bool Manager::UpdateServiceConfig(brillo::ErrorPtr* brillo_error,
                                  const std::string& client_id,
                                  const std::string& client_secret,
                                  const std::string& api_key,
                                  const std::string& oauth_url,
                                  const std::string& service_url) {
  if (!dbus_adaptor_.GetDeviceId().empty()) {
    brillo::Error::AddTo(brillo_error, FROM_HERE, kErrorDomain,
                         "already_registered",
                         "Unable to change config for registered device");
    return false;
  }

  options_.config_options.client_id = client_id;
  options_.config_options.client_secret = client_secret;
  options_.config_options.api_key = api_key;
  options_.config_options.oauth_url = oauth_url;
  options_.config_options.service_url = service_url;

  scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());
  RestartWeave(sequencer.get());
  return true;
}

void Manager::OnStateChanged() {
  auto state = device_->GetState();
  CHECK(state);
  std::string json;
  base::JSONWriter::WriteWithOptions(
      *state, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  dbus_adaptor_.SetState(json);
}

void Manager::OnGcdStateChanged(weave::GcdState state) {
  dbus_adaptor_.SetStatus(weave::EnumToString(state));
}

void Manager::OnConfigChanged(const weave::Settings& settings) {
  dbus_adaptor_.SetDeviceId(settings.cloud_id);
  dbus_adaptor_.SetOemName(settings.oem_name);
  dbus_adaptor_.SetModelName(settings.model_name);
  dbus_adaptor_.SetModelId(settings.model_id);
  dbus_adaptor_.SetName(settings.name);
  dbus_adaptor_.SetDescription(settings.description);
  dbus_adaptor_.SetLocation(settings.location);
}

void Manager::OnPairingStart(const std::string& session_id,
                             weave::PairingType pairing_type,
                             const std::vector<uint8_t>& code) {
  // For now, just overwrite the exposed PairInfo with
  // the most recent pairing attempt.
  dbus_adaptor_.SetPairingInfo(brillo::VariantDictionary{
      {kPairingSessionIdKey, session_id},
      {kPairingModeKey, weave::EnumToString(pairing_type)},
      {kPairingCodeKey, code},
  });
}

void Manager::OnPairingEnd(const std::string& session_id) {
  auto exposed_pairing_attempt = dbus_adaptor_.GetPairingInfo();
  auto it = exposed_pairing_attempt.find(kPairingSessionIdKey);
  if (it == exposed_pairing_attempt.end()) {
    return;
  }
  std::string exposed_session{it->second.TryGet<std::string>()};
  if (exposed_session == session_id) {
    dbus_adaptor_.SetPairingInfo(brillo::VariantDictionary{});
  }
}

void Manager::OnConnectionStateChanged() {
  if (shill_client_->GetIpAddress() != ip_address_) {
    if (!ip_address_.empty()) {
      LOG(INFO) << "IP address changed from " << ip_address_ << " to "
                << shill_client_->GetIpAddress();
      if (http_client_) {
        http_client_->SetLocalIpAddress(shill_client_->GetIpAddress());
      }
    }
    ip_address_ = shill_client_->GetIpAddress();
    return;
  }

  if (http_client_) {
    http_client_->SetOnline(shill_client_->GetConnectionState() ==
                            weave::provider::Network::State::kOnline);
  }
}

}  // namespace buffet
