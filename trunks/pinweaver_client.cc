// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// pinweaver_client is a command line tool for executing PinWeaver vendor
// specific commands to Cr50.

#include <base/command_line.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <brillo/syslog_logging.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cinttypes>
#include <memory>

#include "trunks/tpm_pinweaver.h"
#include "trunks/tpm_utility.h"
#include "trunks/trunks_client_test.h"
#include "trunks/trunks_factory_impl.h"

namespace {

enum return_codes {
  EXIT_SUCCESS_RESERVED = EXIT_SUCCESS,
  EXIT_FAILURE_RESERVED = EXIT_FAILURE,
  EXIT_PINWEAVER_NOT_SUPPORTED = 2,
};

const uint8_t DEFAULT_BITS_PER_LEVEL = 2;
const uint8_t DEFAULT_HEIGHT = 6;
const uint8_t DEFAULT_LE_SECRET[PW_SECRET_SIZE] = {
    // clang-format off
    0xba, 0xbc, 0x98, 0x9d, 0x97, 0x20, 0xcf, 0xea,
    0xaa, 0xbd, 0xb2, 0xe3, 0xe0, 0x2c, 0x5c, 0x55,
    0x06, 0x60, 0x93, 0xbd, 0x07, 0xe2, 0xba, 0x92,
    0x10, 0x19, 0x24, 0xb1, 0x29, 0x33, 0x5a, 0xe2
    // clang-format on
};
const uint8_t DEFAULT_HE_SECRET[PW_SECRET_SIZE] = {
    // clang-format off
    0xe3, 0x46, 0xe3, 0x62, 0x01, 0x5d, 0xfe, 0x0a,
    0xd3, 0x67, 0xd7, 0xef, 0xab, 0x01, 0xad, 0x0e,
    0x3a, 0xed, 0xe8, 0x2f, 0x99, 0xd1, 0x2d, 0x13,
    0x4d, 0x4e, 0xe4, 0x02, 0xbe, 0x71, 0x8e, 0x40
    // clang-format on
};
const uint8_t DEFAULT_RESET_SECRET[PW_SECRET_SIZE] = {
    // clang-format off
    0x8c, 0x33, 0x8c, 0xa7, 0x0f, 0x81, 0xa4, 0xee,
    0x24, 0xcd, 0x04, 0x84, 0x9c, 0xa8, 0xfd, 0xdd,
    0x14, 0xb0, 0xad, 0xe6, 0xb7, 0x6a, 0x10, 0xfc,
    0x03, 0x22, 0xcb, 0x71, 0x31, 0xd3, 0x74, 0xd6
    // clang-format on
};

uint8_t protocol_version = PW_PROTOCOL_VERSION;

using trunks::CommandTransceiver;
using trunks::TrunksFactory;
using trunks::TrunksFactoryImpl;

void PrintUsage() {
  puts("Usage:");
  puts("  help - prints this help message.");
  puts("  resettree [<bits_per_level> <height> --protocol=<protocol>]");
  puts("            - sends a reset tree command.");
  puts("      The default parameters are bits_per_level=2 height=6 protocol=");
  puts("      PW_PROTOCOL_VERSION.");
  puts("  insert [...] - sends an insert leaf command.");
  puts("  remove [...] - sends an remove leaf command.");
  puts("  auth [...] - sends an try auth command.");
  puts("  resetleaf [...] - sends an reset auth command.");
  puts("  getlog [...] - sends an get log command.");
  puts("  replay [...] - sends an log replay command.");
  puts("  selftest [--protocol=<version>] - runs a self test with the");
  puts("           following commands:");
}

std::string HexEncode(const std::string& bytes) {
  return base::HexEncode(bytes.data(), bytes.size());
}

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}

