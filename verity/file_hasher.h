// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Defines FileHasher, a class that creates a Verity-specific file of
// per-block hashes from a given base::File.
#ifndef VERITY_FILE_HASHER_H_
#define VERITY_FILE_HASHER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>

#include "verity/dm-bht.h"
#include "verity/dm-bht-userspace.h"
#include "verity/include/asm/page.h"

namespace verity {
// FileHasher takes a |base::File| object and reads in |block_size|
// bytes creating SHA-256 hashes as it goes.
// TODO(wad) allow any hashing format supported by openssl (and the kernel).
// This class may not be used by multiple threads at once.
class FileHasher {
 public:
  FileHasher(std::unique_ptr<base::File> source,
             std::unique_ptr<base::File> destination,
             unsigned int blocks,
             const char* alg)
      : source_(std::move(source)),
        destination_(std::move(destination)),
        block_limit_(blocks),
        alg_(alg),
        initialized_(false) {}
  virtual ~FileHasher();

  // TODO(wad) add initialized_ variable to check.
  virtual bool Initialize();
  virtual bool Hash();
  virtual bool Store();
  // Print a table to stdout which contains a dmsetup compatible format
  virtual void PrintTable(bool colocated);
  virtual std::string GetTable(bool colocated);

  virtual const char* RandomSalt();
  virtual void set_salt(const char* salt) {
    if (!strcmp(salt, "random"))
      salt = RandomSalt();
    dm_bht_set_salt(&tree_, salt);
    salt_ = salt;
  }
  virtual const char* salt(void) { return salt_; }

 private:
  std::unique_ptr<base::File> source_;
  std::unique_ptr<base::File> destination_;
  unsigned int block_limit_;
  const char* alg_;
  const char* salt_;
  char random_salt_[DM_BHT_SALT_SIZE * 2 + 1];
  std::vector<char> hash_data_;
  struct dm_bht tree_;
  sector_t sectors_;
  bool initialized_;

  FileHasher(const FileHasher&) = delete;
  FileHasher& operator=(const FileHasher&) = delete;
};

}  // namespace verity

#endif  // VERITY_FILE_HASHER_H__
