// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/policy/user_policy_encoder.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <components/policy/core/common/registry_dict.h>

#include "authpolicy/policy/policy_encoder_helper.h"
#include "bindings/cloud_policy.pb.h"
#include "bindings/policy_constants.h"

namespace em = enterprise_management;

namespace policy {

UserPolicyEncoder::UserPolicyEncoder(const RegistryDict* dict,
                                     PolicyLevel level)
    : dict_(dict), level_(level) {}

void UserPolicyEncoder::EncodePolicy(em::CloudPolicySettings* policy) const {
  LOG_IF(INFO, log_policy_values_)
      << authpolicy::kColorPolicy << "User policy ("
      << (level_ == POLICY_LEVEL_RECOMMENDED ? "recommended" : "mandatory")
      << ")" << authpolicy::kColorReset;
  EncodeList(policy, kBooleanPolicyAccess, &UserPolicyEncoder::EncodeBoolean);
  EncodeList(policy, kIntegerPolicyAccess, &UserPolicyEncoder::EncodeInteger);
  EncodeList(policy, kStringPolicyAccess, &UserPolicyEncoder::EncodeString);
  EncodeList(policy, kStringListPolicyAccess,
             &UserPolicyEncoder::EncodeStringList);
}

void UserPolicyEncoder::EncodeBoolean(em::CloudPolicySettings* policy,
                                      const BooleanPolicyAccess* access) const {
  const char* policy_name = access->policy_key;

  base::Optional<bool> bool_value = EncodeBooleanPolicy(
      policy_name, GetValueFromDictCallback(dict_), log_policy_values_);
  if (bool_value) {
    // Create proto and set value.
    em::BooleanPolicyProto* proto = (policy->*access->mutable_proto_ptr)();
    DCHECK(proto);
    proto->set_value(bool_value.value());
    SetPolicyOptions(proto->mutable_policy_options(), level_);
  }
}

void UserPolicyEncoder::EncodeInteger(em::CloudPolicySettings* policy,
                                      const IntegerPolicyAccess* access) const {
  const char* policy_name = access->policy_key;

  base::Optional<int> int_value = EncodeIntegerInRangePolicy(
      policy_name, GetValueFromDictCallback(dict_),
      std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
      log_policy_values_);
  if (int_value) {
    // Create proto and set value.
    em::IntegerPolicyProto* proto = (policy->*access->mutable_proto_ptr)();
    DCHECK(proto);
    proto->set_value(int_value.value());
    SetPolicyOptions(proto->mutable_policy_options(), level_);
  }
}

void UserPolicyEncoder::EncodeString(em::CloudPolicySettings* policy,
                                     const StringPolicyAccess* access) const {
  const char* policy_name = access->policy_key;

  base::Optional<std::string> string_value = EncodeStringPolicy(
      policy_name, GetValueFromDictCallback(dict_), log_policy_values_);
  if (string_value) {
    // Create proto and set value.
    em::StringPolicyProto* proto = (policy->*access->mutable_proto_ptr)();
    DCHECK(proto);
    *proto->mutable_value() = string_value.value();
    SetPolicyOptions(proto->mutable_policy_options(), level_);
  }
}

void UserPolicyEncoder::EncodeStringList(
    em::CloudPolicySettings* policy,
    const StringListPolicyAccess* access) const {
  // Try to get policy key from dict.
  const char* policy_name = access->policy_key;
  const RegistryDict* key = dict_->GetKey(policy_name);
  if (!key)
    return;

  base::Optional<std::vector<std::string>> string_values =
      EncodeStringListPolicy(policy_name, GetValueFromDictCallback(key),
                             log_policy_values_);
  if (string_values) {
    // Create proto and set value.
    em::StringListPolicyProto* proto = (policy->*access->mutable_proto_ptr)();
    DCHECK(proto);
    em::StringList* proto_list = proto->mutable_value();
    DCHECK(proto_list);
    proto_list->clear_entries();
    for (const std::string& value : string_values.value())
      *proto_list->add_entries() = value;
    SetPolicyOptions(proto->mutable_policy_options(), level_);
  }
}

template <typename T_Access>
void UserPolicyEncoder::EncodeList(em::CloudPolicySettings* policy,
                                   const T_Access* access,
                                   Encoder<T_Access> encode) const {
  // Access lists are NULL-terminated.
  for (; access->policy_key && access->mutable_proto_ptr; ++access)
    (this->*encode)(policy, access);
}

}  // namespace policy
