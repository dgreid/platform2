// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUTHPOLICY_POLICY_DEVICE_POLICY_ENCODER_H_
#define AUTHPOLICY_POLICY_DEVICE_POLICY_ENCODER_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <components/policy/core/common/policy_types.h>

#include "authpolicy/policy/policy_encoder_helper.h"

namespace enterprise_management {
class ChromeDeviceSettingsProto;
}  // namespace enterprise_management

namespace policy {

// Connection types for the key::kDeviceUpdateAllowedConnectionTypes policy,
// exposed for tests. The int is actually enterprise_management::
// AutoUpdateSettingsProto_ConnectionType, but we don't want to include
// chrome_device_policy.pb.h here in this header.
extern const std::pair<const char*, int> kConnectionTypes[];
extern const size_t kConnectionTypesSize;

class RegistryDict;

// Private helper class used to convert a RegistryDict into a device policy
// protobuf. Don't include directly, use |preg_policy_encoder.h| instead,
class DevicePolicyEncoder {
 public:
  DevicePolicyEncoder(const RegistryDict* dict, const PolicyLevel level);

  // Toggles logging of policy values.
  void LogPolicyValues(bool enabled) { log_policy_values_ = enabled; }

  // Extracts all supported device policies from |dict| and puts them into
  // |policy|.
  void EncodePolicy(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;

 private:
  // Some logical grouping of policy encoding.
  void EncodeLoginPolicies(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;
  void EncodeNetworkPolicies(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;
  void EncodeAutoUpdatePolicies(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;
  void EncodeAccessibilityPolicies(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;
  void EncodeGenericPolicies(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;
  void EncodePoliciesWithPolicyOptions(
      enterprise_management::ChromeDeviceSettingsProto* policy) const;

  // Boolean policies.
  void EncodeBoolean(const char* policy_name,
                     const SetBooleanPolicyCallback& set_policy) const;

  // Boolean policies with PolicyOptions.
  void EncodeBooleanWithPolicyOptions(
      const char* policy_name,
      const SetBooleanPolicyCallback& set_policy) const;
  // Integer policies.
  void EncodeInteger(const char* policy_name,
                     const SetIntegerPolicyCallback& set_policy) const;
  // Integer in range policies.
  void EncodeIntegerInRange(const char* policy_name,
                            int range_min,
                            int range_max,
                            const SetIntegerPolicyCallback& set_policy) const;
  // String policies.
  void EncodeString(const char* policy_name,
                    const SetStringPolicyCallback& set_policy) const;

  // String list policies are a little different. Unlike the basic types they
  // are not stored as registry value, but as registry key with values 1, 2, ...
  // for the entries.
  void EncodeStringList(const char* policy_name,
                        const SetStringListPolicyCallback& set_policy) const;

  // Prints out an error message if the |policy_name| is contained in the
  // registry dictionary. Use this for unsupported policies.
  void HandleUnsupported(const char* policy_name) const;

 private:
  const RegistryDict* dict_ = nullptr;
  const PolicyLevel level_;
  bool log_policy_values_ = false;
};

}  // namespace policy

#endif  // AUTHPOLICY_POLICY_DEVICE_POLICY_ENCODER_H_
