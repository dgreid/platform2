// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/cloud_delegate.h"

#include <vector>

#include <base/bind.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/values.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>
#include <dbus/bus.h>

#include "buffet/dbus-proxies.h"
#include "privetd/constants.h"

namespace privetd {

namespace {

using chromeos::ErrorPtr;
using chromeos::VariantDictionary;
using org::chromium::Buffet::ManagerProxy;
using org::chromium::Buffet::ObjectManagerProxy;

const int kMaxSetupRetries = 5;
const int kFirstRetryTimeoutSec = 1;

class CloudDelegateImpl : public CloudDelegate {
 public:
  CloudDelegateImpl(const scoped_refptr<dbus::Bus>& bus,
                    bool is_gcd_setup_enabled)
      : object_manager_{bus}, is_gcd_setup_enabled_(is_gcd_setup_enabled) {
    object_manager_.SetManagerAddedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerAdded,
                   weak_factory_.GetWeakPtr()));
    object_manager_.SetManagerRemovedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerRemoved,
                   weak_factory_.GetWeakPtr()));
  }

  ~CloudDelegateImpl() override = default;

  bool GetModelId(std::string* id, chromeos::ErrorPtr* error) const override {
    if (!IsManagerReady(error))
      return false;
    if (manager_->model_id().size() != 5) {
      chromeos::Error::AddToPrintf(
          error, FROM_HERE, errors::kDomain, errors::kInvalidState,
          "Model ID is invalid: %s", manager_->model_id().c_str());
      return false;
    }
    *id = manager_->model_id();
    return true;
  }

  bool GetName(std::string* name, chromeos::ErrorPtr* error) const override {
    if (!IsManagerReady(error))
      return false;
    if (manager_->name().empty()) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kInvalidState, "Device name is empty");
      return false;
    }
    *name = manager_->name();
    return true;
  }

  std::string GetDescription() const override {
    return manager_ ? manager_->description() : std::string{};
  }

  std::string GetLocation() const override {
    return manager_ ? manager_->location() : std::string{};
  }

  void UpdateDeviceInfo(const std::string& name,
                        const std::string& description,
                        const std::string& location,
                        const base::Closure& success_callback,
                        const ErrorCallback& error_callback) override {
    chromeos::ErrorPtr error;
    if (!IsManagerReady(&error))
      return error_callback.Run(error.get());

    if (name.empty()) {
      chromeos::Error::AddTo(&error, FROM_HERE, errors::kDomain,
                             errors::kInvalidParams, "Empty device name");
      return error_callback.Run(error.get());
    }

    if (name == manager_->name() && description == manager_->description() &&
        location == manager_->location()) {
      return success_callback.Run();
    }

    manager_->UpdateDeviceInfoAsync(name, description, location,
                                    success_callback, error_callback);
  }

  std::string GetOemName() const override {
    return manager_ ? manager_->oem_name() : std::string{};
  }

  std::string GetModelName() const override {
    return manager_ ? manager_->model_name() : std::string{};
  }

  std::set<std::string> GetServices() const override {
    std::set<std::string> result;
    for (base::DictionaryValue::Iterator it{command_defs_}; !it.IsAtEnd();
         it.Advance()) {
      result.emplace(it.key());
    }
    return result;
  }

  AuthScope GetAnonymousMaxScope() const override {
    if (manager_) {
      AuthScope scope;
      if (StringToAuthScope(manager_->anonymous_access_role(), &scope))
        return scope;
    }
    return AuthScope::kGuest;
  }

  const ConnectionState& GetConnectionState() const override {
    return connection_state_;
  }

  const SetupState& GetSetupState() const override { return setup_state_; }

  bool Setup(const std::string& ticket_id,
             const std::string& user,
             chromeos::ErrorPtr* error) override {
    if (!is_gcd_setup_enabled_) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kSetupUnavailable,
                             "GCD setup unavailible");
      return false;
    }
    if (!object_manager_.GetManagerProxy()) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kDeviceBusy, "Buffet is not ready");
      return false;
    }
    if (setup_state_.IsStatusEqual(SetupState::kInProgress)) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kDeviceBusy, "Setup in progress");
      return false;
    }
    VLOG(1) << "GCD Setup started. ticket_id: " << ticket_id
            << ", user:" << user;
    setup_state_ = SetupState(SetupState::kInProgress);
    setup_weak_factory_.InvalidateWeakPtrs();
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, base::Bind(&CloudDelegateImpl::CallManagerRegisterDevice,
                              setup_weak_factory_.GetWeakPtr(), ticket_id, 0),
        base::TimeDelta::FromSeconds(kSetupDelaySeconds));
    // Return true because we tried setup.
    return true;
  }

  std::string GetCloudId() const override {
    return manager_ ? manager_->device_id() : std::string{};
  }

  const base::DictionaryValue& GetState() const override { return state_; }

  const base::DictionaryValue& GetCommandDef() const override {
    return command_defs_;
  }

  void AddCommand(const base::DictionaryValue& command,
                  const SuccessCallback& success_callback,
                  const ErrorCallback& error_callback) override {
    chromeos::ErrorPtr error;
    if (!IsManagerReady(&error))
      return error_callback.Run(error.get());

    std::string command_str;
    base::JSONWriter::Write(&command, &command_str);
    manager_->AddCommandAsync(
        command_str, base::Bind(&CloudDelegateImpl::OnAddCommandSucceeded,
                                weak_factory_.GetWeakPtr(), success_callback,
                                error_callback),
        error_callback);
  }

  void GetCommand(const std::string& id,
                  const SuccessCallback& success_callback,
                  const ErrorCallback& error_callback) override {
    chromeos::ErrorPtr error;
    if (!IsManagerReady(&error))
      return error_callback.Run(error.get());
    manager_->GetCommandAsync(
        id, base::Bind(&CloudDelegateImpl::OnGetCommandSucceeded,
                       weak_factory_.GetWeakPtr(), success_callback,
                       error_callback),
        error_callback);
  }

  void CancelCommand(const std::string& id,
                     const SuccessCallback& success_callback,
                     const ErrorCallback& error_callback) override {
    for (auto command : object_manager_.GetCommandInstances()) {
      if (command->id() == id) {
        return command->CancelAsync(
            base::Bind(&CloudDelegateImpl::GetCommand,
                       weak_factory_.GetWeakPtr(), id, success_callback,
                       error_callback),
            error_callback);
      }
    }
    chromeos::ErrorPtr error;
    chromeos::Error::AddToPrintf(&error, FROM_HERE, errors::kDomain,
                                 errors::kNotFound,
                                 "Command not found, ID='%s'", id.c_str());
    error_callback.Run(error.get());
  }

  void ListCommands(const SuccessCallback& success_callback,
                    const ErrorCallback& error_callback) override {
    std::vector<org::chromium::Buffet::CommandProxy*> commands{
        object_manager_.GetCommandInstances()};

    auto ids = std::make_shared<std::vector<std::string>>();
    for (auto command : commands)
      ids->push_back(command->id());

    GetNextCommand(ids, std::make_shared<base::ListValue>(), success_callback,
                   error_callback, base::DictionaryValue{});
  }

 private:
  void OnManagerAdded(ManagerProxy* manager) {
    manager_ = manager;
    manager_->SetPropertyChangedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerPropertyChanged,
                   weak_factory_.GetWeakPtr()));
    // Read all initial values.
    OnManagerPropertyChanged(manager, std::string{});
  }

  void OnManagerPropertyChanged(ManagerProxy* manager,
                                const std::string& property_name) {
    CHECK_EQ(manager_, manager);

    if (property_name.empty() || property_name == ManagerProxy::StatusName()) {
      OnStatusPropertyChanged();
    }

    if (property_name.empty() ||
        property_name == ManagerProxy::DeviceIdName() ||
        property_name == ManagerProxy::OemNameName() ||
        property_name == ManagerProxy::ModelNameName() ||
        property_name == ManagerProxy::ModelIdName() ||
        property_name == ManagerProxy::NameName() ||
        property_name == ManagerProxy::DescriptionName() ||
        property_name == ManagerProxy::LocationName() ||
        property_name == ManagerProxy::AnonymousAccessRoleName()) {
      NotifyOnDeviceInfoChanged();
    }

    if (property_name.empty() || property_name == ManagerProxy::StateName()) {
      OnStatePropertyChanged();
    }

    if (property_name.empty() ||
        property_name == ManagerProxy::CommandDefsName()) {
      OnCommandDefsPropertyChanged();
    }
  }

  void OnStatusPropertyChanged() {
    const std::string& status = manager_->status();
    if (status == "unconfigured") {
      connection_state_ = ConnectionState{ConnectionState::kUnconfigured};
    } else if (status == "connecting") {
      // TODO(vitalybuka): Find conditions for kOffline.
      connection_state_ = ConnectionState{ConnectionState::kConnecting};
    } else if (status == "connected") {
      connection_state_ = ConnectionState{ConnectionState::kOnline};
    } else {
      chromeos::ErrorPtr error;
      chromeos::Error::AddToPrintf(
          &error, FROM_HERE, errors::kDomain, errors::kInvalidState,
          "Unexpected buffet status: %s", status.c_str());
      connection_state_ = ConnectionState{std::move(error)};
    }
    NotifyOnDeviceInfoChanged();
  }

  void OnStatePropertyChanged() {
    state_.Clear();
    std::unique_ptr<base::Value> value{
        base::JSONReader::Read(manager_->state())};
    const base::DictionaryValue* state{nullptr};
    if (value && value->GetAsDictionary(&state))
      state_.MergeDictionary(state);
    NotifyOnStateChanged();
  }

  void OnCommandDefsPropertyChanged() {
    command_defs_.Clear();
    std::unique_ptr<base::Value> value{
        base::JSONReader::Read(manager_->command_defs())};
    const base::DictionaryValue* defs{nullptr};
    if (value && value->GetAsDictionary(&defs))
      command_defs_.MergeDictionary(defs);
    NotifyOnCommandDefsChanged();
  }

  void OnManagerRemoved(const dbus::ObjectPath& path) {
    manager_ = nullptr;
    connection_state_ = ConnectionState(ConnectionState::kDisabled);
    state_.Clear();
    command_defs_.Clear();
    NotifyOnDeviceInfoChanged();
    NotifyOnCommandDefsChanged();
    NotifyOnStateChanged();
  }

  void RetryRegister(const std::string& ticket_id,
                     int retries,
                     chromeos::Error* error) {
    if (retries >= kMaxSetupRetries) {
      chromeos::ErrorPtr new_error{error ? error->Clone() : nullptr};
      chromeos::Error::AddTo(&new_error, FROM_HERE, errors::kDomain,
                             errors::kInvalidState,
                             "Failed to register device");
      setup_state_ = SetupState{std::move(new_error)};
      return;
    }
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&CloudDelegateImpl::CallManagerRegisterDevice,
                   setup_weak_factory_.GetWeakPtr(), ticket_id, retries + 1),
        base::TimeDelta::FromSeconds(kFirstRetryTimeoutSec << retries));
  }

  void OnRegisterSuccess(const std::string& device_id) {
    VLOG(1) << "Device registered: " << device_id;
    setup_state_ = SetupState(SetupState::kSuccess);
  }

  void CallManagerRegisterDevice(const std::string& ticket_id, int retries) {
    auto manager_proxy = object_manager_.GetManagerProxy();
    if (!manager_proxy) {
      LOG(ERROR) << "Couldn't register because Buffet was offline.";
      RetryRegister(ticket_id, retries, nullptr);
      return;
    }
    manager_proxy->RegisterDeviceAsync(
        ticket_id, base::Bind(&CloudDelegateImpl::OnRegisterSuccess,
                              setup_weak_factory_.GetWeakPtr()),
        base::Bind(&CloudDelegateImpl::RetryRegister,
                   setup_weak_factory_.GetWeakPtr(), ticket_id, retries));
  }

  void OnAddCommandSucceeded(const SuccessCallback& success_callback,
                             const ErrorCallback& error_callback,
                             const std::string& id) {
    GetCommand(id, success_callback, error_callback);
  }

  void OnGetCommandSucceeded(const SuccessCallback& success_callback,
                             const ErrorCallback& error_callback,
                             const std::string& json_command) {
    std::unique_ptr<base::Value> value{base::JSONReader::Read(json_command)};
    base::DictionaryValue* command{nullptr};
    if (!value || !value->GetAsDictionary(&command)) {
      chromeos::ErrorPtr error;
      chromeos::Error::AddTo(&error, FROM_HERE, errors::kDomain,
                             errors::kInvalidFormat,
                             "Buffet returned invalid JSON");
      return error_callback.Run(error.get());
    }
    success_callback.Run(*command);
  }

  void GetNextCommandSkipError(
      const std::shared_ptr<std::vector<std::string>>& ids,
      const std::shared_ptr<base::ListValue>& commands,
      const SuccessCallback& success_callback,
      const ErrorCallback& error_callback,
      chromeos::Error*) {
    // Ignore if we can't get some commands. Maybe they were removed.
    GetNextCommand(ids, commands, success_callback, error_callback,
                   base::DictionaryValue{});
  }

  void GetNextCommand(const std::shared_ptr<std::vector<std::string>>& ids,
                      const std::shared_ptr<base::ListValue>& commands,
                      const SuccessCallback& success_callback,
                      const ErrorCallback& error_callback,
                      const base::DictionaryValue& json) {
    if (!json.empty())
      commands->Append(json.DeepCopy());

    if (ids->empty()) {
      base::DictionaryValue commands_json;
      commands_json.Set("commands", commands->DeepCopy());
      return success_callback.Run(commands_json);
    }

    std::string next_id = ids->back();
    ids->pop_back();

    auto on_success = base::Bind(&CloudDelegateImpl::GetNextCommand,
                                 weak_factory_.GetWeakPtr(), ids, commands,
                                 success_callback, error_callback);

    auto on_error = base::Bind(&CloudDelegateImpl::GetNextCommandSkipError,
                               weak_factory_.GetWeakPtr(), ids, commands,
                               success_callback, error_callback);

    GetCommand(next_id, on_success, on_error);
  }

  bool IsManagerReady(chromeos::ErrorPtr* error) const {
    if (!manager_) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kDeviceBusy, "Buffet is not ready");
      return false;
    }
    return true;
  }

  ObjectManagerProxy object_manager_;

  bool is_gcd_setup_enabled_{false};

  ManagerProxy* manager_{nullptr};

  // Primary state of GCD.
  ConnectionState connection_state_{ConnectionState::kDisabled};

  // State of the current or last setup.
  SetupState setup_state_{SetupState::kNone};

  // Current device state.
  base::DictionaryValue state_;

  // Current commands definitions.
  base::DictionaryValue command_defs_;

  // |setup_weak_factory_| tracks the lifetime of callbacks used in connection
  // with a particular invocation of Setup().
  base::WeakPtrFactory<CloudDelegateImpl> setup_weak_factory_{this};
  // |weak_factory_| tracks the lifetime of |this|.
  base::WeakPtrFactory<CloudDelegateImpl> weak_factory_{this};
};

}  // namespace

CloudDelegate::CloudDelegate() {
}

CloudDelegate::~CloudDelegate() {
}

// static
std::unique_ptr<CloudDelegate> CloudDelegate::CreateDefault(
    const scoped_refptr<dbus::Bus>& bus,
    bool is_gcd_setup_enabled) {
  return std::unique_ptr<CloudDelegateImpl>{
      new CloudDelegateImpl{bus, is_gcd_setup_enabled}};
}

void CloudDelegate::NotifyOnDeviceInfoChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnDeviceInfoChanged());
}

void CloudDelegate::NotifyOnCommandDefsChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnCommandDefsChanged());
}

void CloudDelegate::NotifyOnStateChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnStateChanged());
}

}  // namespace privetd
