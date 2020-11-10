// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_OOBE_CONFIG_H_
#define OOBE_CONFIG_OOBE_CONFIG_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>

namespace tpmcrypto {
class TpmCrypto;
}

namespace oobe_config {

class RollbackData;

// Helper class for saving and restoring rollback data. Testing is possible with
// |set_prefix_path_for_testing|.
class OobeConfig {
 public:
  OobeConfig();
  explicit OobeConfig(std::unique_ptr<tpmcrypto::TpmCrypto> crypto);
  OobeConfig(const OobeConfig&) = delete;
  OobeConfig& operator=(const OobeConfig&) = delete;

  ~OobeConfig();

  // Saves the rollback data into an unencrypted file. Only use for testing.
  bool UnencryptedRollbackSave() const;

  // Saves the rollback data into an encrypted file.
  bool EncryptedRollbackSave() const;

  // Restores the rollback data from an unencrypted file. Only use for testing.
  bool UnencryptedRollbackRestore() const;

  // Restores the rollback data from an encrypted file.
  bool EncryptedRollbackRestore() const;

  // Removes all files from kEncryptedStatefulRollbackDataPath.
  void CleanupEncryptedStatefulDirectory() const;

  // Sets a prefix path which is used as file system root when testing. Setting
  // to an empty path removes the prefix.
  void set_prefix_path_for_testing(const base::FilePath& prefix_path) {
    prefix_path_for_testing_ = prefix_path;
  }

  // Reads the content of the file at |file_path| (inside the testing prefix if
  // set) and returns it in |out_content|.
  // Returns true if reading the file succeeded.
  // Returns false if the file doesn't exist or reading it fails, |out_content|
  // is set to empty string in this case.
  bool ReadFile(const base::FilePath& file_path,
                std::string* out_content) const;

  // Returns whether the file at |file_path| (inside the testing prefix if set)
  // exists.
  bool FileExists(const base::FilePath& file_path) const;

  // Writes |data| into a file at |file_path| (inside the testing prefix if
  // set).
  // Returns true if writing the file succeeded, false otherwise.
  bool WriteFile(const base::FilePath& file_path,
                 const std::string& data) const;

  bool CheckFirstStage() const;
  bool CheckSecondStage() const;
  bool CheckThirdStage() const;

  bool ShouldSaveRollbackData() const;
  bool DeleteRollbackSaveFlagFile() const;

 private:
  // Returns the file path of |file_path| which includes the prefix if set.
  // |file_path| must be an absolute path starting with "/".
  base::FilePath GetPrefixedFilePath(const base::FilePath& file_path) const;

  // Reads the content of the file at |file_path| and returns it in
  // |out_content|. Doesn't respect the testing prefix. Returns true if reading
  // the file succeeded. Returns false if the file doesn't exist or reading it
  // fails, |out_content| is set to empty string in this case.
  bool ReadFileWithoutPrefix(const base::FilePath& file_path,
                             std::string* out_content) const;
  // Writes |data| into a file at |file_path|. Doesn't respect the testing
  // prefix. Returns true if the write succeeded, false otherwise.
  bool WriteFileWithoutPrefix(const base::FilePath& file_path,
                              const std::string& data) const;

  // Gets the files needed for rollback and stores them in a |RollbackData|
  // proto, then returns the serialized proto |serialized_rollback_data|.
  bool GetSerializedRollbackData(std::string* serialized_rollback_data) const;

  // Gets the files needed for rollback and returns them in |rollback_data|.
  // Returns true if succeeded, false otherwise.
  bool GetRollbackData(RollbackData* rollback_data) const;

  // Restores the files stored in |rollback_data|. Returns true if succeeded,
  // false otherwise.
  bool RestoreRollbackData(const RollbackData& rollback_data) const;

  // We're prefixing all paths for testing with a temp directory. Empty (no
  // prefix) by default.
  base::FilePath prefix_path_for_testing_;

  // Class for implementing crypto.
  std::unique_ptr<tpmcrypto::TpmCrypto> crypto_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_OOBE_CONFIG_H_
