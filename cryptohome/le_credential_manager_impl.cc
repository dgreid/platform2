// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/le_credential_manager_impl.h"

#include <fcntl.h>

#include <string>
#include <utility>

#include <base/files/file_util.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"

namespace cryptohome {

LECredentialManagerImpl::LECredentialManagerImpl(
    LECredentialBackend* le_backend, const base::FilePath& le_basedir)
    : is_locked_(false), le_tpm_backend_(le_backend), basedir_(le_basedir) {
  CHECK(le_tpm_backend_);

  // Check if hash tree already exists.
  bool new_hash_tree = !base::PathExists(le_basedir);

  hash_tree_ = std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel,
                                                le_basedir);

  // Reset the root hash in the TPM to its initial value.
  if (new_hash_tree) {
    CHECK(le_tpm_backend_->Reset(&root_hash_));
    hash_tree_->GenerateAndStoreHashCache();
  }
}

LECredError LECredentialManagerImpl::InsertCredential(
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    const ValidPcrCriteria& valid_pcr_criteria,
    uint64_t* ret_label) {
  if (!Sync()) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  SignInHashTree::Label label = hash_tree_->GetFreeLabel();
  if (!label.is_valid()) {
    LOG(ERROR) << "No free labels available.";
    ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk,
                   LE_CRED_ERROR_NO_FREE_LABEL);
    return LE_CRED_ERROR_NO_FREE_LABEL;
  }

  std::vector<std::vector<uint8_t>> h_aux = GetAuxHashes(label);
  if (h_aux.empty()) {
    LOG(ERROR) << "Error getting aux hashes for label: " << label.value();
    ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk, LE_CRED_ERROR_HASH_TREE);
    return LE_CRED_ERROR_HASH_TREE;
  }

  ReportLEResult(kLEOpInsert, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> cred_metadata, mac;
  bool success = le_tpm_backend_->InsertCredential(
      label.value(), h_aux, le_secret, he_secret, reset_secret, delay_sched,
      valid_pcr_criteria, &cred_metadata, &mac, &root_hash_);
  if (!success) {
    LOG(ERROR) << "Error executing TPM InsertCredential command.";
    ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_ERROR_HASH_TREE);
    return LE_CRED_ERROR_HASH_TREE;
  }

  ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_SUCCESS);

  if (!hash_tree_->StoreLabel(label, mac, cred_metadata, false)) {
    ReportLEResult(kLEOpInsert, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR)
        << "InsertCredential succeeded in TPM but disk updated failed, label: "
        << label.value();
    // The insert into the disk hash tree failed, so let us remove
    // the credential from the TPM state so that we are back to where
    // we started.
    success = le_tpm_backend_->RemoveCredential(label.value(), h_aux, mac,
                                                &root_hash_);
    if (!success) {
      ReportLEResult(kLEOpInsert, kLEActionBackend, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << " Failed to rewind aborted InsertCredential in TPM, label: "
                 << label.value();
      // The attempt to undo the TPM side operation has also failed, Can't do
      // much else now. We block further LE operations until at least the next
      // boot.
      is_locked_ = true;
      // TODO(crbug.com/809749): Report failure to UMA.
    }
    return LE_CRED_ERROR_HASH_TREE;
  }

  ReportLEResult(kLEOpInsert, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  *ret_label = label.value();
  return LE_CRED_SUCCESS;
}

LECredError LECredentialManagerImpl::CheckCredential(
    const uint64_t& label,
    const brillo::SecureBlob& le_secret,
    brillo::SecureBlob* he_secret,
    brillo::SecureBlob* reset_secret) {
  return CheckSecret(label, le_secret, he_secret, reset_secret, true);
}

LECredError LECredentialManagerImpl::ResetCredential(
    const uint64_t& label, const brillo::SecureBlob& reset_secret) {
  return CheckSecret(label, reset_secret, nullptr, nullptr, false);
}

LECredError LECredentialManagerImpl::RemoveCredential(const uint64_t& label) {
  if (!Sync()) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredError ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                      &h_aux, &metadata_lost);
  if (ret != LE_CRED_SUCCESS) {
    return ret;
  }

  bool success =
      le_tpm_backend_->RemoveCredential(label, h_aux, orig_mac, &root_hash_);
  if (!success) {
    LOG(ERROR) << "Error executing TPM RemoveCredential command.";
    return LE_CRED_ERROR_HASH_TREE;
  }

  if (!hash_tree_->RemoveLabel(label_object)) {
    LOG(ERROR) << "Removed label from TPM but hash tree removal "
                  "encountered error: "
               << label;
    // This is an un-salvageable state. We can't make LE updates anymore,
    // since the disk state can't be updated.
    // We block further LE operations until at least the next boot.
    // The hope is that on reboot, the disk operations start working. In that
    // case, we will be able to replay this operation from the TPM log.
    is_locked_ = true;
    // TODO(crbug.com/809749): Report failure to UMA.
    return LE_CRED_ERROR_HASH_TREE;
  }

  return LE_CRED_SUCCESS;
}

