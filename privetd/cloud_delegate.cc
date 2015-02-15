// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/cloud_delegate.h"

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>
#include <dbus/bus.h>

#include "buffet/dbus-proxies.h"
#include "privetd/constants.h"
#include "privetd/device_delegate.h"
#include "privetd/peerd_client.h"

namespace privetd {

namespace {

using chromeos::VariantDictionary;
using chromeos::ErrorPtr;

const int kMaxSetupRetries = 5;
const int kFirstRetryTimeoutMs = 100;
const char kBuffetStatus[] = "Status";

class CloudDelegateImpl : public CloudDelegate {
 public:
  CloudDelegateImpl(const scoped_refptr<dbus::Bus>& bus,
                    DeviceDelegate* device,
                    const base::Closure& on_changed)
      : object_manager_{bus}, device_{device}, on_changed_{on_changed} {
    object_manager_.SetManagerAddedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerAdded,
                   weak_factory_.GetWeakPtr()));
    object_manager_.SetManagerRemovedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerRemoved,
                   weak_factory_.GetWeakPtr()));
  }

  ~CloudDelegateImpl() override = default;

  ConnectionState GetConnectionState() const override { return state_; }

  SetupState GetSetupState() const override { return setup_state_; }

  bool Setup(const std::string& ticket_id, const std::string& user) override {
    if (setup_state_.status == SetupState::kInProgress ||
        GetManagerProxy() == nullptr)
      return false;
    VLOG(1) << "GCD Setup started. ticket_id: " << ticket_id
            << ", user:" << user;
    setup_state_ = SetupState(SetupState::kInProgress);
    cloud_id_.clear();
    setup_weak_factory_.InvalidateWeakPtrs();
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, base::Bind(&CloudDelegateImpl::CallManagerRegisterDevice,
                              setup_weak_factory_.GetWeakPtr(), ticket_id, 0),
        base::TimeDelta::FromSeconds(kSetupDelaySeconds));
    on_changed_.Run();
    // Return true because we tried setup.
    return true;
  }

  std::string GetCloudId() const override { return cloud_id_; }

 private:
  org::chromium::Buffet::ManagerProxy* GetManagerProxy() {
    if (!manager_path_.IsValid())
      return nullptr;
    return object_manager_.GetManagerProxy(manager_path_);
  }

  void OnManagerAdded(org::chromium::Buffet::ManagerProxy* manager) {
    manager_path_ = manager->GetObjectPath();
    manager->SetPropertyChangedCallback(
        base::Bind(&CloudDelegateImpl::OnManagerPropertyChanged,
                   weak_factory_.GetWeakPtr()));
    OnManagerPropertyChanged(manager, kBuffetStatus);
    // TODO(wiley) Get the device id (cloud_id_) here if we're online
  }

  void OnManagerPropertyChanged(org::chromium::Buffet::ManagerProxy* manager,
                                const std::string& property_name) {
    if (property_name != kBuffetStatus)
      return;
    std::string status{manager->status()};
    if (status == "offline")
      state_ = ConnectionState(ConnectionState::kOffline);
    else if (status == "cloud_error")
      state_ = ConnectionState(ConnectionState::kError);
    else if (status == "unregistered")
      state_ = ConnectionState(ConnectionState::kUnconfigured);
    else if (status == "registering")
      state_ = ConnectionState(ConnectionState::kConnecting);
    else if (status == "registered")
      state_ = ConnectionState(ConnectionState::kOnline);
    else
      state_ = ConnectionState(ConnectionState::kError);
    on_changed_.Run();
  }

  void OnManagerRemoved(const dbus::ObjectPath& path) {
    state_ = ConnectionState(ConnectionState::kOffline);
    manager_path_ = dbus::ObjectPath();
    on_changed_.Run();
  }

  void RetryRegister(const std::string& ticket_id,
                     int retries,
                     chromeos::Error* error) {
    if (retries >= kMaxSetupRetries) {
      setup_state_ = SetupState(Error::kServerError);
      return;
    }
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&CloudDelegateImpl::CallManagerRegisterDevice,
                   setup_weak_factory_.GetWeakPtr(), ticket_id, retries + 1),
        base::TimeDelta::FromSeconds(kFirstRetryTimeoutMs * (1 << retries)));
  }

  void OnRegisterSuccess(const std::string& device_id) {
    VLOG(1) << "Device registered: " << device_id;
    cloud_id_ = device_id;
    setup_state_ = SetupState(SetupState::kSuccess);
    on_changed_.Run();
  }

  void CallManagerRegisterDevice(const std::string& ticket_id, int retries) {
    auto manager_proxy = GetManagerProxy();
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

  org::chromium::Buffet::ObjectManagerProxy object_manager_;
  dbus::ObjectPath manager_path_;

  DeviceDelegate* device_;

  base::Closure on_changed_;

  // Primary state of GCD.
  ConnectionState state_{ConnectionState::kUnconfigured};

  // State of the current or last setup.
  SetupState setup_state_{SetupState::kNone};

  // Cloud ID if device is registered.
  std::string cloud_id_;

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
    DeviceDelegate* device,
    const base::Closure& on_changed) {
  return std::unique_ptr<CloudDelegateImpl>(
      new CloudDelegateImpl(bus, device, on_changed));
}

}  // namespace privetd
