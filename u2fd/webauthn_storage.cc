// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_storage.h"

#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/json/json_reader.h>
#include <base/json/json_string_value_serializer.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>

#include "brillo/scoped_umask.h"

namespace u2f {

using base::FilePath;

namespace {

constexpr const char kDaemonStorePath[] = "/run/daemon-store/u2f";
constexpr const char kWebAuthnDirName[] = "webauthn";
constexpr const char kRecordFileNamePrefix[] = "Record_";

// Members of the JSON file
constexpr const char kCredentialIdKey[] = "credential_id";
constexpr const char kSecretKey[] = "secret";
constexpr const char kRpIdKey[] = "rp_id";
constexpr const char kUserIdKey[] = "user_id";
// TODO(yichengli): Add user display name field.
constexpr const char kCreatedTimestampKey[] = "created";

}  // namespace

WebAuthnStorage::WebAuthnStorage() : root_path_(kDaemonStorePath) {}
WebAuthnStorage::~WebAuthnStorage() = default;

bool WebAuthnStorage::WriteRecord(const WebAuthnRecord& record) {
  DCHECK(allow_access_ && !sanitized_user_.empty());

  const std::string credential_id_hex =
      base::HexEncode(record.credential_id.data(), record.credential_id.size());

  if (record.secret.size() != kCredentialSecretSize) {
    LOG(ERROR) << "Wrong secret size in record with id " << credential_id_hex;
    return false;
  }

  base::Value record_value(base::Value::Type::DICTIONARY);
  record_value.SetStringKey(kCredentialIdKey, credential_id_hex);
  record_value.SetStringKey(kSecretKey, base::Base64Encode(record.secret));
  record_value.SetStringKey(kRpIdKey, record.rp_id);
  record_value.SetStringKey(kUserIdKey, record.user_id);
  record_value.SetDoubleKey(kCreatedTimestampKey, record.timestamp);

  std::string json_string;
  JSONStringValueSerializer json_serializer(&json_string);
  if (!json_serializer.Serialize(record_value)) {
    LOG(ERROR) << "Failed to serialize record with id " << credential_id_hex
               << " to JSON.";
    return false;
  }

  std::vector<FilePath> paths = {
      FilePath(sanitized_user_), FilePath(kWebAuthnDirName),
      FilePath(kRecordFileNamePrefix + credential_id_hex)};

  FilePath record_storage_filename = root_path_;
  for (const auto& path : paths) {
    DCHECK(!path.IsAbsolute());
    record_storage_filename = record_storage_filename.Append(path);
  }

  {
    brillo::ScopedUmask owner_only_umask(~(0700));

    if (!base::CreateDirectory(record_storage_filename.DirName())) {
      PLOG(ERROR) << "Cannot create directory: "
                  << record_storage_filename.DirName().value() << ".";
      return false;
    }
  }

  {
    brillo::ScopedUmask owner_only_umask(~(0600));

    if (!base::ImportantFileWriter::WriteFileAtomically(record_storage_filename,
                                                        json_string)) {
      LOG(ERROR) << "Failed to write JSON file: "
                 << record_storage_filename.value() << ".";
      return false;
    }
  }

  LOG(INFO) << "Done writing record with id " << credential_id_hex
            << " to file successfully. ";

  records_.emplace_back(record);
  return true;
}

bool WebAuthnStorage::LoadRecords() {
  DCHECK(allow_access_ && !sanitized_user_.empty());

  FilePath webauthn_path =
      root_path_.Append(sanitized_user_).Append(kWebAuthnDirName);
  base::FileEnumerator enum_records(webauthn_path, false,
                                    base::FileEnumerator::FILES,
                                    std::string(kRecordFileNamePrefix) + "*");
  bool read_all_records_successfully = true;
  for (FilePath record_path = enum_records.Next(); !record_path.empty();
       record_path = enum_records.Next()) {
    std::string json_string;
    if (!base::ReadFileToString(record_path, &json_string)) {
      LOG(ERROR) << "Failed to read the string from " << record_path.value()
                 << ".";
      read_all_records_successfully = false;
      continue;
    }

    auto record_value = base::JSONReader::ReadAndReturnValueWithError(
        json_string, base::JSON_ALLOW_TRAILING_COMMAS);

    if (!record_value.value) {
      LOG_IF(ERROR, record_value.error_code)
          << "Error in deserializing JSON from path " << record_path.value()
          << " with code " << record_value.error_code << ".";
      LOG_IF(ERROR, !record_value.error_message.empty())
          << "JSON error message: " << record_value.error_message << ".";
      read_all_records_successfully = false;
      continue;
    }

    if (!record_value.value->is_dict()) {
      LOG(ERROR) << "Value " << record_path.value() << " is not a dictionary.";
      read_all_records_successfully = false;
      continue;
    }
    base::Value record_dictionary = std::move(*record_value.value);

    const std::string* credential_id_hex =
        record_dictionary.FindStringKey(kCredentialIdKey);
    std::string credential_id;
    if (!credential_id_hex ||
        !base::HexStringToString(*credential_id_hex, &credential_id)) {
      LOG(ERROR) << "Cannot read credential_id from " << record_path.value()
                 << ".";
      read_all_records_successfully = false;
      continue;
    }

    const std::string* secret_base64 =
        record_dictionary.FindStringKey(kSecretKey);
    std::string secret;
    if (!secret_base64 || !base::Base64Decode(*secret_base64, &secret)) {
      LOG(ERROR) << "Cannot read credential secret from " << record_path.value()
                 << ".";
      read_all_records_successfully = false;
      continue;
    }
    const brillo::SecureBlob secret_blob(secret);

    const std::string* rp_id = record_dictionary.FindStringKey(kRpIdKey);
    if (!rp_id) {
      LOG(ERROR) << "Cannot read rp_id from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    const std::string* user_id = record_dictionary.FindStringKey(kUserIdKey);
    if (!user_id) {
      LOG(ERROR) << "Cannot read user_id from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    const base::Optional<double> timestamp =
        record_dictionary.FindDoubleKey(kCreatedTimestampKey);
    if (!timestamp) {
      LOG(ERROR) << "Cannot read timestamp from " << record_path.value() << ".";
      read_all_records_successfully = false;
      continue;
    }

    records_.emplace_back(WebAuthnRecord{credential_id, secret_blob, *rp_id,
                                         *user_id, *timestamp});
  }
  LOG(INFO) << "Loaded " << records_.size() << " WebAuthn records to memory.";
  return read_all_records_successfully;
}

base::Optional<brillo::SecureBlob> WebAuthnStorage::GetSecretByCredentialId(
    const std::string& credential_id) {
  for (const WebAuthnRecord& record : records_) {
    if (record.credential_id == credential_id) {
      return record.secret;
    }
  }
  return base::nullopt;
}

base::Optional<WebAuthnRecord> WebAuthnStorage::GetRecordByCredentialId(
    const std::string& credential_id) {
  for (const WebAuthnRecord& record : records_) {
    if (record.credential_id == credential_id) {
      return record;
    }
  }
  return base::nullopt;
}

void WebAuthnStorage::Reset() {
  allow_access_ = false;
  sanitized_user_.clear();
  records_.clear();
}

void WebAuthnStorage::SetRootPathForTesting(const base::FilePath& root_path) {
  root_path_ = root_path;
}

}  // namespace u2f