std::string PwErrorStr(int code) {
  switch (code) {
    case 0:
      return "EC_SUCCESS";
    case 1:
      return "EC_ERROR_UNKNOWN";
    case 2:
      return "EC_ERROR_UNIMPLEMENTED";
    case PW_ERR_VERSION_MISMATCH:
      return "PW_ERR_VERSION_MISMATCH";
    case PW_ERR_LENGTH_INVALID:
      return "PW_ERR_LENGTH_INVALID";
    case PW_ERR_TYPE_INVALID:
      return "PW_ERR_TYPE_INVALID";
    case PW_ERR_BITS_PER_LEVEL_INVALID:
      return "PW_ERR_BITS_PER_LEVEL_INVALID";
    case PW_ERR_HEIGHT_INVALID:
      return "PW_ERR_HEIGHT_INVALID";
    case PW_ERR_LABEL_INVALID:
      return "PW_ERR_LABEL_INVALID";
    case PW_ERR_DELAY_SCHEDULE_INVALID:
      return "PW_ERR_DELAY_SCHEDULE_INVALID";
    case PW_ERR_PATH_AUTH_FAILED:
      return "PW_ERR_PATH_AUTH_FAILED";
    case PW_ERR_LEAF_VERSION_MISMATCH:
      return "PW_ERR_LEAF_VERSION_MISMATCH";
    case PW_ERR_HMAC_AUTH_FAILED:
      return "PW_ERR_HMAC_AUTH_FAILED";
    case PW_ERR_LOWENT_AUTH_FAILED:
      return "PW_ERR_LOWENT_AUTH_FAILED";
    case PW_ERR_RESET_AUTH_FAILED:
      return "PW_ERR_RESET_AUTH_FAILED";
    case PW_ERR_CRYPTO_FAILURE:
      return "PW_ERR_CRYPTO_FAILURE";
    case PW_ERR_RATE_LIMIT_REACHED:
      return "PW_ERR_RATE_LIMIT_REACHED";
    case PW_ERR_ROOT_NOT_FOUND:
      return "PW_ERR_ROOT_NOT_FOUND";
    case PW_ERR_NV_EMPTY:
      return "PW_ERR_NV_EMPTY";
    case PW_ERR_NV_LENGTH_MISMATCH:
      return "PW_ERR_NV_LENGTH_MISMATCH";
    case PW_ERR_NV_VERSION_MISMATCH:
      return "PW_ERR_NV_VERSION_MISMATCH";
    default:
      return "?";
  }
}

