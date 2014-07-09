// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_policy_service.h"

#include <secmodt.h>

#include <vector>

#include <base/files/file_path.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop_proxy.h>
#include <chromeos/switches/chrome_switches.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>

#include "login_manager/chrome_device_policy.pb.h"
#include "login_manager/dbus_error_types.h"
#include "login_manager/device_management_backend.pb.h"
#include "login_manager/install_attributes.pb.h"
#include "login_manager/key_generator.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/owner_key_loss_mitigator.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_store.h"

namespace em = enterprise_management;

namespace login_manager {
using crypto::RSAPrivateKey;
using google::protobuf::RepeatedPtrField;

namespace {

// Returns true if |policy| was not pushed by an enterprise.
bool IsConsumerPolicy(const em::PolicyFetchResponse& policy) {
  em::PolicyData poldata;
  if (!policy.has_policy_data() ||
      !poldata.ParseFromString(policy.policy_data())) {
    return false;
  }
  return !poldata.has_request_token() && poldata.has_username();
}

const char kInstallAttributesPath[] = "/home/.shadow/install_attributes.pb";

}  // namespace

// static
const char DevicePolicyService::kPolicyPath[] = "/var/lib/whitelist/policy";
// static
const char DevicePolicyService::kSerialRecoveryFlagFile[] =
    "/var/lib/enterprise_serial_number_recovery";
// static
const char DevicePolicyService::kDevicePolicyType[] = "google/chromeos/device";
// static
const char DevicePolicyService::kAttrEnterpriseMode[] = "enterprise.mode";
// static
const char DevicePolicyService::kEnterpriseDeviceMode[] = "enterprise";

DevicePolicyService::~DevicePolicyService() {
}

// static
DevicePolicyService* DevicePolicyService::Create(
    LoginMetrics* metrics,
    PolicyKey* owner_key,
    OwnerKeyLossMitigator* mitigator,
    NssUtil* nss,
    const scoped_refptr<base::MessageLoopProxy>& main_loop) {
  return new DevicePolicyService(
      base::FilePath(kSerialRecoveryFlagFile),
      base::FilePath(kPolicyPath),
      base::FilePath(kInstallAttributesPath),
      scoped_ptr<PolicyStore>(
          new PolicyStore(base::FilePath(kPolicyPath))),
      owner_key,
      main_loop,
      metrics,
      mitigator,
      nss);
}

bool DevicePolicyService::CheckAndHandleOwnerLogin(
    const std::string& current_user,
    PK11SlotInfo* slot,
    bool* is_owner,
    Error* error) {
  // Record metrics around consumer usage of user whitelisting.
  if (IsConsumerPolicy(store()->Get()))
    metrics_->SendConsumerAllowsNewUsers(PolicyAllowsNewUsers(store()->Get()));

  // If the current user is the owner, and isn't whitelisted or set as the owner
  // in the settings blob, then do so.
  scoped_ptr<RSAPrivateKey> signing_key(
      GetOwnerKeyForGivenUser(key()->public_key_der(), slot, error));
  if (signing_key.get()) {
    if (!StoreOwnerProperties(current_user, signing_key.get(), error))
      return false;
  }

  // Now, the flip side...if we believe the current user to be the owner based
  // on the user field in policy, and she DOESN'T have the private half of the
  // public key, we must mitigate.
  *is_owner = GivenUserIsOwner(current_user);
  if (*is_owner && !signing_key.get()) {
    if (!mitigator_->Mitigate(current_user))
      return false;
  }
  return true;
}

bool DevicePolicyService::ValidateAndStoreOwnerKey(
    const std::string& current_user,
    const std::string& buf,
    PK11SlotInfo* slot) {
  std::vector<uint8> pub_key;
  NssUtil::BlobFromBuffer(buf, &pub_key);

  Error error;
  scoped_ptr<RSAPrivateKey> signing_key(
      GetOwnerKeyForGivenUser(pub_key, slot, &error));
  if (!signing_key.get())
    return false;

  if (mitigator_->Mitigating()) {
    // Mitigating: Depending on whether the public key is still present, either
    // clobber or populate regularly.
    if (!(key()->IsPopulated() ? key()->ClobberCompromisedKey(pub_key)
                               : key()->PopulateFromBuffer(pub_key))) {
      return false;
    }
  } else {
    // Not mitigating, so regular key population should work.
    if (!key()->PopulateFromBuffer(pub_key))
      return false;
    // Clear policy in case we're re-establishing ownership.
    store()->Set(em::PolicyFetchResponse());
  }

  if (StoreOwnerProperties(current_user, signing_key.get(), &error)) {
    PersistKey();
    PersistPolicy();
  } else {
    LOG(WARNING) << "Could not immediately store owner properties in policy";
  }
  return true;
}

DevicePolicyService::DevicePolicyService(
    const base::FilePath& serial_recovery_flag_file,
    const base::FilePath& policy_file,
    const base::FilePath& install_attributes_file,
    scoped_ptr<PolicyStore> policy_store,
    PolicyKey* policy_key,
    const scoped_refptr<base::MessageLoopProxy>& main_loop,
    LoginMetrics* metrics,
    OwnerKeyLossMitigator* mitigator,
    NssUtil* nss)
    : PolicyService(policy_store.Pass(), policy_key, main_loop),
      serial_recovery_flag_file_(serial_recovery_flag_file),
      policy_file_(policy_file),
      install_attributes_file_(install_attributes_file),
      metrics_(metrics),
      mitigator_(mitigator),
      nss_(nss) {
}

bool DevicePolicyService::KeyMissing() {
  return key()->HaveCheckedDisk() && !key()->IsPopulated();
}

bool DevicePolicyService::Mitigating() {
  return mitigator_->Mitigating();
}

bool DevicePolicyService::Initialize() {
  bool key_success = key()->PopulateFromDiskIfPossible();
  if (!key_success)
    LOG(ERROR) << "Failed to load device policy key from disk.";

  bool policy_success = store()->LoadOrCreate();
  if (!policy_success)
    LOG(WARNING) << "Failed to load device policy data, continuing anyway.";

  if (!key_success && policy_success && store()->Get().has_new_public_key()) {
      LOG(WARNING) << "Recovering missing owner key from policy blob!";
      std::vector<uint8> pub_key;
      NssUtil::BlobFromBuffer(store()->Get().new_public_key(), &pub_key);
      key_success = key()->PopulateFromBuffer(pub_key);
      if (key_success)
        PersistKey();
  }

  ReportPolicyFileMetrics(key_success, policy_success);
  UpdateSerialNumberRecoveryFlagFile();
  return key_success;
}

bool DevicePolicyService::Store(const uint8* policy_blob,
                                uint32 len,
                                Completion* completion,
                                int flags) {
  bool result = PolicyService::Store(policy_blob, len, completion, flags);

  if (result) {
    UpdateSerialNumberRecoveryFlagFile();

    // Flush the settings cache, the next read will decode the new settings.
    settings_.reset();
  }

  return result;
}

void DevicePolicyService::ReportPolicyFileMetrics(bool key_success,
                                                  bool policy_success) {
  LoginMetrics::PolicyFilesStatus status;
  if (!key_success) {  // Key load failed.
    status.owner_key_file_state = LoginMetrics::MALFORMED;
  } else {
    if (key()->IsPopulated()) {
      if (nss_->CheckPublicKeyBlob(key()->public_key_der()))
        status.owner_key_file_state = LoginMetrics::GOOD;
      else
        status.owner_key_file_state = LoginMetrics::MALFORMED;
    } else {
      status.owner_key_file_state = LoginMetrics::NOT_PRESENT;
    }
  }

  if (!policy_success) {
    status.policy_file_state = LoginMetrics::MALFORMED;
  } else {
    std::string serialized;
    if (!store()->Get().SerializeToString(&serialized))
      status.policy_file_state = LoginMetrics::MALFORMED;
    else if (serialized == "")
      status.policy_file_state = LoginMetrics::NOT_PRESENT;
    else
      status.policy_file_state = LoginMetrics::GOOD;
  }

  if (store()->DefunctPrefsFilePresent())
    status.defunct_prefs_file_state = LoginMetrics::GOOD;

  metrics_->SendPolicyFilesStatus(status);
}

std::vector<std::string> DevicePolicyService::GetStartUpFlags() {
  std::vector<std::string> policy_args;
  const em::ChromeDeviceSettingsProto& policy = GetSettings();
  if (policy.has_start_up_flags()) {
    const em::StartUpFlagsProto& flags_proto = policy.start_up_flags();
    const RepeatedPtrField<std::string>& flags = flags_proto.flags();
    policy_args.push_back(
        std::string("--").append(chromeos::switches::kPolicySwitchesBegin));
    for (RepeatedPtrField<std::string>::const_iterator it = flags.begin();
         it != flags.end(); ++it) {
      std::string flag(*it);
      // Ignore empty flags.
      if (flag.empty() || flag == "-" || flag == "--")
        continue;
      // Check if the flag doesn't start with proper prefix and add it.
      if (flag.length() <= 1 || flag[0] != '-')
        flag = std::string("--").append(flag);
      policy_args.push_back(flag);
    }
    policy_args.push_back(
        std::string("--").append(chromeos::switches::kPolicySwitchesEnd));
  }
  return policy_args;
}

const em::ChromeDeviceSettingsProto& DevicePolicyService::GetSettings() {
  if (!settings_.get()) {
    settings_.reset(new em::ChromeDeviceSettingsProto());

    em::PolicyData policy_data;
    if (!policy_data.ParseFromString(store()->Get().policy_data()) ||
        !settings_->ParseFromString(policy_data.policy_value())) {
      LOG(ERROR) << "Failed to parse device settings, using empty defaults.";
    }
  }

  return *settings_;
}

// static
bool DevicePolicyService::PolicyAllowsNewUsers(
    const em::PolicyFetchResponse& policy) {
  em::PolicyData poldata;
  if (!policy.has_policy_data() ||
      !poldata.ParseFromString(policy.policy_data())) {
    return false;
  }
  em::ChromeDeviceSettingsProto polval;
  if (!poldata.has_policy_type() ||
      poldata.policy_type() != DevicePolicyService::kDevicePolicyType ||
      !poldata.has_policy_value() ||
      !polval.ParseFromString(poldata.policy_value())) {
    return false;
  }
  // Explicitly states that new users are allowed.
  bool explicitly_allowed = (polval.has_allow_new_users() &&
                             polval.allow_new_users().allow_new_users());
  // Doesn't state that new users are allowed, but also doesn't have a
  // non-empty whitelist.
  bool not_disallowed =
      !polval.has_allow_new_users() &&
      !(polval.has_user_whitelist() &&
        polval.user_whitelist().user_whitelist_size() > 0);
  // States that new users are not allowed, but doesn't specify a whitelist.
  // So, we fail open. Such policies are the result of a long-fixed bug, but
  // we're not certain all users ever got migrated.
  bool failed_open =
      polval.has_allow_new_users() &&
      !polval.allow_new_users().allow_new_users() &&
      !polval.has_user_whitelist();

  return explicitly_allowed || not_disallowed || failed_open;
}

bool DevicePolicyService::StoreOwnerProperties(const std::string& current_user,
                                               RSAPrivateKey* signing_key,
                                               Error* error) {
  CHECK(signing_key);
  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData poldata;
  if (policy.has_policy_data()) {
    poldata.ParseFromString(policy.policy_data());
    // TODO(cmasone): remove once http://crbug.com/355664 is fixed.
    LOG(INFO) << "Loading existing policy.";
  }
  em::ChromeDeviceSettingsProto polval;
  if (poldata.has_policy_type() &&
      poldata.policy_type() == kDevicePolicyType) {
    if (poldata.has_policy_value()) {
      polval.ParseFromString(poldata.policy_value());
      // TODO(cmasone): remove once http://crbug.com/355664 is fixed.
      LOG(INFO) << "Loading existing settings from policy.";
    }
  } else {
    poldata.set_policy_type(kDevicePolicyType);
  }
  // If there existed some device policy, we've got it now!
  // Update the UserWhitelistProto inside the ChromeDeviceSettingsProto we made.
  em::UserWhitelistProto* whitelist_proto = polval.mutable_user_whitelist();
  bool on_list = false;
  const RepeatedPtrField<std::string>& whitelist =
      whitelist_proto->user_whitelist();
  for (RepeatedPtrField<std::string>::const_iterator it = whitelist.begin();
       it != whitelist.end();
       ++it) {
    if (current_user == *it) {
      on_list = true;
      break;
    }
  }

  if (poldata.has_username() && poldata.username() == current_user &&
      on_list &&
      key()->Equals(policy.new_public_key())) {
    // TODO(cmasone): remove once http://crbug.com/355664 is fixed.
    LOG(INFO) << "Leaving settings unchanged; user is owner and on whitelist.";
    if (!polval.has_allow_new_users()) {
      LOG(INFO) << "No allow_new_users setting!";
    } else {
      LOG(INFO) << "Allow new users is "
                << polval.allow_new_users().allow_new_users();
    }

    return true;  // No changes are needed.
  }
  if (!on_list) {
    // Add owner to the whitelist and turn off whitelist enforcement if it is
    // currently not explicitly turned on or off.
    whitelist_proto->add_user_whitelist(current_user);
    if (!polval.has_allow_new_users())
      polval.mutable_allow_new_users()->set_allow_new_users(true);
  }
  poldata.set_username(current_user);

  // TODO(cmasone): remove once http://crbug.com/355664 is fixed.
  LOG(INFO) << "Settings updated. User " << (on_list ? "was" : "wasn't")
            << " on the whitelist.";
  if (!polval.has_allow_new_users()) {
    LOG(INFO) << "No allow_new_users setting!";
  } else {
    LOG(INFO) << "Allow new users is "
              << polval.allow_new_users().allow_new_users();
  }


  // We have now updated the whitelist and owner setting in |polval|.
  // We need to put it into |poldata|, serialize that, sign it, and
  // write it back.
  poldata.set_policy_value(polval.SerializeAsString());
  std::string new_data = poldata.SerializeAsString();
  std::vector<uint8> sig;
  const uint8* data = reinterpret_cast<const uint8*>(new_data.c_str());
  if (!nss_->Sign(data, new_data.length(), &sig, signing_key)) {
    const char err_msg[] = "Could not sign policy containing new owner data.";
    if (error) {
      LOG(WARNING) << err_msg;
      error->Set(dbus_error::kPubkeySetIllegal, err_msg);
    } else {
      LOG(ERROR) << err_msg;
    }
    return false;
  }

  em::PolicyFetchResponse new_policy;
  new_policy.CheckTypeAndMergeFrom(policy);
  new_policy.set_policy_data(new_data);
  new_policy.set_policy_data_signature(
      std::string(reinterpret_cast<const char*>(&sig[0]), sig.size()));
  const std::vector<uint8>& key_der = key()->public_key_der();
  new_policy.set_new_public_key(
      std::string(reinterpret_cast<const char*>(&key_der[0]), key_der.size()));
  store()->Set(new_policy);
  return true;
}

RSAPrivateKey* DevicePolicyService::GetOwnerKeyForGivenUser(
    const std::vector<uint8>& key,
    PK11SlotInfo* slot,
    Error* error) {
  RSAPrivateKey* result = nss_->GetPrivateKeyForUser(key, slot);
  if (!result) {
    const char msg[] = "Could not verify that owner key belongs to this user.";
    LOG(WARNING) << msg;
    if (error)
      error->Set(dbus_error::kPubkeySetIllegal, msg);
    return NULL;
  }
  return result;
}

bool DevicePolicyService::GivenUserIsOwner(const std::string& current_user) {
  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData poldata;
  if (!policy.has_policy_data())
    return false;
  if (poldata.ParseFromString(policy.policy_data())) {
    return (!poldata.has_request_token() &&
            poldata.has_username() &&
            poldata.username() == current_user);
  }
  return false;
}

void DevicePolicyService::UpdateSerialNumberRecoveryFlagFile() {
  bool recovery_needed = false;
  int64 policy_size = 0;
  if (!base::GetFileSize(base::FilePath(policy_file_), &policy_size) ||
      !policy_size) {
    LOG(WARNING) << "Policy file empty or missing.";
    recovery_needed = true;
  }

  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData policy_data;
  bool policy_parsed =
      policy.has_policy_data() &&
      policy_data.ParseFromString(policy.policy_data());

  if (policy_parsed && !policy_data.request_token().empty() &&
      policy_data.valid_serial_number_missing()) {
    LOG(WARNING) << "Serial number missing flag encountered in policy data.";
    recovery_needed = true;
  }

  // Expose serial number on "spontaneously unenrolled" devices to allow them to
  // go through the enrollment flow again:  https://crbug.com/389481
  if (policy_parsed && policy_data.request_token().empty()) {
    std::string contents;
    base::ReadFileToString(install_attributes_file_, &contents);
    cryptohome::SerializedInstallAttributes install_attributes;
    if (install_attributes.ParseFromString(contents)) {
      for (int i = 0; i < install_attributes.attributes_size(); ++i) {
        const cryptohome::SerializedInstallAttributes_Attribute &attribute =
            install_attributes.attributes(i);
        if (attribute.name() == kAttrEnterpriseMode &&
            attribute.value() == kEnterpriseDeviceMode) {
          LOG(WARNING) << "DM token missing on enrolled device.";
          recovery_needed = true;
          break;
        }
      }
    }
  }

  // We need to recreate the machine info file if |valid_serial_number_missing|
  // is set to true in the protobuf or if the policy file is missing or empty
  // and we need to re-enroll.
  // TODO(pastarmovj,wad): Only check if file is missing if enterprise enrolled.
  // To check that we need to access the install attributes here.
  // For more info see: http://crosbug.com/31537
  if (recovery_needed) {
    if (base::WriteFile(serial_recovery_flag_file_, NULL, 0) != 0) {
      PLOG(WARNING) << "Failed to write "
                    << serial_recovery_flag_file_.value();
    }
  } else {
    if (!base::DeleteFile(serial_recovery_flag_file_, false)) {
      PLOG(WARNING) << "Failed to delete "
                    << serial_recovery_flag_file_.value();
    }
  }
}

}  // namespace login_manager