LECredError LECredentialManagerImpl::CheckSecret(
    const uint64_t& label,
    const brillo::SecureBlob& secret,
    brillo::SecureBlob* he_secret,
    brillo::SecureBlob* reset_secret,
    bool is_le_secret) {
  if (!Sync()) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  const char* uma_log_op = is_le_secret ? kLEOpCheck : kLEOpReset;

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredError ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                      &h_aux, &metadata_lost);
  if (ret != LE_CRED_SUCCESS) {
    ReportLEResult(uma_log_op, kLEActionLoadFromDisk, ret);
    return ret;
  }

  if (metadata_lost) {
    LOG(ERROR) << "Invalid cred metadata for label: " << label;
    ReportLEResult(uma_log_op, kLEActionLoadFromDisk,
                   LE_CRED_ERROR_INVALID_METADATA);
    return LE_CRED_ERROR_INVALID_METADATA;
  }

  ReportLEResult(uma_log_op, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> new_cred, new_mac;
  LECredBackendError err;
  if (is_le_secret) {
    he_secret->clear();
    le_tpm_backend_->CheckCredential(label, h_aux, orig_cred, secret, &new_cred,
                                     &new_mac, he_secret, reset_secret, &err,
                                     &root_hash_);
  } else {
    le_tpm_backend_->ResetCredential(label, h_aux, orig_cred, secret, &new_cred,
                                     &new_mac, &err, &root_hash_);
  }

  ReportLEResult(uma_log_op, kLEActionBackend, ConvertTpmError(err));

  // Store the new credential meta data and MAC in case the backend performed a
  // state change. Note that this might also be needed for some failure cases.
  if (!new_cred.empty() && !new_mac.empty()) {
    if (!hash_tree_->StoreLabel(label_object, new_mac, new_cred, false)) {
      ReportLEResult(uma_log_op, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Failed to update credential in disk hash tree for label: "
                 << label;
      // This is an un-salvageable state. We can't make LE updates anymore,
      // since the disk state can't be updated.
      // We block further LE operations until at least the next boot.
      // The hope is that on reboot, the disk operations start working. In that
      // case, we will be able to replay this operation from the TPM log.
      is_locked_ = true;
      // TODO(crbug.com/809749): Report failure to UMA.
      return LE_CRED_ERROR_HASH_TREE;
    }
  }

  ReportLEResult(uma_log_op, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  return ConvertTpmError(err);
}

bool LECredentialManagerImpl::NeedsPcrBinding(const uint64_t& label) {
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredError ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                      &h_aux, &metadata_lost);
  if (ret != LE_CRED_SUCCESS)
    return false;

  return le_tpm_backend_->NeedsPCRBinding(orig_cred);
}

int LECredentialManagerImpl::GetWrongAuthAttempts(const uint64_t& label) {
  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  LECredError ret = RetrieveLabelInfo(label_object, &orig_cred, &orig_mac,
                                      &h_aux, &metadata_lost);
  if (ret != LE_CRED_SUCCESS)
    return -1;

  return le_tpm_backend_->GetWrongAuthAttempts(orig_cred);
}

LECredError LECredentialManagerImpl::RetrieveLabelInfo(
    const SignInHashTree::Label& label,
    std::vector<uint8_t>* cred_metadata,
    std::vector<uint8_t>* mac,
    std::vector<std::vector<uint8_t>>* h_aux,
    bool* metadata_lost) {
  if (!hash_tree_->GetLabelData(label, mac, cred_metadata, metadata_lost)) {
    LOG(ERROR) << "Failed to get the credential in disk hash tree for label: "
               << label.value();
    return LE_CRED_ERROR_INVALID_LABEL;
  }

  // Any empty |cred_metadata| means the label isn't present in the hash tree.
  if (cred_metadata->empty()) {
    LOG(ERROR) << "Label doesn't exist in hash tree: " << label.value();
    return LE_CRED_ERROR_INVALID_LABEL;
  }

  *h_aux = GetAuxHashes(label);
  if (h_aux->empty()) {
    LOG(ERROR) << "Error retrieving aux hashes from hash tree for label: "
               << label.value();
    return LE_CRED_ERROR_HASH_TREE;
  }
  return LE_CRED_SUCCESS;
}

std::vector<std::vector<uint8_t>> LECredentialManagerImpl::GetAuxHashes(
    const SignInHashTree::Label& label) {
  auto aux_labels = hash_tree_->GetAuxiliaryLabels(label);
  std::vector<std::vector<uint8_t>> h_aux;
  if (aux_labels.empty()) {
    LOG(ERROR) << "Error getting h_aux for label:" << label.value();
    return h_aux;
  }

  h_aux.reserve(aux_labels.size());
  for (auto cur_aux_label : aux_labels) {
    brillo::Blob hash, cred_data;
    bool metadata_lost;
    if (!hash_tree_->GetLabelData(cur_aux_label, &hash, &cred_data,
                                  &metadata_lost)) {
      LOG(INFO) << "Error getting aux label :" << cur_aux_label.value()
                << " for label: " << label.value();
      h_aux.clear();
      break;
    }
    h_aux.push_back(std::move(hash));
  }

  return h_aux;
}

LECredError LECredentialManagerImpl::ConvertTpmError(LECredBackendError err) {
  switch (err) {
    case LE_TPM_SUCCESS:
      return LE_CRED_SUCCESS;
    case LE_TPM_ERROR_INVALID_LE_SECRET:
      return LE_CRED_ERROR_INVALID_LE_SECRET;
    case LE_TPM_ERROR_INVALID_RESET_SECRET:
      return LE_CRED_ERROR_INVALID_RESET_SECRET;
    case LE_TPM_ERROR_TOO_MANY_ATTEMPTS:
      return LE_CRED_ERROR_TOO_MANY_ATTEMPTS;
    case LE_TPM_ERROR_HASH_TREE_SYNC:
    case LE_TPM_ERROR_TPM_OP_FAILED:
      return LE_CRED_ERROR_HASH_TREE;
    case LE_TPM_ERROR_PCR_NOT_MATCH:
      return LE_CRED_ERROR_PCR_NOT_MATCH;
  }

  return LE_CRED_ERROR_HASH_TREE;
}

bool LECredentialManagerImpl::Sync() {
  if (is_locked_) {
    ReportLESyncOutcome(LE_CRED_ERROR_LE_LOCKED);
    return false;
  }

  std::vector<uint8_t> disk_root_hash;
  hash_tree_->GetRootHash(&disk_root_hash);

  // If we don't have it, get the root hash from the LE Backend.
  std::vector<LELogEntry> log;
  if (root_hash_.empty()) {
    if (!le_tpm_backend_->GetLog(disk_root_hash, &root_hash_, &log)) {
      ReportLEResult(kLEOpSync, kLEActionBackendGetLog,
                     LE_CRED_ERROR_UNCLASSIFIED);
      ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Couldn't get LE Log.";
      is_locked_ = true;
      return false;
    }
    ReportLEResult(kLEOpSync, kLEActionBackendGetLog, LE_CRED_SUCCESS);
  }

  if (disk_root_hash == root_hash_) {
    ReportLESyncOutcome(LE_CRED_SUCCESS);
    return true;
  }

  LOG(WARNING) << "LE HashCache is stale; reconstructing.";
  // TODO(crbug.com/809749): Add UMA logging for this event.
  hash_tree_->GenerateAndStoreHashCache();
  disk_root_hash.clear();
  hash_tree_->GetRootHash(&disk_root_hash);

  if (disk_root_hash == root_hash_) {
    ReportLESyncOutcome(LE_CRED_SUCCESS);
    return true;
  }

  // Get the log again, since |disk_root_hash| may have changed.
  log.clear();
  if (!le_tpm_backend_->GetLog(disk_root_hash, &root_hash_, &log)) {
    ReportLEResult(kLEOpSync, kLEActionBackendGetLog,
                   LE_CRED_ERROR_UNCLASSIFIED);
    ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "Couldn't get LE Log.";
    is_locked_ = true;
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionBackendGetLog, LE_CRED_SUCCESS);
  if (!ReplayLogEntries(log, disk_root_hash)) {
    ReportLESyncOutcome(LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "Failed to Synchronize LE disk state after log replay.";
    // TODO(crbug.com/809749): Add UMA logging for this event.
    is_locked_ = true;
    return false;
  }
  ReportLESyncOutcome(LE_CRED_SUCCESS);
  return true;
}

bool LECredentialManagerImpl::ReplayInsert(uint64_t label,
                                           const std::vector<uint8_t>& log_root,
                                           const std::vector<uint8_t>& mac) {
  // Fill cred_metadata with some random data since LECredentialManager
  // considers empty cred_metadata as a non-existent label.
  std::vector<uint8_t> cred_metadata(mac.size());
  CryptoLib::GetSecureRandom(cred_metadata.data(), cred_metadata.size());
  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  if (!hash_tree_->StoreLabel(label_obj, mac, cred_metadata, true)) {
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "InsertCredentialReplay disk update "
                  "failed, label: "
               << label_obj.value();
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  return true;
}

bool LECredentialManagerImpl::ReplayCheck(
    uint64_t label, const std::vector<uint8_t>& log_root) {
  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  bool metadata_lost;
  if (RetrieveLabelInfo(label_obj, &orig_cred, &orig_mac, &h_aux,
                        &metadata_lost) != LE_CRED_SUCCESS) {
    ReportLEResult(kLEOpSync, kLEActionLoadFromDisk, LE_CRED_ERROR_HASH_TREE);
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionLoadFromDisk, LE_CRED_SUCCESS);

  std::vector<uint8_t> new_cred, new_mac;
  if (!le_tpm_backend_->ReplayLogOperation(log_root, h_aux, orig_cred,
                                           &new_cred, &new_mac)) {
    ReportLEResult(kLEOpSync, kLEActionBackendReplayLog,
                   LE_CRED_ERROR_UNCLASSIFIED);
    LOG(ERROR) << "Auth replay failed on LE Backend, label: " << label;
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionBackendReplayLog, LE_CRED_SUCCESS);

  // Store the new credential metadata and MAC.
  if (!new_cred.empty() && !new_mac.empty()) {
    if (!hash_tree_->StoreLabel(label_obj, new_mac, new_cred, false)) {
      ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
      LOG(ERROR) << "Error in LE auth replay disk hash tree update, label: "
                 << label;
      // TODO(crbug.com/809749): Report failure to UMA.
      return false;
    }

    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);
  }

  return true;
}

bool LECredentialManagerImpl::ReplayResetTree() {
  hash_tree_.reset();
  if (!base::DeletePathRecursively(basedir_)) {
    PLOG(ERROR) << "Failed to delete disk hash tree during replay.";
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    return false;
  }

  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);

  hash_tree_ =
      std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel, basedir_);
  hash_tree_->GenerateAndStoreHashCache();
  return true;
}

