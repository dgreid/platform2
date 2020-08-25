// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_pinweaver.h"

#include <endian.h>

#include <algorithm>

#include <base/logging.h>

#include <trunks/cr50_headers/pinweaver_types.h>

namespace trunks {
namespace {

void Serialize(const void* value, size_t length, std::string* buffer) {
  const char* value_bytes = reinterpret_cast<const char*>(value);
  buffer->append(value_bytes, length);
}

void Serialize_pw_request_header_t(uint8_t protocol_version,
                                   uint8_t message_type,
                                   uint16_t data_length,
                                   std::string* buffer) {
  struct pw_request_header_t header = {
      protocol_version, {message_type}, htole16(data_length)};
  Serialize(&header, sizeof(header), buffer);
}

TPM_RC Parse_unimported_leaf_data_t(std::string::const_iterator begin,
                                    std::string::const_iterator end,
                                    std::string* cred_metadata,
                                    std::string* mac) {
  auto size = end - begin;
  if (size < sizeof(unimported_leaf_data_t))
    return SAPI_RC_BAD_SIZE;

  const struct unimported_leaf_data_t* unimported_leaf_data =
      reinterpret_cast<const struct unimported_leaf_data_t*>(&*begin);
  if (size != sizeof(unimported_leaf_data_t) +
                  unimported_leaf_data->head.pub_len +
                  unimported_leaf_data->head.sec_len) {
    return SAPI_RC_BAD_SIZE;
  }

  if (cred_metadata)
    cred_metadata->assign(begin, end);
  if (mac) {
    mac->assign(
        unimported_leaf_data->hmac,
        unimported_leaf_data->hmac + sizeof(unimported_leaf_data->hmac));
  }
  return TPM_RC_SUCCESS;
}

TPM_RC Validate_cred_metadata(const std::string& cred_metadata) {
  return Parse_unimported_leaf_data_t(cred_metadata.begin(),
                                      cred_metadata.end(), nullptr, nullptr);
}

}  // namespace

TPM_RC Serialize_pw_ping_t(uint8_t request_version, std::string* buffer) {
  buffer->reserve(buffer->size() + sizeof(pw_request_header_t));

  Serialize_pw_request_header_t(request_version, PW_MT_INVALID, 0, buffer);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_reset_tree_t(uint8_t protocol_version,
                                 uint8_t bits_per_level,
                                 uint8_t height,
                                 std::string* buffer) {
  struct pw_request_reset_tree_t data = {{bits_per_level}, {height}};
  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) + sizeof(data));

