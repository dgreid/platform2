// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_

#include <set>
#include <string>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/fake_variable.h"

namespace chromeos_update_manager {

// Fake implementation of the DevicePolicyProvider base class.
class FakeDevicePolicyProvider : public DevicePolicyProvider {
 public:
  FakeDevicePolicyProvider() {}

  FakeVariable<bool>* var_device_policy_is_loaded() override {
    return &var_device_policy_is_loaded_;
  }

  FakeVariable<std::string>* var_release_channel() override {
    return &var_release_channel_;
  }

  FakeVariable<bool>* var_release_channel_delegated() override {
    return &var_release_channel_delegated_;
  }

  FakeVariable<bool>* var_update_disabled() override {
    return &var_update_disabled_;
  }

  FakeVariable<std::string>* var_target_version_prefix() override {
    return &var_target_version_prefix_;
  }

  FakeVariable<base::TimeDelta>* var_scatter_factor() override {
    return &var_scatter_factor_;
  }

  FakeVariable<std::set<ConnectionType>>*
      var_allowed_connection_types_for_update() override {
    return &var_allowed_connection_types_for_update_;
  }

  FakeVariable<std::string>* var_owner() override {
    return &var_owner_;
  }

  FakeVariable<bool>* var_http_downloads_enabled() override {
    return &var_http_downloads_enabled_;
  }

  FakeVariable<bool>* var_au_p2p_enabled() override {
    return &var_au_p2p_enabled_;
  }

 private:
  FakeVariable<bool> var_device_policy_is_loaded_{
      "policy_is_loaded", kVariableModePoll};
  FakeVariable<std::string> var_release_channel_{
      "release_channel", kVariableModePoll};
  FakeVariable<bool> var_release_channel_delegated_{
      "release_channel_delegated", kVariableModePoll};
  FakeVariable<bool> var_update_disabled_{
      "update_disabled", kVariableModePoll};
  FakeVariable<std::string> var_target_version_prefix_{
      "target_version_prefix", kVariableModePoll};
  FakeVariable<base::TimeDelta> var_scatter_factor_{
      "scatter_factor", kVariableModePoll};
  FakeVariable<std::set<ConnectionType>>
      var_allowed_connection_types_for_update_{
          "allowed_connection_types_for_update", kVariableModePoll};
  FakeVariable<std::string> var_owner_{"owner", kVariableModePoll};
  FakeVariable<bool> var_http_downloads_enabled_{
      "http_downloads_enabled", kVariableModePoll};
  FakeVariable<bool> var_au_p2p_enabled_{"au_p2p_enabled", kVariableModePoll};

  DISALLOW_COPY_AND_ASSIGN(FakeDevicePolicyProvider);
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_
