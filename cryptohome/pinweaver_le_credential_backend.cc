// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/pinweaver_le_credential_backend.h"

#include <stdint.h>  // required by pinweaver_types.h

#include <algorithm>
#include <utility>

#include <base/logging.h>

#include <trunks/error_codes.h>
#include <trunks/tpm_utility.h>
#include <trunks/trunks_factory.h>

extern "C" {
#define __packed __attribute((packed))
#define __aligned(x) __attribute((aligned(x)))
#include <trunks/cr50_headers/pinweaver_types.h>
}

#include "cryptohome/pinweaver.pb.h"
#include "cryptohome/tpm2_impl.h"

namespace cryptohome {

namespace {

// Translates a pair of trunks error code and pinweaver status code to the
// appropriate LECredBackendError.
LECredBackendError ConvertStatus(const std::string& operation,
                                 trunks::TPM_RC result,
                                 int32_t pinweaver_status) {
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "TPM error on pinweaver " << operation
               << " operation: " << trunks::GetErrorString(result);
    return LE_TPM_ERROR_TPM_OP_FAILED;
  }

  if (pinweaver_status != 0 /* EC_SUCCESS */) {
    LOG(WARNING) << "Pinweaver " << operation << ": status "
                 << pinweaver_status;
  }

  switch (pinweaver_status) {
    case 0:  // EC_SUCCESS
      return LE_TPM_SUCCESS;
    case PW_ERR_LOWENT_AUTH_FAILED:
      return LE_TPM_ERROR_INVALID_LE_SECRET;
    case PW_ERR_RESET_AUTH_FAILED:
      return LE_TPM_ERROR_INVALID_RESET_SECRET;
    case PW_ERR_RATE_LIMIT_REACHED:
      return LE_TPM_ERROR_TOO_MANY_ATTEMPTS;
    case PW_ERR_PATH_AUTH_FAILED:
      return LE_TPM_ERROR_HASH_TREE_SYNC;
    // This could happen (by design) only if the device is hacked. Treat the
    // error as like an invalid PIN was provided.
    case PW_ERR_PCR_NOT_MATCH:
      return LE_TPM_ERROR_PCR_NOT_MATCH;
  }

  LOG(ERROR) << "Pinweaver error on pinweaver " << operation
             << " operation: " << pinweaver_status;
  return LE_TPM_ERROR_TPM_OP_FAILED;
}

std::string EncodeAuxHashes(const std::vector<std::vector<uint8_t>>& h_aux) {
  std::string result;
  result.reserve(h_aux.size() * PW_HASH_SIZE);
  for (const std::vector<uint8_t>& hash : h_aux) {
    CHECK_EQ(PW_HASH_SIZE, hash.size());
    result.append(hash.begin(), hash.end());
  }
  return result;
}

std::vector<uint8_t> StringToBlob(const std::string& str) {
  return std::vector<uint8_t>(str.begin(), str.end());
}

std::string BlobToString(const std::vector<uint8_t>& blob) {
  return std::string(blob.begin(), blob.end());
}

void ConvertPinWeaverLogToLELog(
    const std::vector<trunks::PinWeaverLogEntry>& orig_log,
    std::vector<cryptohome::LELogEntry>* log) {
  for (const auto& log_entry : orig_log) {
    cryptohome::LELogEntry entry;
    if (log_entry.has_insert_leaf()) {
      entry.type = LE_LOG_INSERT;
      entry.mac = StringToBlob(log_entry.insert_leaf().hmac());
    } else if (log_entry.has_remove_leaf()) {
      entry.type = LE_LOG_REMOVE;
    } else if (log_entry.has_auth()) {
      entry.type = LE_LOG_CHECK;
    } else if (log_entry.has_reset_tree()) {
      entry.type = LE_LOG_RESET;
    } else {
      entry.type = LE_LOG_INVALID;
    }
    entry.root = StringToBlob(log_entry.root());
    entry.label = log_entry.label();

    log->push_back(entry);
  }
}

}  // namespace

PinweaverLECredentialBackend::PinweaverLECredentialBackend(Tpm2Impl* tpm)
    : tpm_(tpm) {}

