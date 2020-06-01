// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_KEY_FILE_STORE_H_
#define SHILL_KEY_FILE_STORE_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/crypto.h"
#include "shill/store_interface.h"

namespace shill {

// A key file store implementation of the store interface. See
// https://specifications.freedesktop.org/desktop-entry-spec/latest/ar01s03.html
// for details of the key file format, and
// https://developer.gnome.org/glib/stable/glib-Key-value-file-parser.html
// for details of the GLib API that is being reimplemented here.
// This implementation does not support locales because we do not use locale
// strings and never have.
class KeyFileStore : public StoreInterface {
 public:
  explicit KeyFileStore(const base::FilePath& path);
  ~KeyFileStore() override;

  // Inherited from StoreInterface.
  bool IsEmpty() const override;
  bool Open() override;
  bool Close() override;
  bool Flush() override;
  bool MarkAsCorrupted() override;
  std::set<std::string> GetGroups() const override;
  std::set<std::string> GetGroupsWithKey(const std::string& key) const override;
  std::set<std::string> GetGroupsWithProperties(
      const KeyValueStore& properties) const override;
  bool ContainsGroup(const std::string& group) const override;
  bool DeleteKey(const std::string& group, const std::string& key) override;
  bool DeleteGroup(const std::string& group) override;
  bool SetHeader(const std::string& header) override;
  bool GetString(const std::string& group,
                 const std::string& key,
                 std::string* value) const override;
  bool SetString(const std::string& group,
                 const std::string& key,
                 const std::string& value) override;
  bool GetBool(const std::string& group,
               const std::string& key,
               bool* value) const override;
  bool SetBool(const std::string& group,
               const std::string& key,
               bool value) override;
  bool GetInt(const std::string& group,
              const std::string& key,
              int* value) const override;
  bool SetInt(const std::string& group,
              const std::string& key,
              int value) override;
  bool GetUint64(const std::string& group,
                 const std::string& key,
                 uint64_t* value) const override;
  bool SetUint64(const std::string& group,
                 const std::string& key,
                 uint64_t value) override;
  bool GetStringList(const std::string& group,
                     const std::string& key,
                     std::vector<std::string>* value) const override;
  bool SetStringList(const std::string& group,
                     const std::string& key,
                     const std::vector<std::string>& value) override;
  // For migration from ROT47 to plaintext. New usecases should use GetString.
  // TODO(crbug.com/1084279) Remove after migration is complete.
  bool GetCryptedString(const std::string& group,
                        const std::string& deprecated_key,
                        const std::string& plaintext_key,
                        std::string* value) const override;

 private:
  FRIEND_TEST(KeyFileStoreTest, OpenClose);
  FRIEND_TEST(KeyFileStoreTest, OpenFail);

  class KeyFile;

  static const char kCorruptSuffix[];

  bool DoesGroupMatchProperties(const std::string& group,
                                const KeyValueStore& properties) const;

  std::unique_ptr<KeyFile> key_file_;
  const base::FilePath path_;

  DISALLOW_COPY_AND_ASSIGN(KeyFileStore);
};

}  // namespace shill

#endif  // SHILL_KEY_FILE_STORE_H_
