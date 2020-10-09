// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_STORAGE_H_
#define BIOD_BIOD_STORAGE_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/scoped_umask.h>

#include "biod/biometrics_manager.h"

namespace biod {

// Version of the record format.
constexpr int kRecordFormatVersion = 2;
constexpr int kRecordFormatVersionNoValidationValue = 1;

class BiodStorage {
 public:
  using ReadRecordsCallback =
      base::Callback<bool(int record_format_version,
                          const std::string& user_id,
                          const std::string& label,
                          const std::string& record_id,
                          const std::vector<uint8_t>& validation_val,
                          const base::Value& data)>;

  // Constructor set the file path to be
  // /home/root/<hash of user id>/biod/<BiometricsManager>/RecordUUID.
  // Callback load_record is run synchronously as each record is read back from
  // storage and loaded into biod.
  // TODO(mqg): Instead of callback, update ReadRecords to use out-parameter or
  // lambda.
  BiodStorage(const std::string& biometrics_manager_name,
              const ReadRecordsCallback& load_record);

  // Set root path to a different path for testing purpose only
  void SetRootPathForTesting(const base::FilePath& root_path);

  // Write one record to file in per user stateful. This is called whenever
  // we enroll a new record.
  bool WriteRecord(const BiometricsManager::Record& record, base::Value data);

  // Read validation value from |record_dictionary| and store in |output|.
  static std::unique_ptr<std::vector<uint8_t>> ReadValidationValueFromRecord(
      int record_format_version,
      const base::Value& record_dictionary,
      const base::FilePath& record_path);

  // Read all records from file for all users in the set. Called whenever biod
  // starts or when a new user logs in.
  bool ReadRecords(const std::unordered_set<std::string>& user_ids);

  // Read all records from disk for a single user. Uses a file enumerator to
  // enumerate through all record files. Called whenever biod starts or when
  // a new user logs in.
  bool ReadRecordsForSingleUser(const std::string& user_id);

  // Delete one record file. User will be able to do this via UI. True if
  // this record does not exist on disk.
  bool DeleteRecord(const std::string& user_id, const std::string& record_id);

  // Generate a uuid with guid.h for each record. Uuid is 128 bit number,
  // which is then turned into a string of format
  // xxxxxxxx_xxxx_xxxx_xxxx_xxxxxxxxxxxx, where x is a lowercase hex number.
  std::string GenerateNewRecordId();

  // Set the |allow_access_| which determines whether the backing storage
  // location can be accessed or not. Depending on the mounting mechanism and
  // namespace restrictions, the mounts might not be visible until after
  // certain points of the user flow (like successful login) are complete.
  void set_allow_access(bool allow_access) { allow_access_ = allow_access; }

 private:
  base::FilePath root_path_;
  std::string biometrics_manager_name_;
  ReadRecordsCallback load_record_;
  bool allow_access_;
};
}  // namespace biod

#endif  // BIOD_BIOD_STORAGE_H_