bool PinweaverLECredentialBackend::Reset(std::vector<uint8_t>* new_root) {
  return PerformPinweaverOperation(
      "Reset", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        trunks::TPM_RC result = tpm_utility->PinWeaverResetTree(
            protocol_version_, kBitsPerLevel, kLengthLabels / kBitsPerLevel,
            &pinweaver_status, &root);
        *new_root = StringToBlob(root);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::IsSupported() {
  return PerformPinweaverOperation(
      "IsSupported", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        trunks::TPM_RC result = tpm_utility->PinWeaverIsSupported(
            PW_PROTOCOL_VERSION, &protocol_version_);
        if (result == trunks::SAPI_RC_ABI_MISMATCH)
          result = tpm_utility->PinWeaverIsSupported(0, &protocol_version_);
        if (result == trunks::TPM_RC_SUCCESS)
          protocol_version_ = std::min(
              protocol_version_, static_cast<uint8_t>(PW_PROTOCOL_VERSION));

        return std::make_pair(result, 0 /* EC_SUCCESS */);
      });
}

bool PinweaverLECredentialBackend::InsertCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const std::map<uint32_t, uint32_t>& delay_schedule,
    const ValidPcrCriteria& valid_pcr_criteria,
    std::vector<uint8_t>* cred_metadata,
    std::vector<uint8_t>* mac,
    std::vector<uint8_t>* new_root) {
  trunks::ValidPcrCriteria pcr_criteria;
  if (protocol_version_ > 0) {
    for (ValidPcrValue value : valid_pcr_criteria) {
      trunks::ValidPcrValue* new_value = pcr_criteria.add_valid_pcr_values();
      new_value->set_bitmask(&value.bitmask, 2);
      new_value->set_digest(value.digest);
    }
  }
  return PerformPinweaverOperation(
      "InsertCredential", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        std::string cred_metadata_string;
        std::string mac_string;
        trunks::TPM_RC result = tpm_utility->PinWeaverInsertLeaf(
            protocol_version_, label, EncodeAuxHashes(h_aux), le_secret,
            he_secret, reset_secret, delay_schedule, pcr_criteria,
            &pinweaver_status, &root, &cred_metadata_string, &mac_string);

        *cred_metadata = StringToBlob(cred_metadata_string);
        *mac = StringToBlob(mac_string);
        *new_root = StringToBlob(root);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::NeedsPCRBinding(
    const std::vector<uint8_t>& cred_metadata) {
  if (protocol_version_ == 0)
    return false;

  const struct unimported_leaf_data_t* unimported =
      reinterpret_cast<const struct unimported_leaf_data_t*>(
          cred_metadata.data());
  if (unimported->head.leaf_version.minor == 0 &&
      unimported->head.leaf_version.major == 0)
    return true;

  if (cred_metadata.size() <
      offsetof(unimported_leaf_data_t, payload) +
          offsetof(leaf_public_data_t, valid_pcr_criteria) +
          PW_MAX_PCR_CRITERIA_COUNT * sizeof(struct valid_pcr_value_t)) {
    LOG(ERROR) << "PinweaverLECredentialBackend metadata too short "
               << cred_metadata.size();
    return true;
  }

  const struct leaf_public_data_t* leaf_data =
      reinterpret_cast<const struct leaf_public_data_t*>(unimported->payload);
  return leaf_data->valid_pcr_criteria[0].bitmask[0] == 0 &&
         leaf_data->valid_pcr_criteria[0].bitmask[1] == 0;
}

int PinweaverLECredentialBackend::GetWrongAuthAttempts(
    const std::vector<uint8_t>& cred_metadata) {
  // Just like in NeedsPCRBinding, the assumption is that leaf_public_data_t
  // structure will have the existing part immutable in the future.
  const struct unimported_leaf_data_t* unimported =
      reinterpret_cast<const struct unimported_leaf_data_t*>(
          cred_metadata.data());

  if (cred_metadata.size() < offsetof(unimported_leaf_data_t, payload) +
                                 offsetof(leaf_public_data_t, attempt_count) +
                                 sizeof(struct attempt_count_t)) {
    LOG(ERROR) << "GetWrongAuthAttempts metadata too short "
               << cred_metadata.size();
    return -1;
  }

  const struct leaf_public_data_t* leaf_data =
      reinterpret_cast<const struct leaf_public_data_t*>(unimported->payload);
  return leaf_data->attempt_count.v;
}

bool PinweaverLECredentialBackend::CheckCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    const brillo::SecureBlob& le_secret,
    std::vector<uint8_t>* new_cred_metadata,
    std::vector<uint8_t>* new_mac,
    brillo::SecureBlob* he_secret,
    brillo::SecureBlob* reset_secret,
    LECredBackendError* err,
    std::vector<uint8_t>* new_root) {
  return PerformPinweaverOperation(
      "CheckCredential", err, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        uint32_t seconds_to_wait;
        std::string cred_metadata_string;
        std::string mac_string;
        trunks::TPM_RC result = tpm_utility->PinWeaverTryAuth(
            protocol_version_, le_secret, EncodeAuxHashes(h_aux),
            BlobToString(orig_cred_metadata), &pinweaver_status, &root,
            &seconds_to_wait, he_secret, reset_secret, &cred_metadata_string,
            &mac_string);

        *new_cred_metadata = StringToBlob(cred_metadata_string);
        *new_mac = StringToBlob(mac_string);
        *new_root = StringToBlob(root);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::ResetCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    const brillo::SecureBlob& reset_secret,
    std::vector<uint8_t>* new_cred_metadata,
    std::vector<uint8_t>* new_mac,
    LECredBackendError* err,
    std::vector<uint8_t>* new_root) {
  return PerformPinweaverOperation(
      "ResetCredential", err, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        brillo::SecureBlob he_secret;
        std::string cred_metadata_string;
        std::string mac_string;
        trunks::TPM_RC result = tpm_utility->PinWeaverResetAuth(
            protocol_version_, reset_secret, EncodeAuxHashes(h_aux),
            BlobToString(orig_cred_metadata), &pinweaver_status, &root,
            &he_secret, &cred_metadata_string, &mac_string);

        *new_cred_metadata = StringToBlob(cred_metadata_string);
        *new_mac = StringToBlob(mac_string);
        *new_root = StringToBlob(root);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::RemoveCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& mac,
    std::vector<uint8_t>* new_root) {
  return PerformPinweaverOperation(
      "RemoveCredential", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        trunks::TPM_RC result = tpm_utility->PinWeaverRemoveLeaf(
            protocol_version_, label, EncodeAuxHashes(h_aux), BlobToString(mac),
            &pinweaver_status, &root);
        *new_root = StringToBlob(root);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::GetLog(
    const std::vector<uint8_t>& cur_disk_root_hash,
    std::vector<uint8_t>* root_hash,
    std::vector<LELogEntry>* log) {
  return PerformPinweaverOperation(
      "GetLog", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string cur_root = BlobToString(cur_disk_root_hash);
        std::string root;
        std::vector<trunks::PinWeaverLogEntry> log_ret;
        trunks::TPM_RC result = tpm_utility->PinWeaverGetLog(
            protocol_version_, cur_root, &pinweaver_status, &root, &log_ret);
        *root_hash = StringToBlob(root);
        ConvertPinWeaverLogToLELog(log_ret, log);
        return std::make_pair(result, pinweaver_status);
      });
}

bool PinweaverLECredentialBackend::ReplayLogOperation(
    const std::vector<uint8_t>& log_entry_root,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    std::vector<uint8_t>* new_cred_metadata,
    std::vector<uint8_t>* new_mac) {
  return PerformPinweaverOperation(
      "LogReplay", nullptr, [&](trunks::TpmUtility* tpm_utility) {
        uint32_t pinweaver_status;
        std::string root;
        std::string cred_metadata_string;
        std::string mac_string;
        trunks::TPM_RC result = tpm_utility->PinWeaverLogReplay(
            protocol_version_, BlobToString(log_entry_root),
            EncodeAuxHashes(h_aux), BlobToString(orig_cred_metadata),
            &pinweaver_status, &root, &cred_metadata_string, &mac_string);
        *new_cred_metadata = StringToBlob(cred_metadata_string);
        *new_mac = StringToBlob(mac_string);
        return std::make_pair(result, pinweaver_status);
      });
}

template <typename Operation>
bool PinweaverLECredentialBackend::PerformPinweaverOperation(
    const std::string& name, LECredBackendError* err, Operation op) {
  Tpm2Impl::TrunksClientContext* trunks = nullptr;
  if (!tpm_->GetTrunksContext(&trunks)) {
    LOG(ERROR) << "Error getting trunks context for " << name;
    return false;
  }

  if (protocol_version_ > PW_PROTOCOL_VERSION && name != "IsSupported") {
    LOG(ERROR) << "Protocol version not initialized for " << name;
    return false;
  }

  std::pair<trunks::TPM_RC, int32_t> status = op(trunks->tpm_utility.get());

  LECredBackendError error = ConvertStatus(name, status.first, status.second);
  if (err) {
    *err = error;
  }

  return error == LE_TPM_SUCCESS;
}

}  // namespace cryptohome