  Serialize_pw_request_header_t(protocol_version, PW_RESET_TREE, sizeof(data),
                                buffer);
  Serialize(&data, sizeof(data), buffer);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_insert_leaf_t(
    uint8_t protocol_version,
    uint64_t label,
    const std::string& h_aux,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const std::map<uint32_t, uint32_t>& delay_schedule,
    const ValidPcrCriteria& valid_pcr_criteria,
    std::string* buffer) {
  if (h_aux.length() > PW_MAX_PATH_SIZE || le_secret.size() != PW_SECRET_SIZE ||
      he_secret.size() != PW_SECRET_SIZE ||
      reset_secret.size() != PW_SECRET_SIZE ||
      delay_schedule.size() > PW_SCHED_COUNT ||
      valid_pcr_criteria.valid_pcr_values_size() > PW_MAX_PCR_CRITERIA_COUNT) {
    return SAPI_RC_BAD_PARAMETER;
  }

  struct pw_request_insert_leaf_t data = {};
  int pcr_criteria_size =
      sizeof(struct valid_pcr_value_t) * PW_MAX_PCR_CRITERIA_COUNT;
  int data_size = sizeof(data);
  if (protocol_version == 0) {
    data_size -= pcr_criteria_size;
  }

  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) + data_size +
                  h_aux.size());

  data.label = {htole64(label)};
  size_t x = 0;
  for (const auto& itr : delay_schedule) {
    data.delay_schedule[x].attempt_count = {htole32(itr.first)};
    data.delay_schedule[x].time_diff = {htole32(itr.second)};
    ++x;
  }

  if (protocol_version > 0) {
    x = 0;
    for (const ValidPcrValue& value : valid_pcr_criteria.valid_pcr_values()) {
      data.valid_pcr_criteria[x].bitmask[0] = value.bitmask()[0];
      data.valid_pcr_criteria[x].bitmask[1] = value.bitmask()[1];
      if (value.digest().size() > sizeof(data.valid_pcr_criteria[0].digest))
        return SAPI_RC_BAD_PARAMETER;
      std::copy(value.digest().begin(), value.digest().end(),
                data.valid_pcr_criteria[x].digest);
      ++x;
    }
    for (; x < PW_MAX_PCR_CRITERIA_COUNT; ++x) {
      memset(data.valid_pcr_criteria[x].bitmask, 0,
             sizeof(data.valid_pcr_criteria[x].bitmask));
    }
  } else if (valid_pcr_criteria.valid_pcr_values_size() > 0) {
    return SAPI_RC_BAD_PARAMETER;
  }

  std::copy(le_secret.begin(), le_secret.end(), data.low_entropy_secret);
  std::copy(he_secret.begin(), he_secret.end(), data.high_entropy_secret);
  std::copy(reset_secret.begin(), reset_secret.end(), data.reset_secret);

  Serialize_pw_request_header_t(protocol_version, PW_INSERT_LEAF,
                                data_size + h_aux.size(), buffer);
  Serialize(&data, data_size, buffer);

  buffer->append(h_aux);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_remove_leaf_t(uint8_t protocol_version,
                                  uint64_t label,
                                  const std::string& h_aux,
                                  const std::string& mac,
                                  std::string* buffer) {
  if (h_aux.length() > PW_MAX_PATH_SIZE || mac.size() != PW_HASH_SIZE) {
    return SAPI_RC_BAD_PARAMETER;
  }

  struct pw_request_remove_leaf_t data = {};
  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) + sizeof(data) +
                  h_aux.size());

  data.leaf_location = {htole64(label)};
  std::copy(mac.begin(), mac.end(), data.leaf_hmac);

  Serialize_pw_request_header_t(protocol_version, PW_REMOVE_LEAF,
                                sizeof(data) + h_aux.size(), buffer);
  Serialize(&data, sizeof(data), buffer);
  buffer->append(h_aux);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_try_auth_t(uint8_t protocol_version,
                               const brillo::SecureBlob& le_secret,
                               const std::string& h_aux,
                               const std::string& cred_metadata,
                               std::string* buffer) {
  if (le_secret.size() != PW_SECRET_SIZE || h_aux.length() > PW_MAX_PATH_SIZE ||
      Validate_cred_metadata(cred_metadata) != TPM_RC_SUCCESS) {
    return SAPI_RC_BAD_PARAMETER;
  }

  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) +
                  sizeof(pw_request_try_auth_t) +
                  (cred_metadata.size() - sizeof(unimported_leaf_data_t)) +
                  h_aux.size());

  Serialize_pw_request_header_t(
      protocol_version, PW_TRY_AUTH,
      le_secret.size() + cred_metadata.size() + h_aux.size(), buffer);

  buffer->append(le_secret.to_string());
  buffer->append(cred_metadata);
  buffer->append(h_aux);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_reset_auth_t(uint8_t protocol_version,
                                 const brillo::SecureBlob& reset_secret,
                                 const std::string& h_aux,
                                 const std::string& cred_metadata,
                                 std::string* buffer) {
  if (reset_secret.size() != PW_SECRET_SIZE ||
      h_aux.length() > PW_MAX_PATH_SIZE ||
      Validate_cred_metadata(cred_metadata) != TPM_RC_SUCCESS) {
    return SAPI_RC_BAD_PARAMETER;
  }

  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) +
                  sizeof(pw_request_reset_auth_t) +
                  (cred_metadata.size() - sizeof(unimported_leaf_data_t)) +
                  h_aux.size());

  Serialize_pw_request_header_t(
      protocol_version, PW_RESET_AUTH,
      reset_secret.size() + cred_metadata.size() + h_aux.size(), buffer);

  buffer->append(reset_secret.to_string());
  buffer->append(cred_metadata);
  buffer->append(h_aux);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_get_log_t(uint8_t protocol_version,
                              const std::string& root,
                              std::string* buffer) {
  if (root.size() != PW_HASH_SIZE) {
    return SAPI_RC_BAD_PARAMETER;
  }

  Serialize_pw_request_header_t(protocol_version, PW_GET_LOG,
                                sizeof(struct pw_request_get_log_t), buffer);
  buffer->append(root);
  return TPM_RC_SUCCESS;
}