void GetEmptyPath(uint8_t bits_per_level, uint8_t height, std::string* h_aux) {
  static_assert(SHA256_DIGEST_SIZE >= PW_HASH_SIZE, "");
  std::vector<uint8_t> hash(SHA256_DIGEST_SIZE, 0);
  uint8_t num_siblings = (1 << bits_per_level) - 1;
  size_t level_size = num_siblings * PW_HASH_SIZE;

  h_aux->resize(height * level_size);

  for (auto level_ptr = h_aux->begin(); level_ptr < h_aux->end();
       level_ptr += level_size) {
    for (auto index_ptr = level_ptr; index_ptr < level_ptr + level_size;
         index_ptr += PW_HASH_SIZE) {
      std::copy(hash.begin(), hash.begin() + PW_HASH_SIZE, index_ptr);
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    for (uint8_t x = 0; x <= num_siblings; ++x) {
      SHA256_Update(&ctx, hash.data(), PW_HASH_SIZE);
    }
    SHA256_Final(hash.data(), &ctx);
  }
}

void GetInsertLeafDefaults(uint64_t* label,
                           std::string* h_aux,
                           brillo::SecureBlob* le_secret,
                           brillo::SecureBlob* he_secret,
                           brillo::SecureBlob* reset_secret,
                           std::map<uint32_t, uint32_t>* delay_schedule,
                           trunks::ValidPcrCriteria* valid_pcr_criteria) {
  *label = 0x1b1llu;  // {0, 1, 2, 3, 0, 1}

  GetEmptyPath(DEFAULT_BITS_PER_LEVEL, DEFAULT_HEIGHT, h_aux);
  le_secret->assign(DEFAULT_LE_SECRET,
                    DEFAULT_LE_SECRET + sizeof(DEFAULT_LE_SECRET));
  he_secret->assign(DEFAULT_HE_SECRET,
                    DEFAULT_HE_SECRET + sizeof(DEFAULT_HE_SECRET));
  reset_secret->assign(DEFAULT_RESET_SECRET,
                       DEFAULT_RESET_SECRET + sizeof(DEFAULT_RESET_SECRET));
  delay_schedule->clear();
  delay_schedule->emplace(5, 20);
  delay_schedule->emplace(6, 60);
  delay_schedule->emplace(7, 300);
  delay_schedule->emplace(8, 600);
  delay_schedule->emplace(9, 1800);
  delay_schedule->emplace(10, 3600);
  delay_schedule->emplace(50, PW_BLOCK_ATTEMPTS);
  valid_pcr_criteria->Clear();
  if (protocol_version > 0) {
    trunks::ValidPcrValue* default_pcr_value =
        valid_pcr_criteria->add_valid_pcr_values();
    uint8_t bitmask[2]{0, 0};
    default_pcr_value->set_bitmask(&bitmask, sizeof(bitmask));
  }
}

void SetupBaseOutcome(uint32_t result_code,
                      const std::string& root,
                      base::DictionaryValue* outcome) {
  // This is exported as a string because the API handles integers as signed.
  outcome->SetString("result_code.value", std::to_string(result_code));
  outcome->SetString("result_code.name", PwErrorStr(result_code));
  outcome->SetString("root_hash", HexEncode(root));
}

std::string GetOutcomeJson(const base::DictionaryValue& outcome) {
  std::string json;
  base::JSONWriter::WriteWithOptions(
      outcome, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

int HandleResetTree(base::CommandLine::StringVector::const_iterator begin,
                    base::CommandLine::StringVector::const_iterator end,
                    TrunksFactoryImpl* factory) {
  uint8_t bits_per_level;
  uint8_t height;
  if (begin == end) {
    bits_per_level = DEFAULT_BITS_PER_LEVEL;
    height = DEFAULT_HEIGHT;
  } else if (end - begin != 2) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else {
    bits_per_level = std::stoi(begin[0]);
    height = std::stoi(begin[1]);
  }

  uint32_t result_code = 0;
  std::string root;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverResetTree(
      protocol_version, bits_per_level, height, &result_code, &root);

  if (result) {
    LOG(ERROR) << "PinWeaverResetTree: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleInsert(base::CommandLine::StringVector::const_iterator begin,
                 base::CommandLine::StringVector::const_iterator end,
                 TrunksFactoryImpl* factory) {
  uint64_t label;
  std::string h_aux;
  brillo::SecureBlob le_secret;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  std::map<uint32_t, uint32_t> delay_schedule;
  trunks::ValidPcrCriteria valid_pcr_criteria;
  if (begin == end) {
    GetInsertLeafDefaults(&label, &h_aux, &le_secret, &he_secret, &reset_secret,
                          &delay_schedule, &valid_pcr_criteria);
  } else if (end - begin < 6) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else {
    label = std::stoul(begin[0]);

    std::vector<uint8_t> bytes;
    if (!base::HexStringToBytes(begin[1], &bytes))
      return EXIT_FAILURE;
    h_aux.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[2], &bytes))
      return EXIT_FAILURE;
    le_secret.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[3], &bytes))
      return EXIT_FAILURE;
    he_secret.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[4], &bytes))
      return EXIT_FAILURE;
    reset_secret.assign(bytes.begin(), bytes.end());

    begin += 5;
    for (size_t x = 0; x < end - begin; x += 2) {
      delay_schedule.emplace(static_cast<uint32_t>(std::stoul(begin[x])),
                             static_cast<uint32_t>(std::stoul(begin[x + 1])));
    }
  }

  uint32_t result_code = 0;
  std::string root;
  std::string cred_metadata;
  std::string mac;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverInsertLeaf(
      protocol_version, label, h_aux, le_secret, he_secret, reset_secret,
      delay_schedule, valid_pcr_criteria, &result_code, &root, &cred_metadata,
      &mac);

  if (result) {
    LOG(ERROR) << "PinWeaverInsertLeaf: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  outcome.SetString("cred_metadata", HexEncode(cred_metadata));
  outcome.SetString("mac", HexEncode(mac));
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleRemove(base::CommandLine::StringVector::const_iterator begin,
                 base::CommandLine::StringVector::const_iterator end,
                 TrunksFactoryImpl* factory) {
  if (end - begin != 3) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  }
  uint64_t label = std::stoul(begin[0]);

  std::vector<uint8_t> bytes;
  if (!base::HexStringToBytes(begin[1], &bytes))
    return EXIT_FAILURE;
  std::string h_aux(bytes.begin(), bytes.end());

  bytes.clear();
  if (!base::HexStringToBytes(begin[2], &bytes))
    return EXIT_FAILURE;
  std::string mac(bytes.begin(), bytes.end());

  uint32_t result_code = 0;
  std::string root;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverRemoveLeaf(
      protocol_version, label, h_aux, mac, &result_code, &root);

  if (result) {
    LOG(ERROR) << "PinWeaverRemoveLeaf: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleAuth(base::CommandLine::StringVector::const_iterator begin,
               base::CommandLine::StringVector::const_iterator end,
               TrunksFactoryImpl* factory) {
  std::string h_aux;
  brillo::SecureBlob le_secret;
  std::string cred_metadata;
  if (end - begin != 3) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else {
    std::vector<uint8_t> bytes;
    if (!base::HexStringToBytes(begin[0], &bytes))
      return EXIT_FAILURE;
    h_aux.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[1], &bytes))
      return EXIT_FAILURE;
    le_secret.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[2], &bytes))
      return EXIT_FAILURE;
    cred_metadata.assign(bytes.begin(), bytes.end());
  }

  uint32_t result_code = 0;
  std::string root;
  uint32_t seconds_to_wait;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  std::string cred_metadata_out;
  std::string mac_out;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverTryAuth(
      protocol_version, le_secret, h_aux, cred_metadata, &result_code, &root,
      &seconds_to_wait, &he_secret, &reset_secret, &cred_metadata_out,
      &mac_out);

  if (result) {
    LOG(ERROR) << "PinWeaverTryAuth: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  outcome.SetString("seconds_to_wait", std::to_string(seconds_to_wait));
  outcome.SetString("he_secret", HexEncode(he_secret.to_string()));
  outcome.SetString("cred_metadata", HexEncode(cred_metadata_out));
  outcome.SetString("mac", HexEncode(mac_out));
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleResetLeaf(base::CommandLine::StringVector::const_iterator begin,
                    base::CommandLine::StringVector::const_iterator end,
                    TrunksFactoryImpl* factory) {
  std::string h_aux;
  brillo::SecureBlob reset_secret;
  std::string cred_metadata;
  if (end - begin != 3) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else {
    std::vector<uint8_t> bytes;
    if (!base::HexStringToBytes(begin[0], &bytes))
      return EXIT_FAILURE;
    h_aux.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[1], &bytes))
      return EXIT_FAILURE;
    reset_secret.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[2], &bytes))
      return EXIT_FAILURE;
    cred_metadata.assign(bytes.begin(), bytes.end());
  }

  uint32_t result_code = 0;
  std::string root;
  brillo::SecureBlob he_secret;
  std::string cred_metadata_out;
  std::string mac_out;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverResetAuth(
      protocol_version, reset_secret, h_aux, cred_metadata, &result_code, &root,
      &he_secret, &cred_metadata_out, &mac_out);

  if (result) {
    LOG(ERROR) << "PinWeaverResetAuth: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  outcome.SetString("he_secret", HexEncode(he_secret.to_string()));
  outcome.SetString("cred_metadata", HexEncode(cred_metadata_out));
  outcome.SetString("mac", HexEncode(mac_out));
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleGetLog(base::CommandLine::StringVector::const_iterator begin,
                 base::CommandLine::StringVector::const_iterator end,
                 TrunksFactoryImpl* factory) {
  std::string root;
  if (end - begin > 1) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else if (end == begin) {
    root.assign(static_cast<size_t>(SHA256_DIGEST_SIZE), '\0');
  } else {
    std::vector<uint8_t> bytes;
    if (!base::HexStringToBytes(begin[0], &bytes))
      return EXIT_FAILURE;
    root.assign(bytes.begin(), bytes.end());
  }

  uint32_t result_code = 0;
  std::string root_hash;
  std::vector<trunks::PinWeaverLogEntry> log;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverGetLog(
      protocol_version, root, &result_code, &root_hash, &log);

  if (result) {
    LOG(ERROR) << "PinWeaverGetLog: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);

  auto out_entries = std::make_unique<base::ListValue>();
  for (const auto& entry : log) {
    auto out_entry = std::make_unique<base::DictionaryValue>();
    out_entry->SetString("label", std::to_string(entry.label()));
    out_entry->SetString("root", HexEncode(entry.root()));
    switch (entry.type_case()) {
      case trunks::PinWeaverLogEntry::TypeCase::kInsertLeaf:
        out_entry->SetString("type", "InsertLeaf");
        out_entry->SetString("hmac", HexEncode(entry.insert_leaf().hmac()));
        break;
      case trunks::PinWeaverLogEntry::TypeCase::kRemoveLeaf:
        out_entry->SetString("type", "RemoveLeaf");
        break;
      case trunks::PinWeaverLogEntry::TypeCase::kAuth:
        out_entry->SetString("type", "Auth");
        out_entry->SetString(
            "timestamp.boot_count",
            std::to_string(entry.auth().timestamp().boot_count()));
        out_entry->SetString(
            "timestamp.timer_value",
            std::to_string(entry.auth().timestamp().timer_value()));
        out_entry->SetString("return_code.value",
                             std::to_string(entry.auth().return_code()));
        out_entry->SetString(
            "return_code.name",
            trunks::GetErrorString(entry.auth().return_code()));
        break;
      case trunks::PinWeaverLogEntry::TypeCase::kResetTree:
        out_entry->SetString("type", "ResetTree");
        break;
      default:
        out_entry->SetString("type", std::to_string(entry.type_case()));
    }
    out_entries->Append(std::move(out_entry));
  }
  outcome.Set("entries", std::move(out_entries));
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleReplay(base::CommandLine::StringVector::const_iterator begin,
                 base::CommandLine::StringVector::const_iterator end,
                 TrunksFactoryImpl* factory) {
  std::string h_aux;
  std::string log_root;
  std::string cred_metadata;
  if (end - begin != 3) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  } else {
    std::vector<uint8_t> bytes;
    if (!base::HexStringToBytes(begin[0], &bytes))
      return EXIT_FAILURE;
    h_aux.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[1], &bytes))
      return EXIT_FAILURE;
    log_root.assign(bytes.begin(), bytes.end());

    bytes.clear();
    if (!base::HexStringToBytes(begin[2], &bytes))
      return EXIT_FAILURE;
    cred_metadata.assign(bytes.begin(), bytes.end());
  }

  uint32_t result_code = 0;
  std::string root;
  std::string cred_metadata_out;
  std::string mac_out;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result = tpm_utility->PinWeaverLogReplay(
      protocol_version, log_root, h_aux, cred_metadata, &result_code, &root,
      &cred_metadata_out, &mac_out);

  if (result) {
    LOG(ERROR) << "PinWeaverResetAuth: " << trunks::GetErrorString(result);
  }

  base::DictionaryValue outcome;
  SetupBaseOutcome(result_code, root, &outcome);
  outcome.SetString("cred_metadata", HexEncode(cred_metadata_out));
  outcome.SetString("mac", HexEncode(mac_out));
  puts(GetOutcomeJson(outcome).c_str());
  return result;
}