bool LECredentialManagerImpl::ReplayRemove(uint64_t label) {
  SignInHashTree::Label label_obj(label, kLengthLabels, kBitsPerLevel);
  if (!hash_tree_->RemoveLabel(label_obj)) {
    ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_ERROR_HASH_TREE);
    LOG(ERROR) << "RemoveLabel LE Replay failed for label: " << label;
    // TODO(crbug.com/809749): Report failure to UMA.
    return false;
  }
  ReportLEResult(kLEOpSync, kLEActionSaveToDisk, LE_CRED_SUCCESS);
  return true;
}

bool LECredentialManagerImpl::ReplayLogEntries(
    const std::vector<LELogEntry>& log,
    const std::vector<uint8_t>& disk_root_hash) {
  // The log entries are in reverse chronological order. Because the log entries
  // only store the root hash after the operation, the strategy here is:
  // - Parse the logs in reverse.
  // - First try to find a log entry which matches the on-disk root hash,
  //   and start with the log entry following that. If you can't, then just
  //   start from the earliest log.
  // - For all other entries, simply attempt to replay the operation.
  auto it = log.rbegin();
  for (; it != log.rend(); ++it) {
    const LELogEntry& log_entry = *it;
    if (log_entry.root == disk_root_hash) {
      ++it;
      break;
    }
  }

  if (it == log.rend()) {
    it = log.rbegin();
  }

  std::vector<uint8_t> cur_root_hash = disk_root_hash;
  std::vector<uint64_t> inserted_leaves;
  for (; it != log.rend(); ++it) {
    const LELogEntry& log_entry = *it;
    bool ret;
    switch (log_entry.type) {
      case LE_LOG_INSERT:
        ret = ReplayInsert(log_entry.label, log_entry.root, log_entry.mac);
        if (ret) {
          inserted_leaves.push_back(log_entry.label);
        }
        break;
      case LE_LOG_REMOVE:
        ret = ReplayRemove(log_entry.label);
        break;
      case LE_LOG_CHECK:
        ret = ReplayCheck(log_entry.label, log_entry.root);
        break;
      case LE_LOG_RESET:
        ret = ReplayResetTree();
        break;
      case LE_LOG_INVALID:
        ret = false;
        break;
    }
    if (!ret) {
      LOG(ERROR) << "Failure to replay LE Cred log entries.";
      return false;
    }
    cur_root_hash.clear();
    hash_tree_->GetRootHash(&cur_root_hash);
    if (cur_root_hash != log_entry.root) {
      LOG(ERROR) << "Root hash doesn't match log root after replaying entry.";
      return false;
    }
  }

  // Remove any inserted leaves since they are unusable.
  for (const auto& label : inserted_leaves) {
    if (RemoveCredential(label) != LE_CRED_SUCCESS) {
      LOG(ERROR) << "Failed to remove re-inserted label: " << label;
      return false;
    }
  }

  return true;
}

}  // namespace cryptohome
