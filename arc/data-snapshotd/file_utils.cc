// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/data-snapshotd/file_utils.h"

#include <algorithm>
#include <string>
#include <utility>

#if USE_SELINUX
#include <selinux/selinux.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <openssl/sha.h>

namespace arc {
namespace data_snapshotd {

bool ReadSnapshotDirectory(const base::FilePath& dir,
                           SnapshotDirectory* snapshot_directory) {
  if (!snapshot_directory) {
    LOG(ERROR) << "snapshot_directory is nullptr";
    return false;
  }
  base::FileEnumerator dir_enumerator(
      dir, true /* recursive */,
      base::FileEnumerator::FileType::DIRECTORIES |
          base::FileEnumerator::FileType::FILES |
          base::FileEnumerator::FileType::SHOW_SYM_LINKS);
  std::vector<SnapshotFile> snapshot_files;
  for (auto file = dir_enumerator.Next(); !file.empty();
       file = dir_enumerator.Next()) {
    std::string relative_path =
        file.value().substr(dir.value().size(), std::string::npos);
    SnapshotFile snapshot_file;
    snapshot_file.set_name(std::move(relative_path));
    std::string contents;
    if (!dir_enumerator.GetInfo().IsDirectory() &&
        !base::ReadFileToString(file, &contents)) {
      LOG(ERROR) << "Failed to read file " << file.value();
      return false;
    }
    {
      std::vector<uint8_t> digest;
      digest.resize(SHA256_DIGEST_LENGTH);
      if (!SHA256((const unsigned char*)contents.data(), contents.size(),
                  digest.data())) {
        LOG(ERROR) << "Failed to calculate digest of file contents.";
        return false;
      }
      snapshot_file.set_content_hash(digest.data(), digest.size());
    }

#if USE_SELINUX
    char* con = nullptr;
    if (lgetfilecon(file.value().c_str(), &con) < 0) {
      PLOG(ERROR) << "Failed to getfilecon of file " << file.value();
      return false;
    }
    snapshot_file.set_selinux_context(con, strlen(con));
    if (con != nullptr) {
      free(con);
    }
#endif

    struct stat stat_buf;
    if (lstat(file.value().c_str(), &stat_buf)) {
      PLOG(ERROR) << "Failed to get stat of file " << file.value();
      return false;
    }
    Stat* stat_value = snapshot_file.mutable_stat();
    stat_value->set_ino(stat_buf.st_ino);
    stat_value->set_mode(stat_buf.st_mode);
    stat_value->set_uid(stat_buf.st_uid);
    stat_value->set_gid(stat_buf.st_gid);
    stat_value->set_size(stat_buf.st_size);
    stat_value->set_modification_time(stat_buf.st_mtime);
    snapshot_files.emplace_back(snapshot_file);
  }
  std::sort(snapshot_files.begin(), snapshot_files.end(),
            [](const SnapshotFile& a, const SnapshotFile& b) {
              return a.name() < b.name();
            });
  for (auto file : snapshot_files) {
    *snapshot_directory->mutable_files()->Add() = file;
  }
  return true;
}

std::vector<uint8_t> CalculateDirectoryCryptographicHash(
    const SnapshotDirectory& dir) {
  std::vector<uint8_t> hash;
  std::string serialized;
  if (!dir.SerializeToString(&serialized)) {
    LOG(ERROR) << "Failed to serialize to string snapshot directory info.";
    return hash;
  }
  hash.resize(SHA256_DIGEST_LENGTH);
  if (!SHA256((const unsigned char*)serialized.data(), serialized.size(),
              hash.data())) {
    LOG(ERROR) << "Failed to calculate digest of serialized SnapshotDirectory.";
    return std::vector<uint8_t>();
  }
  return hash;
}

}  // namespace data_snapshotd
}  // namespace arc
