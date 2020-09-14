// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fake_le_credential_backend.h"

#include <openssl/sha.h>

#include <brillo/secure_blob.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/fake_le_credential_metadata.pb.h"

namespace cryptohome {

// Initial root hash when the leaf length is 14 bits, and each node
// has 4 children.
const uint8_t kInitRootHash14_4[SHA256_DIGEST_LENGTH] = {
    0x91, 0x3C, 0xA7, 0x20, 0x82, 0x23, 0xB8, 0xC8, 0x92, 0xA6, 0x1E,
    0x83, 0xD9, 0x68, 0x07, 0x28, 0xE3, 0xE1, 0xD6, 0xBB, 0x10, 0x63,
    0xF2, 0xDD, 0xCE, 0x92, 0x25, 0x71, 0x80, 0x3D, 0xA9, 0xEE};

FakeLECredentialBackend::FakeLECredentialBackend() {
  log_.reserve(kFakeLogSize);
}

bool FakeLECredentialBackend::Reset(std::vector<uint8_t>* new_root) {
  struct FakeLELogEntry fake_entry;
  fake_entry.entry.type = LE_LOG_RESET;
  fake_entry.entry.root = std::vector<uint8_t>(
      kInitRootHash14_4, kInitRootHash14_4 + SHA256_DIGEST_LENGTH);
  AddLogEntry(fake_entry);

  *new_root = CurrentRootHash();

  return true;
}

bool FakeLECredentialBackend::InsertCredential(
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
  // Calculate the root hash, assuming new MAC is 32 bytes of 0.
  std::vector<uint8_t> leaf_mac(SHA256_DIGEST_LENGTH, 0);
  // Set |new_root| to the original value, in case we return errors.
  *new_root = CurrentRootHash();

  std::vector<uint8_t> root_hash = RecalculateRootHash(label, leaf_mac, h_aux);
  if (root_hash != CurrentRootHash()) {
    LOG(ERROR) << "h_aux and/or metadata don't match the current root hash.";
    return false;
  }

  // Generate the credential metadata structure.
  // TODO(pmalani): Still have to add support for the delay schedule.
  // The "encryption" of the credential is just a NOP.
  FakeLECredentialMetadata cred_metadata_entry;
  cred_metadata_entry.set_label(label);
  cred_metadata_entry.set_le_secret(le_secret.data(), le_secret.size());
  cred_metadata_entry.set_he_secret(he_secret.data(), he_secret.size());
  cred_metadata_entry.set_reset_secret(reset_secret.data(),
                                       reset_secret.size());
  cred_metadata_entry.set_attempt_count(0);
  if (valid_pcr_criteria.size() > 0) {
    cred_metadata_entry.set_valid_pcr_digest(valid_pcr_criteria[0].digest);
  }

  cred_metadata->resize(cred_metadata_entry.ByteSizeLong());
  if (!cred_metadata_entry.SerializeToArray(cred_metadata->data(),
                                            cred_metadata->size())) {
    LOG(ERROR) << "Couldn't serialize cred metadata, label: " << label;
    return false;
  }

  // Actual TPM will actually calculate the MAC, but for testing,
  // just a SHA should be sufficient.
  brillo::SecureBlob hash = CryptoLib::Sha256ToSecureBlob(*cred_metadata);
  mac->assign(hash.begin(), hash.end());

  struct FakeLELogEntry fake_entry;
  fake_entry.entry.type = LE_LOG_INSERT;
  fake_entry.entry.root = RecalculateRootHash(label, *mac, h_aux);
  fake_entry.entry.label = label;
  fake_entry.entry.mac = *mac;
  AddLogEntry(fake_entry);

  *new_root = CurrentRootHash();

  return true;
}

bool FakeLECredentialBackend::NeedsPCRBinding(
    const std::vector<uint8_t>& cred_metadata) {
  return false;
}

int FakeLECredentialBackend::GetWrongAuthAttempts(
    const std::vector<uint8_t>& cred_metadata) {
  FakeLECredentialMetadata metadata_tmp;
  if (!metadata_tmp.ParseFromArray(cred_metadata.data(),
                                   cred_metadata.size())) {
    LOG(INFO) << "Couldn't parse the data.";
    return -1;
  }
  return metadata_tmp.attempt_count();
}

void FakeLECredentialBackend::ExtendArcPCR(const std::string& data) {
  pcr_digest += data;
}

void FakeLECredentialBackend::ResetArcPCR() {
  pcr_digest.clear();
}

bool FakeLECredentialBackend::CheckCredential(
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
  *err = LE_TPM_SUCCESS;
  new_cred_metadata->clear();
  new_mac->clear();
  // Set |new_root| to the original value, in case we return errors.
  *new_root = CurrentRootHash();

  std::vector<uint8_t> orig_mac = CryptoLib::Sha256(orig_cred_metadata);

  std::vector<uint8_t> root_hash = RecalculateRootHash(label, orig_mac, h_aux);
  if (root_hash != CurrentRootHash()) {
    LOG(ERROR) << "h_aux and/or metadata don't match the current root hash.";
    *err = LE_TPM_ERROR_HASH_TREE_SYNC;
    return false;
  }

  FakeLECredentialMetadata metadata_tmp;
  if (!metadata_tmp.ParseFromArray(orig_cred_metadata.data(),
                                   orig_cred_metadata.size())) {
    LOG(INFO) << "Couldn't deserialize cred metadata, label: " << label;
    *err = LE_TPM_ERROR_HASH_TREE_SYNC;
    return false;
  }

  if (metadata_tmp.attempt_count() >= LE_MAX_INCORRECT_ATTEMPTS) {
    *err = LE_TPM_ERROR_TOO_MANY_ATTEMPTS;
    return false;
  }

  // Check the PCR.
  if (!pcr_digest.empty() && metadata_tmp.valid_pcr_digest() != pcr_digest) {
    *err = LE_TPM_ERROR_PCR_NOT_MATCH;
    return false;
  }

  // Check the LE secret.
  if (!memcmp(metadata_tmp.le_secret().data(), le_secret.data(),
              le_secret.size())) {
    metadata_tmp.set_attempt_count(0);
    he_secret->assign(metadata_tmp.he_secret().begin(),
                      metadata_tmp.he_secret().end());
    reset_secret->assign(metadata_tmp.reset_secret().begin(),
                         metadata_tmp.reset_secret().end());
  } else {
    *err = LE_TPM_ERROR_INVALID_LE_SECRET;
    metadata_tmp.set_attempt_count(metadata_tmp.attempt_count() + 1);
  }

  new_cred_metadata->resize(metadata_tmp.ByteSizeLong());
  if (!metadata_tmp.SerializeToArray(new_cred_metadata->data(),
                                     new_cred_metadata->size())) {
    LOG(ERROR) << "Couldn't serialize new cred metadata, label: " << label;
    new_cred_metadata->clear();
    return false;
  }

  brillo::SecureBlob hash = CryptoLib::Sha256ToSecureBlob(*new_cred_metadata);
  new_mac->assign(hash.begin(), hash.end());

  struct FakeLELogEntry fake_entry;
  fake_entry.entry.type = LE_LOG_CHECK;
  fake_entry.entry.label = label;
  fake_entry.entry.root = RecalculateRootHash(label, *new_mac, h_aux);
  fake_entry.entry.mac = *new_mac;
  fake_entry.check_success = (*err == LE_TPM_SUCCESS);
  AddLogEntry(fake_entry);

  *new_root = CurrentRootHash();

  return (*err == LE_TPM_SUCCESS);
}

bool FakeLECredentialBackend::ResetCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    const brillo::SecureBlob& reset_secret,
    std::vector<uint8_t>* new_cred_metadata,
    std::vector<uint8_t>* new_mac,
    LECredBackendError* err,
    std::vector<uint8_t>* new_root) {
  *err = LE_TPM_SUCCESS;
  new_cred_metadata->clear();
  new_mac->clear();
  // Set |new_root| to the original value, in case we return errors.
  *new_root = CurrentRootHash();

  std::vector<uint8_t> orig_mac = CryptoLib::Sha256(orig_cred_metadata);

  std::vector<uint8_t> root_hash = RecalculateRootHash(label, orig_mac, h_aux);
  if (root_hash != CurrentRootHash()) {
    LOG(ERROR) << "h_aux and/or metadata don't match the current root hash.";
    *err = LE_TPM_ERROR_HASH_TREE_SYNC;
    return false;
  }

  FakeLECredentialMetadata metadata_tmp;
  if (!metadata_tmp.ParseFromArray(orig_cred_metadata.data(),
                                   orig_cred_metadata.size())) {
    LOG(INFO) << "Couldn't deserialize cred metadata, label: " << label;
    *err = LE_TPM_ERROR_HASH_TREE_SYNC;
    return false;
  }

  // Check the Reset secret.
  if (!memcmp(metadata_tmp.reset_secret().data(), reset_secret.data(),
              reset_secret.size())) {
    metadata_tmp.set_attempt_count(0);
  } else {
    *err = LE_TPM_ERROR_INVALID_RESET_SECRET;
  }

  new_cred_metadata->resize(metadata_tmp.ByteSizeLong());
  if (!metadata_tmp.SerializeToArray(new_cred_metadata->data(),
                                     new_cred_metadata->size())) {
    LOG(ERROR) << "Couldn't serialize new cred metadata, label: " << label;
    new_cred_metadata->clear();
    return false;
  }

  brillo::SecureBlob hash = CryptoLib::Sha256ToSecureBlob(*new_cred_metadata);
  new_mac->assign(hash.begin(), hash.end());

  struct FakeLELogEntry fake_entry;
  fake_entry.entry.type = LE_LOG_CHECK;
  fake_entry.entry.label = label;
  fake_entry.entry.root = RecalculateRootHash(label, *new_mac, h_aux);
  fake_entry.check_success = (*err == LE_TPM_SUCCESS);
  fake_entry.entry.mac = *new_mac;
  AddLogEntry(fake_entry);

  *new_root = CurrentRootHash();

  return (*err == LE_TPM_SUCCESS);
}

