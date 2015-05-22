// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STORAGE_IMPLS_H_
#define BUFFET_STORAGE_IMPLS_H_

#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/values.h>

#include "buffet/storage_interface.h"

namespace buffet {

// Persists the given Value to an atomically written file.
class FileStorage : public StorageInterface {
 public:
  explicit FileStorage(const base::FilePath& file_path);
  ~FileStorage() override = default;
  std::unique_ptr<base::DictionaryValue> Load() override;
  bool Save(const base::DictionaryValue& config) override;

 private:
  base::FilePath file_path_;
  DISALLOW_COPY_AND_ASSIGN(FileStorage);
};

// StorageInterface for testing. Just stores the values in memory.
class MemStorage : public StorageInterface {
 public:
  MemStorage() = default;
  ~MemStorage() override = default;
  std::unique_ptr<base::DictionaryValue> Load() override;
  bool Save(const base::DictionaryValue& config) override;

 private:
  base::DictionaryValue cache_;
  DISALLOW_COPY_AND_ASSIGN(MemStorage);
};

}  // namespace buffet

#endif  // BUFFET_STORAGE_IMPLS_H_