TPM_RC Serialize_pw_log_replay_t(uint8_t protocol_version,
                                 const std::string& log_root,
                                 const std::string& h_aux,
                                 const std::string& cred_metadata,
                                 std::string* buffer) {
  if (log_root.size() != PW_HASH_SIZE || h_aux.length() > PW_MAX_PATH_SIZE ||
      Validate_cred_metadata(cred_metadata) != TPM_RC_SUCCESS) {
    return SAPI_RC_BAD_PARAMETER;
  }

  buffer->reserve(buffer->size() + sizeof(pw_request_header_t) +
                  sizeof(pw_request_log_replay_t) +
                  (cred_metadata.size() - sizeof(unimported_leaf_data_t)) +
                  h_aux.size());

  Serialize_pw_request_header_t(protocol_version, PW_LOG_REPLAY,
                                sizeof(pw_request_log_replay_t) -
                                    sizeof(struct unimported_leaf_data_t) +
                                    cred_metadata.size() + h_aux.size(),
                                buffer);
  buffer->append(log_root);
  buffer->append(cred_metadata);
  buffer->append(h_aux);
  return TPM_RC_SUCCESS;
}

TPM_RC Parse_pw_response_header_t(const std::string& buffer,
                                  uint32_t* result_code,
                                  std::string* root_hash,
                                  uint16_t* data_length) {
  *result_code = 0;
  if (root_hash)
    root_hash->clear();
  *data_length = 0;

  if (buffer.empty()) {
    return SAPI_RC_INSUFFICIENT_BUFFER;
  }

  uint8_t version = (uint8_t)buffer[0];
  if (version > PW_PROTOCOL_VERSION) {
    LOG(ERROR) << "Pinweaver protocol version mismatch: got "
               << static_cast<uint32_t>(version) << " expected "
               << PW_PROTOCOL_VERSION << " or lower.";
    return SAPI_RC_ABI_MISMATCH;
  }

  if (buffer.size() < sizeof(struct pw_response_header_t)) {
    LOG(ERROR) << "Pinweaver response contained an unexpected number of bytes.";
    return SAPI_RC_INSUFFICIENT_BUFFER;
  }

  const struct pw_response_header_t* header =
      reinterpret_cast<const struct pw_response_header_t*>(buffer.data());
  *result_code = le32toh(header->result_code);
  if (root_hash)
    root_hash->assign(header->root, header->root + sizeof(header->root));
  *data_length = le16toh(header->data_length);

  if (buffer.size() != sizeof(struct pw_response_header_t) + *data_length) {
    LOG(ERROR) << "Pinweaver response contained " << buffer.size()
               << " instead of "
               << sizeof(struct pw_response_header_t) + *data_length
               << "bytes.";
    return SAPI_RC_BAD_SIZE;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC Parse_pw_short_message(const std::string& buffer,
                              uint32_t* result_code,
                              std::string* root_hash) {
  uint16_t data_length;
  TPM_RC rc =
      Parse_pw_response_header_t(buffer, result_code, root_hash, &data_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  if (data_length != 0) {
    LOG(ERROR) << "Pinweaver error contained an unexpected number of bytes.";
    return SAPI_RC_BAD_SIZE;
  }

  return TPM_RC_SUCCESS;
}

TPM_RC Parse_pw_pong_t(const std::string& buffer, uint8_t* protocol_version) {
  uint32_t result_code;
  TPM_RC rc = Parse_pw_short_message(buffer, &result_code, nullptr);
  if (rc != TPM_RC_SUCCESS)
    return rc;
  if (result_code != PW_ERR_TYPE_INVALID)
    return SAPI_RC_ABI_MISMATCH;
  *protocol_version = (uint8_t)buffer[0];
  return TPM_RC_SUCCESS;
}

TPM_RC Parse_pw_insert_leaf_t(const std::string& buffer,
                              uint32_t* result_code,
                              std::string* root_hash,
                              std::string* cred_metadata,
                              std::string* mac) {
  cred_metadata->clear();
  mac->clear();

  uint16_t response_length;
  TPM_RC rc = Parse_pw_response_header_t(buffer, result_code, root_hash,
                                         &response_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  if (*result_code != 0) {
    return response_length == 0 ? TPM_RC_SUCCESS : SAPI_RC_BAD_SIZE;
  }

  return Parse_unimported_leaf_data_t(
      buffer.begin() + sizeof(pw_response_header_t), buffer.end(),
      cred_metadata, mac);
}

TPM_RC Parse_pw_try_auth_t(const std::string& buffer,
                           uint32_t* result_code,
                           std::string* root_hash,
                           uint32_t* seconds_to_wait,
                           brillo::SecureBlob* he_secret,
                           brillo::SecureBlob* reset_secret,
                           std::string* cred_metadata_out,
                           std::string* mac_out) {
  *seconds_to_wait = 0;
  he_secret->clear();
  reset_secret->clear();
  cred_metadata_out->clear();
  mac_out->clear();

  uint16_t response_length;
  TPM_RC rc = Parse_pw_response_header_t(buffer, result_code, root_hash,
                                         &response_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  // For EC_SUCCESS, PW_ERR_RATE_LIMIT_REACHED, and PW_ERR_LOWENT_AUTH_FAILED
  // a full size response is sent. However, only particular fields are valid.
  if (*result_code != 0 && *result_code != PW_ERR_RATE_LIMIT_REACHED &&
      *result_code != PW_ERR_LOWENT_AUTH_FAILED) {
    return response_length == 0 ? TPM_RC_SUCCESS : SAPI_RC_BAD_SIZE;
  }

  if (response_length < sizeof(pw_response_try_auth_t))
    return SAPI_RC_BAD_SIZE;

  auto itr = buffer.begin() + sizeof(pw_response_header_t);
  // This field may not be aligned so it is retrieved in a way that will work
  // regardless of platform.
  *seconds_to_wait = (static_cast<uint32_t>(itr[0]) << 24) |
                     (static_cast<uint32_t>(itr[1]) << 16) |
                     (static_cast<uint32_t>(itr[2]) << 8) |
                     static_cast<uint32_t>(itr[3]);
  itr += 4;

  // he_secret is only valid for EC_SUCCESS.
  if (*result_code == 0) {
    he_secret->assign(itr, itr + PW_SECRET_SIZE);
    // reset_secret is present only starting from protocol_version = 1.
    if ((uint8_t)buffer[0] > 0) {
      reset_secret->assign(itr + PW_SECRET_SIZE, itr + 2 * PW_SECRET_SIZE);
    }
  }
  if ((uint8_t)buffer[0] > 0) {
    itr += 2 * PW_SECRET_SIZE;
  } else {
    itr += PW_SECRET_SIZE;
  }

  // For PW_ERR_RATE_LIMIT_REACHED the only valid result field is
  // seconds_to_wait.
  if (*result_code == PW_ERR_RATE_LIMIT_REACHED)
    return TPM_RC_SUCCESS;

  return Parse_unimported_leaf_data_t(itr, buffer.end(), cred_metadata_out,
                                      mac_out);
}

TPM_RC Parse_pw_reset_auth_t(const std::string& buffer,
                             uint32_t* result_code,
                             std::string* root_hash,
                             brillo::SecureBlob* he_secret,
                             std::string* cred_metadata_out,
                             std::string* mac_out) {
  he_secret->clear();
  cred_metadata_out->clear();
  mac_out->clear();

  uint16_t response_length;
  TPM_RC rc = Parse_pw_response_header_t(buffer, result_code, root_hash,
                                         &response_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  if (*result_code != 0) {
    return response_length == 0 ? TPM_RC_SUCCESS : SAPI_RC_BAD_SIZE;
  }

  if (response_length < sizeof(pw_response_reset_auth_t)) {
    LOG(ERROR) << "Pinweaver pw_response_reset_auth contained an unexpected "
                  "number of bytes.";
    return SAPI_RC_BAD_SIZE;
  }

  auto itr = buffer.begin() + sizeof(pw_response_header_t);
  he_secret->assign(itr, itr + PW_SECRET_SIZE);
  itr += PW_SECRET_SIZE;

  return Parse_unimported_leaf_data_t(itr, buffer.end(), cred_metadata_out,
                                      mac_out);
}

TPM_RC Parse_pw_get_log_t(const std::string& buffer,
                          uint32_t* result_code,
                          std::string* root_hash,
                          std::vector<trunks::PinWeaverLogEntry>* log) {
  log->clear();

  uint16_t response_length;
  TPM_RC rc = Parse_pw_response_header_t(buffer, result_code, root_hash,
                                         &response_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  if (*result_code != 0) {
    return response_length == 0 ? TPM_RC_SUCCESS : SAPI_RC_BAD_SIZE;
  }

  if (response_length % sizeof(struct pw_get_log_entry_t) != 0)
    return SAPI_RC_BAD_SIZE;

  log->resize(response_length / sizeof(struct pw_get_log_entry_t));
  TPM_RC ret = TPM_RC_SUCCESS;
  size_t x = 0;
  for (auto itr = buffer.begin() + sizeof(struct pw_response_header_t);
       itr < buffer.end(); itr += sizeof(struct pw_get_log_entry_t)) {
    const struct pw_get_log_entry_t* entry =
        reinterpret_cast<const struct pw_get_log_entry_t*>(&*itr);
    trunks::PinWeaverLogEntry* proto_entry = &(*log)[x];
    proto_entry->set_label(entry->label.v);
    proto_entry->set_root(entry->root, PW_HASH_SIZE);
    switch (entry->type.v) {
      case PW_INSERT_LEAF:
        proto_entry->mutable_insert_leaf()->set_hmac(entry->leaf_hmac,
                                                     PW_HASH_SIZE);
        break;
      case PW_REMOVE_LEAF:
        proto_entry->mutable_remove_leaf();
        break;
      case PW_TRY_AUTH: {
        auto* ptr = proto_entry->mutable_auth();
        ptr->mutable_timestamp()->set_boot_count(entry->timestamp.boot_count);
        ptr->mutable_timestamp()->set_timer_value(entry->timestamp.timer_value);
        ptr->set_return_code(entry->return_code);
      } break;
      case PW_RESET_TREE:
        proto_entry->mutable_reset_tree();
        break;
      default:
        ret = SAPI_RC_BAD_SEQUENCE;
    }
    ++x;
  }
  return ret;
}

TPM_RC Parse_pw_log_replay_t(const std::string& buffer,
                             uint32_t* result_code,
                             std::string* root_hash,
                             std::string* cred_metadata_out,
                             std::string* mac_out) {
  cred_metadata_out->clear();
  mac_out->clear();

  uint16_t response_length;
  TPM_RC rc = Parse_pw_response_header_t(buffer, result_code, root_hash,
                                         &response_length);
  if (rc != TPM_RC_SUCCESS)
    return rc;

  if (*result_code != 0) {
    return response_length == 0 ? TPM_RC_SUCCESS : SAPI_RC_BAD_SIZE;
  }

  if (response_length < sizeof(struct pw_response_reset_auth_t))
    return SAPI_RC_BAD_SIZE;

  auto itr = buffer.begin() + sizeof(struct pw_response_header_t);

  return Parse_unimported_leaf_data_t(itr, buffer.end(), cred_metadata_out,
                                      mac_out);
}

}  // namespace trunks