bool FakeLECredentialBackend::RemoveCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& mac,
    std::vector<uint8_t>* new_root) {
  // Set |new_root| to the original value, in case we return errors.
  *new_root = CurrentRootHash();
  std::vector<uint8_t> root_hash = RecalculateRootHash(label, mac, h_aux);

  if (root_hash != CurrentRootHash()) {
    LOG(ERROR) << "h_aux and/or metadata don't match the current root hash.";
    return false;
  }

  // Create a new MAC which is all zeros.
  brillo::Blob new_mac(SHA256_DIGEST_LENGTH, 0);

  struct FakeLELogEntry fake_entry;
  fake_entry.entry.type = LE_LOG_REMOVE;
  fake_entry.entry.root = RecalculateRootHash(label, new_mac, h_aux);
  fake_entry.entry.label = label;
  AddLogEntry(fake_entry);

  *new_root = CurrentRootHash();

  return true;
}

bool FakeLECredentialBackend::GetLog(
    const std::vector<uint8_t>& cur_disk_root_hash,
    std::vector<uint8_t>* root_hash,
    std::vector<LELogEntry>* log) {
  *root_hash = CurrentRootHash();
  std::vector<struct LELogEntry> ret_log;
  for (const auto& fake_entry : log_) {
    ret_log.push_back(fake_entry.entry);
    if (fake_entry.entry.root == cur_disk_root_hash) {
      break;
    }
  }
  *log = ret_log;
  return true;
}

