// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/cloud_delegate.h"

#include <base/bind.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/values.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>
#include <dbus/bus.h>

#include "buffet/dbus-proxies.h"
#include "privetd/constants.h"
#include "privetd/device_delegate.h"
#include "privetd/peerd_client.h"

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
  CloudDelegateImpl(const scoped_refptr<dbus::Bus>& bus, DeviceDelegate* device)
      : object_manager_{bus}, device_{device} {
    object_manager_.SetManagerAddedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerAdded,
                   weak_factory_.GetWeakPtr()));
    object_manager_.SetManagerRemovedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerRemoved,
                   weak_factory_.GetWeakPtr()));
  }

  ~CloudDelegateImpl() override = default;

  const ConnectionState& GetConnectionState() const override { return state_; }

  const SetupState& GetSetupState() const override { return setup_state_; }

  bool Setup(const std::string& ticket_id,
             const std::string& user,
             chromeos::ErrorPtr* error) override {
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

  std::string GetCloudId() const override { return cloud_id_; }

  const base::DictionaryValue& GetCommandDef() const override {
    return command_defs_;
  }

  void GetCommand(const std::string& id,
                  const SuccessCallback& success_callback,
                  const ErrorCallback& error_callback) override {
    chromeos::ErrorPtr error;
    ManagerProxy* manager = GetManagerProxy(&error);
    if (!manager)
      return error_callback.Run(error.get());
    manager->GetCommandAsync(
        id, base::Bind(&CloudDelegateImpl::OnGetCommandSucceeded,
                       weak_factory_.GetWeakPtr(), success_callback,
                       error_callback),
        error_callback);
  }

 private:
  void OnManagerAdded(ManagerProxy* manager) {
    manager->SetPropertyChangedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerPropertyChanged,
                   weak_factory_.GetWeakPtr()));
    // Read all initial values.
    OnManagerPropertyChanged(manager, std::string{});
  }

  void OnManagerPropertyChanged(ManagerProxy* manager,
                                const std::string& property_name) {
    if (property_name.empty() || property_name == ManagerProxy::StatusName()) {
      OnStatusPropertyChanged(manager);
    }

    if (property_name.empty() ||
        property_name == ManagerProxy::DeviceIdName()) {
      OnDeviceIdPropertyChanged(manager);
    }

    if (property_name.empty() ||
        property_name == ManagerProxy::CommandDefsName()) {
      OnCommandDefsPropertyChanged(manager);
    }
  }

  void OnStatusPropertyChanged(ManagerProxy* manager) {
    const std::string& status = manager->status();
    if (status == "unconfigured") {
      state_ = ConnectionState{ConnectionState::kUnconfigured};
    } else if (status == "connecting") {
      // TODO(vitalybuka): Find conditions for kOffline.
      state_ = ConnectionState{ConnectionState::kConnecting};
    } else if (status == "connected") {
      state_ = ConnectionState{ConnectionState::kOnline};
    } else {
      chromeos::ErrorPtr error;
      chromeos::Error::AddToPrintf(
          &error, FROM_HERE, errors::kDomain, errors::kInvalidState,
          "Unexpected buffet status: %s", status.c_str());
      state_ = ConnectionState{std::move(error)};
    }
    NotifyOnRegistrationChanged();
  }

  void OnDeviceIdPropertyChanged(ManagerProxy* manager) {
    cloud_id_ = manager->device_id();
    NotifyOnRegistrationChanged();
  }

  void OnCommandDefsPropertyChanged(ManagerProxy* manager) {
    command_defs_.Clear();
    std::unique_ptr<base::Value> value{
        base::JSONReader::Read(manager->command_defs())};
    base::DictionaryValue* defs{nullptr};
    if (value && value->GetAsDictionary(&defs))
      command_defs_.MergeDictionary(defs);
    NotifyOnCommandDefsChanged();
  }

  void OnManagerRemoved(const dbus::ObjectPath& path) {
    state_ = ConnectionState(ConnectionState::kDisabled);
    cloud_id_.clear();
    command_defs_.Clear();
    NotifyOnRegistrationChanged();
    NotifyOnCommandDefsChanged();
  }

  void RetryRegister(const std::string& ticket_id,
                     int retries,
                     chromeos::Error* error) {
    if (retries >= kMaxSetupRetries) {
      chromeos::ErrorPtr new_error;
      if (error) {
        chromeos::Error::AddTo(&new_error, FROM_HERE, error->GetDomain(),
                               error->GetCode(), error->GetMessage());
      } else {
        chromeos::Error::AddTo(&new_error, FROM_HERE, errors::kDomain,
                               errors::kInvalidState,
                               "Failed to register device");
      }
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
    VariantDictionary params{
        {"ticket_id", ticket_id},
        {"display_name", device_->GetName()},
        {"description", device_->GetDescription()},
        {"location", device_->GetLocation()},
        {"model_id", device_->GetModelId()},
    };
    manager_proxy->RegisterDeviceAsync(
        params,
        base::Bind(&CloudDelegateImpl::OnRegisterSuccess,
                   setup_weak_factory_.GetWeakPtr()),
        base::Bind(&CloudDelegateImpl::RetryRegister,
                   setup_weak_factory_.GetWeakPtr(),
                   ticket_id,
                   retries));
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
                             "Buffet returned invalid JSON.");
      return error_callback.Run(error.get());
    }
    success_callback.Run(*command);
  }

  ManagerProxy* GetManagerProxy(chromeos::ErrorPtr* error) {
    ManagerProxy* manager = object_manager_.GetManagerProxy();
    if (!manager) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::kDomain,
                             errors::kDeviceBusy, "Buffet is not ready.");
    }
    return manager;
  }

  ObjectManagerProxy object_manager_;

  DeviceDelegate* device_;

  // Primary state of GCD.
  ConnectionState state_{ConnectionState::kDisabled};

  // State of the current or last setup.
  SetupState setup_state_{SetupState::kNone};

  // Cloud ID if device is registered.
  std::string cloud_id_;

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
    DeviceDelegate* device) {
  return std::unique_ptr<CloudDelegateImpl>{new CloudDelegateImpl{bus, device}};
}

void CloudDelegate::NotifyOnRegistrationChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnRegistrationChanged());
}

void CloudDelegate::NotifyOnCommandDefsChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnCommandDefsChanged());
}

void CloudDelegate::NotifyOnStateChanged() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnStateChanged());
}

}  // namespace privetd