int HandleSelfTest(base::CommandLine::StringVector::const_iterator begin,
                   base::CommandLine::StringVector::const_iterator end,
                   TrunksFactoryImpl* factory) {
  if (begin != end) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "reset_tree";
  uint32_t result_code = 0;
  std::string root;
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory->GetTpmUtility();
  trunks::TPM_RC result =
      tpm_utility->PinWeaverResetTree(protocol_version, DEFAULT_BITS_PER_LEVEL,
                                      DEFAULT_HEIGHT, &result_code, &root);
  if (result || result_code) {
    LOG(ERROR) << "reset_tree failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  LOG(INFO) << "insert_leaf";
  result_code = 0;
  uint64_t label;
  std::string h_aux;
  brillo::SecureBlob le_secret;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  brillo::SecureBlob test_reset_secret;
  std::map<uint32_t, uint32_t> delay_schedule;
  trunks::ValidPcrCriteria valid_pcr_criteria;
  GetInsertLeafDefaults(&label, &h_aux, &le_secret, &he_secret, &reset_secret,
                        &delay_schedule, &valid_pcr_criteria);
  std::string cred_metadata;
  std::string mac;
  result = tpm_utility->PinWeaverInsertLeaf(
      protocol_version, label, h_aux, le_secret, he_secret, reset_secret,
      delay_schedule, valid_pcr_criteria, &result_code, &root, &cred_metadata,
      &mac);
  if (result || result_code) {
    LOG(ERROR) << "insert_leaf failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  LOG(INFO) << "try_auth auth success";
  result_code = 0;
  uint32_t seconds_to_wait;
  result = tpm_utility->PinWeaverTryAuth(
      protocol_version, le_secret, h_aux, cred_metadata, &result_code, &root,
      &seconds_to_wait, &he_secret, &test_reset_secret, &cred_metadata, &mac);
  if (result || result_code) {
    LOG(ERROR) << "try_auth failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  if (he_secret.size() != PW_SECRET_SIZE ||
      std::mismatch(he_secret.begin(), he_secret.end(), DEFAULT_HE_SECRET)
              .first != he_secret.end()) {
    LOG(ERROR) << "try_auth credential retrieval failed!";
    return EXIT_FAILURE;
  }

  if (protocol_version > 0 &&
      (test_reset_secret.size() != PW_SECRET_SIZE ||
       std::mismatch(test_reset_secret.begin(), test_reset_secret.end(),
                     DEFAULT_RESET_SECRET)
               .first != test_reset_secret.end())) {
    LOG(ERROR) << "try_auth reset_secret retrieval failed!";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "try_auth auth fail";
  result_code = 0;
  std::string pre_fail_root = root;
  std::string old_metadata = cred_metadata;
  brillo::SecureBlob wrong_le_secret = he_secret;
  result = tpm_utility->PinWeaverTryAuth(
      protocol_version, wrong_le_secret, h_aux, cred_metadata, &result_code,
      &root, &seconds_to_wait, &he_secret, &test_reset_secret, &cred_metadata,
      &mac);
  if (result) {
    LOG(ERROR) << "try_auth failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }
  // Most of the checks covered by the unit tests don't make sense to test here,
  // but since authentication is critical this check is justified.
  if (result_code != PW_ERR_LOWENT_AUTH_FAILED) {
    LOG(ERROR) << "try_auth verification failed!";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "get_log";
  result_code = 0;
  std::vector<trunks::PinWeaverLogEntry> log;
  result = tpm_utility->PinWeaverGetLog(protocol_version, pre_fail_root,
                                        &result_code, &root, &log);
  if (result || result_code) {
    LOG(ERROR) << "get_log failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }
  bool fail = false;
  if (log.empty()) {
    LOG(ERROR) << "get_log verification failed: empty log!";
    fail = true;
  }
  if (log.front().root() != root) {
    LOG(ERROR) << "get_log verification failed: wrong root!";
    LOG(ERROR) << HexEncode(log.front().root());
    fail = true;
  }
  if (log.front().type_case() != trunks::PinWeaverLogEntry::TypeCase::kAuth) {
    LOG(ERROR) << "get_log verification failed: wrong entry type!";
    LOG(ERROR) << log.front().type_case();
    fail = true;
  }
  if (log.front().auth().return_code() != PW_ERR_LOWENT_AUTH_FAILED) {
    LOG(ERROR) << "get_log verification failed: wrong return code!";
    LOG(ERROR) << PwErrorStr(log.front().auth().return_code());
    fail = true;
  }
  if (fail) {
    return EXIT_FAILURE;
  }

  LOG(INFO) << "log_replay";
  result_code = 0;
  std::string replay_metadata = cred_metadata;
  std::string replay_mac = mac;
  result = tpm_utility->PinWeaverLogReplay(protocol_version, root, h_aux,
                                           old_metadata, &result_code, &root,
                                           &replay_metadata, &replay_mac);
  if (result) {
    LOG(ERROR) << "log_replay failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }
  if (replay_metadata != cred_metadata) {
    LOG(ERROR) << "log_replay verification failed: bad metadata!";
    return EXIT_FAILURE;
  }
  if (replay_mac != mac) {
    LOG(ERROR) << "log_replay verification failed: bad HMAC!";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "reset_auth";
  result_code = 0;
  result = tpm_utility->PinWeaverResetAuth(
      protocol_version, reset_secret, h_aux, cred_metadata, &result_code, &root,
      &he_secret, &cred_metadata, &mac);
  if (result || result_code) {
    LOG(ERROR) << "reset_auth failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  if (he_secret.size() != PW_SECRET_SIZE ||
      std::mismatch(he_secret.begin(), he_secret.end(), DEFAULT_HE_SECRET)
              .first != he_secret.end()) {
    LOG(ERROR) << "reset_auth credential retrieval failed!";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "remove_leaf";
  result_code = 0;
  result = tpm_utility->PinWeaverRemoveLeaf(protocol_version, label, h_aux, mac,
                                            &result_code, &root);
  if (result || result_code) {
    LOG(ERROR) << "remove_leaf failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  LOG(INFO) << "insert new leaf with good PCR (PCR4 must be empty)";
  GetInsertLeafDefaults(&label, &h_aux, &le_secret, &he_secret, &reset_secret,
                        &delay_schedule, &valid_pcr_criteria);
  if (protocol_version > 0) {
    std::string digest = HexDecode(
        "66687AADF862BD776C8FC18B8E9F8E20089714856EE233B3902A591D0D5F2925");
    trunks::ValidPcrValue* value =
        valid_pcr_criteria.mutable_valid_pcr_values(0);
    const uint8_t bitmask[2] = {1 << 4 /* PCR 4 */, 0};
    value->set_bitmask(&bitmask, sizeof(bitmask));
    value->set_digest(digest);
  }
  result = tpm_utility->PinWeaverInsertLeaf(
      protocol_version, label, h_aux, le_secret, he_secret, reset_secret,
      delay_schedule, valid_pcr_criteria, &result_code, &root, &cred_metadata,
      &mac);
  if (result || result_code) {
    LOG(ERROR) << "insert_leaf failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  LOG(INFO) << "try_auth should succeed";
  result_code = 0;
  he_secret.clear();
  result = tpm_utility->PinWeaverTryAuth(
      protocol_version, le_secret, h_aux, cred_metadata, &result_code, &root,
      &seconds_to_wait, &he_secret, &reset_secret, &cred_metadata, &mac);
  if (result || result_code) {
    LOG(ERROR) << "try_auth failed";
    return EXIT_FAILURE;
  }

  if (he_secret.size() != PW_SECRET_SIZE ||
      std::mismatch(he_secret.begin(), he_secret.end(), DEFAULT_HE_SECRET)
              .first != he_secret.end()) {
    LOG(ERROR) << "try_auth credential retrieval failed!";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "remove_leaf";
  result_code = 0;
  result = tpm_utility->PinWeaverRemoveLeaf(protocol_version, label, h_aux, mac,
                                            &result_code, &root);
  if (result || result_code) {
    LOG(ERROR) << "remove_leaf failed! " << result_code << " "
               << PwErrorStr(result_code);
    return EXIT_FAILURE;
  }

  if (protocol_version > 0) {
    LOG(INFO) << "insert new leaf with bad PCR";
    GetInsertLeafDefaults(&label, &h_aux, &le_secret, &he_secret, &reset_secret,
                          &delay_schedule, &valid_pcr_criteria);
    trunks::ValidPcrValue* value =
        valid_pcr_criteria.mutable_valid_pcr_values(0);
    const uint8_t bitmask[2] = {16, 0};
    value->set_bitmask(&bitmask, sizeof(bitmask));
    value->set_digest("bad_digest");
    result = tpm_utility->PinWeaverInsertLeaf(
        protocol_version, label, h_aux, le_secret, he_secret, reset_secret,
        delay_schedule, valid_pcr_criteria, &result_code, &root, &cred_metadata,
        &mac);
    if (result || result_code) {
      LOG(ERROR) << "insert_leaf failed! " << result_code << " "
                 << PwErrorStr(result_code);
      return EXIT_FAILURE;
    }

    LOG(INFO) << "try_auth should fail";
    result_code = 0;
    he_secret.clear();
    replay_mac = mac;
    result = tpm_utility->PinWeaverTryAuth(
        protocol_version, le_secret, h_aux, cred_metadata, &result_code, &root,
        &seconds_to_wait, &he_secret, &test_reset_secret, &cred_metadata, &mac);
    if (!result && !result_code) {
      LOG(ERROR) << "try_auth with wrong PCR failed to fail";
      return EXIT_FAILURE;
    }

    // Make sure that he_secret was not leaked.
    if (he_secret.size() > 0 || test_reset_secret.size() > 0) {
      LOG(ERROR) << "try_auth populated the he_secret";
      return EXIT_FAILURE;
    }

    LOG(INFO) << "remove_leaf";
    result_code = 0;
    result = tpm_utility->PinWeaverRemoveLeaf(protocol_version, label, h_aux,
                                              replay_mac, &result_code, &root);
    if (result || result_code) {
      LOG(ERROR) << "remove_leaf failed! " << result_code << " "
                 << PwErrorStr(result_code);
      return EXIT_FAILURE;
    }
  }

  puts("Success!");
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int requested_protocol = PW_PROTOCOL_VERSION;
  if (cl->HasSwitch("protocol")) {
    requested_protocol = std::min(
        PW_PROTOCOL_VERSION, std::stoi(cl->GetSwitchValueASCII("protocol")));
  }
  const auto& args = cl->GetArgs();

  if (args.size() < 1) {
    puts("Invalid options!");
    PrintUsage();
    return EXIT_FAILURE;
  }

  const auto& command = args[0];

  if (command == "help") {
    puts("Pinweaver Client: A command line tool to invoke PinWeaver on Cr50.");
    PrintUsage();
    return EXIT_SUCCESS;
  }

  TrunksFactoryImpl factory;
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";

  {
    std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();
    trunks::TPM_RC result = tpm_utility->PinWeaverIsSupported(
        requested_protocol, &protocol_version);
    if (result == trunks::SAPI_RC_ABI_MISMATCH) {
      result = tpm_utility->PinWeaverIsSupported(0, &protocol_version);
    }
    if (result) {
      LOG(ERROR) << "PinWeaver is not supported on this device!";
      return EXIT_PINWEAVER_NOT_SUPPORTED;
    }
    protocol_version = std::min(protocol_version, (uint8_t)requested_protocol);
    LOG(INFO) << "Protocol version: " << static_cast<int>(protocol_version);
  }

  auto command_args_start = args.begin() + 1;

  const struct {
    const std::string command;
    int (*handler)(base::CommandLine::StringVector::const_iterator begin,
                   base::CommandLine::StringVector::const_iterator end,
                   TrunksFactoryImpl* factory);
  } command_handlers[] = {
      // clang-format off
      {"resettree", HandleResetTree},
      {"insert", HandleInsert},
      {"remove", HandleRemove},
      {"auth", HandleAuth},
      {"resetleaf", HandleResetLeaf},
      {"getlog", HandleGetLog},
      {"replay", HandleReplay},
      {"selftest", HandleSelfTest},
      // clang-format on
  };

  for (const auto& command_handler : command_handlers) {
    if (command_handler.command == command) {
      return command_handler.handler(command_args_start, args.end(), &factory);
    }
  }

  puts("Invalid options!");
  PrintUsage();
  return EXIT_FAILURE;
}