bool FakeLECredentialBackend::ReplayLogOperation(
    const std::vector<uint8_t>& cur_disk_root_hash,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    std::vector<uint8_t>* new_cred_metadata,
    std::vector<uint8_t>* new_mac) {
  new_cred_metadata->clear();
  new_mac->clear();

  bool entry_found;
  bool check_success;
  uint64_t label;
  for (const auto& fake_entry : log_) {
    if (fake_entry.entry.root == cur_disk_root_hash) {
      label = fake_entry.entry.label;
      check_success = fake_entry.check_success;
      entry_found = true;
      break;
    }
  }

  if (!entry_found) {
    LOG(ERROR) << "Log entry not found in replay log.";
    return false;
  }

  FakeLECredentialMetadata metadata_tmp;
  if (!metadata_tmp.ParseFromArray(orig_cred_metadata.data(),
                                   orig_cred_metadata.size())) {
    LOG(INFO) << "Couldn't deserialize cred metadata, label: " << label;
    return false;
  }

  if (check_success) {
    metadata_tmp.set_attempt_count(0);
  } else {
    metadata_tmp.set_attempt_count(metadata_tmp.attempt_count() + 1);
  }

  new_cred_metadata->resize(metadata_tmp.ByteSizeLong());
  if (!metadata_tmp.SerializeToArray(new_cred_metadata->data(),
                                     new_cred_metadata->size())) {
    LOG(ERROR) << "Couldn't serialize new cred metadata, label: " << label;
    new_cred_metadata->clear();
    return false;
  }

  brillo::SecureBlob hash = CryptoLib::Sha256ToSecureBlob(*new_cred_metadata);
  new_mac->assign(hash.begin(), hash.end());

  return true;
}

std::vector<uint8_t> FakeLECredentialBackend::RecalculateRootHash(
    const uint64_t label,
    const std::vector<uint8_t>& leaf_mac,
    const std::vector<std::vector<uint8_t>>& h_aux) {
  std::vector<uint8_t> cur_hash = leaf_mac;
  uint64_t cur_label = label;
  auto it = h_aux.begin();

  uint32_t height = kLengthLabels / kBitsPerLevel;
  while (height--) {
    std::vector<uint8_t> input_buffer;
    // Get bottom kBitsPerLevel bits of current label.
    uint64_t cur_suffix = cur_label & ((1 << kBitsPerLevel) - 1);
    // Go from left through right for all the sibling nodes. If we encounter the
    // suffix for the current node, add it to the input buffer, otherwise add
    // the sibling node.
    for (uint64_t i = 0; i < kNumChildren; i++) {
      if (i == cur_suffix) {
        input_buffer.insert(input_buffer.end(), cur_hash.begin(),
                            cur_hash.end());
      } else {
        auto sibling_hash = *it;
        input_buffer.insert(input_buffer.end(), sibling_hash.begin(),
                            sibling_hash.end());
        ++it;
      }
    }
    brillo::SecureBlob result_hash =
        CryptoLib::Sha256ToSecureBlob(input_buffer);
    cur_hash.assign(result_hash.begin(), result_hash.end());
    cur_label = cur_label >> kBitsPerLevel;
  }

  return cur_hash;
}

void FakeLECredentialBackend::AddLogEntry(const struct FakeLELogEntry& entry) {
  if (log_.size() == kFakeLogSize) {
    log_.pop_back();
  }
  log_.insert(log_.begin(), entry);
}

}  // namespace cryptohome
