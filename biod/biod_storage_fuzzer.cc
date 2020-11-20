// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_storage.h"

#include <stddef.h>
#include <stdint.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/values.h>
#include <openssl/sha.h>

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

class TestRecord : public biod::BiometricsManager::Record {
 public:
  TestRecord(const std::string& id,
             const std::string& user_id,
             const std::string& label,
             const std::vector<uint8_t>& validation_val,
             const std::vector<uint8_t>& data)
      : id_(id),
        user_id_(user_id),
        label_(label),
        validation_val_(validation_val),
        data_(data) {}

  const std::string& GetId() const override { return id_; }
  const std::string& GetUserId() const override { return user_id_; }
  const std::string& GetLabel() const override { return label_; }
  const std::vector<uint8_t>& GetValidationVal() const override {
    return validation_val_;
  }
  const std::vector<uint8_t>& GetData() const { return data_; }

  bool SetLabel(std::string label) override { return true; }
  bool Remove() override { return true; }
  bool SupportsPositiveMatchSecret() const override { return true; }
  bool NeedsNewValidationValue() const override { return false; }

 private:
  std::string id_;
  std::string user_id_;
  std::string label_;
  std::vector<uint8_t> validation_val_;
  std::vector<uint8_t> data_;
};

static std::vector<TestRecord> records;

static bool LoadRecord(int record_format_version,
                       const std::string& user_id,
                       const std::string& label,
                       const std::string& record_id,
                       const std::vector<uint8_t>& validation_val,
                       const base::Value& data_value) {
  records.push_back(TestRecord(record_id, user_id, label, validation_val,
                               data_value.GetBlob()));
  return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  int MAX_LEN = 255;
  int MAX_DATA_LEN = 45000;

  FuzzedDataProvider data_provider(data, size);

  int id_len = data_provider.ConsumeIntegralInRange<int32_t>(1, MAX_LEN);
  int user_id_len = data_provider.ConsumeIntegralInRange<int32_t>(1, MAX_LEN);
  int label_len = data_provider.ConsumeIntegralInRange<int32_t>(1, MAX_LEN);
  int data_len = data_provider.ConsumeIntegralInRange<int32_t>(
      MAX_DATA_LEN - 1000, MAX_DATA_LEN);

  const std::string id = data_provider.ConsumeBytesAsString(id_len);
  const std::string user_id = data_provider.ConsumeBytesAsString(user_id_len);
  const std::string label = data_provider.ConsumeBytesAsString(label_len);
  std::vector<uint8_t> validation_val =
      data_provider.ConsumeBytes<uint8_t>(SHA256_DIGEST_LENGTH);

  std::vector<uint8_t> biod_data;

  if (data_provider.remaining_bytes() > data_len)
    biod_data = data_provider.ConsumeBytes<uint8_t>(data_len);
  else
    biod_data = data_provider.ConsumeRemainingBytes<uint8_t>();

  biod::BiodStorage biod_storage =
      biod::BiodStorage("BiometricsManager", base::Bind(&LoadRecord));
  biod_storage.set_allow_access(true);

  auto record = std::make_unique<TestRecord>(id, user_id, label, validation_val,
                                             biod_data);

  base::FilePath root_path("/tmp/biod_storage_fuzzing_data");
  biod_storage.SetRootPathForTesting(root_path);
  bool status =
      biod_storage.WriteRecord(*record, base::Value(record->GetData()));
  if (status)
    status = biod_storage.ReadRecordsForSingleUser(user_id);

  return 0;
}
