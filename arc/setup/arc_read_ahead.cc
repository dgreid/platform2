// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/arc_read_ahead.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <unordered_map>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

// TODO(yusukes): Read different set of files for Q.
#include "arc/setup/arc_read_ahead_files.h"

namespace arc {

namespace {

// A map from a file name (base name without a path) to its occurence in the
// file tree. This map is just for checking if |kImportantFiles| is up to date.
using FileNameToCountMap = std::unordered_map<std::string, size_t>;

// A multimap from a read-ahead size to a file name (full path). Use the size as
// a key to sort files by size.
using FilesToReadMap = std::multimap<int64_t, base::FilePath>;

// Checks if |base_name| should be read-ahead, and returns >0 when it is. The
// number returned should be passed as the 3rd argument of readahead(2). Returns
// 0 when |base_name| should not be read-ahead. This function also updates
// |usage| when |base_name| is in |kImportantFiles|,
int64_t GetReadAheadSize(const base::FilePath& base_name,
                         int64_t size,
                         FileNameToCountMap* usage) {
  auto it = usage->find(base_name.value());
  if (it != usage->end()) {
    ++(it->second);
    return size;
  }

  const std::string extension = base_name.Extension();
  if (std::find(kImportantExtensions.begin(), kImportantExtensions.end(),
                extension) != kImportantExtensions.end()) {
    return std::min(arc::kDefaultReadAheadSize, size);
  }

  return 0;  // no read ahead.
}

FilesToReadMap GetFileList(const base::FilePath& scan_root,
                           AndroidSdkVersion sdk_version) {
  FilesToReadMap result;

  FileNameToCountMap usage;
  switch (sdk_version) {
    case AndroidSdkVersion::UNKNOWN:
    case AndroidSdkVersion::ANDROID_M:
    case AndroidSdkVersion::ANDROID_N_MR1:
      NOTREACHED();
      break;
    case AndroidSdkVersion::ANDROID_P:
    case AndroidSdkVersion::ANDROID_Q:
    case AndroidSdkVersion::ANDROID_MASTER:
      for (auto* file_name : kImportantFilesP)
        usage.emplace(file_name, 0);
      break;
  }

  // Scan all files in |scan_root|.
  size_t num_files = 0;
  base::FileEnumerator enumerator(scan_root, true /* recursive */,
                                  base::FileEnumerator::FILES);
  for (base::FilePath name = enumerator.Next(); !name.empty();
       name = enumerator.Next()) {
    ++num_files;

    const base::FileEnumerator::FileInfo& info = enumerator.GetInfo();
    if ((info.stat().st_mode & S_IFMT) != S_IFREG)
      continue;  // do not handle device files, symlinks, etc.

    const int64_t read_ahead_bytes =
        GetReadAheadSize(name.BaseName(), info.GetSize(), &usage);
    if (read_ahead_bytes > 0)
      result.emplace(read_ahead_bytes, name);
  }

  // Check if |scan_root| has all files in |kImportantFiles|. Print LOGs if it
  // doesn't.
  for (auto it = usage.begin(); it != usage.end(); ++it) {
    if (it->second > 0)
      continue;
    LOG(WARNING) << it->first
                 << " is in |kImportantFiles|, but is not found in "
                 << scan_root.value()
                 << ". Update the table for better performance.";
  }

  LOG(INFO) << num_files << " files checked, found " << result.size()
            << " files to read";
  return result;
}

}  // namespace

std::pair<size_t, size_t> EmulateArcUreadahead(const base::FilePath& scan_root,
                                               const base::TimeDelta& timeout,
                                               AndroidSdkVersion sdk_version) {
  base::ElapsedTimer timer;
  size_t num_files_read = 0;
  size_t num_bytes_read = 0;

  FilesToReadMap files_to_read(GetFileList(scan_root, sdk_version));
  // Use rbegin/rend to read larger files first.
  for (auto it = files_to_read.rbegin(); it != files_to_read.rend(); ++it) {
    const int64_t read_ahead_bytes = it->first;
    const base::FilePath& name = it->second;

    if (timeout <= timer.Elapsed()) {
      LOG(WARNING) << "Timed out after reading " << num_files_read << " files";
      break;
    }

    base::ScopedFD scoped_fd(open(name.value().c_str(), O_RDONLY));
    if (!scoped_fd.is_valid()) {
      PLOG(WARNING) << "open failed for " << name.value();
      continue;
    }

    if (readahead(scoped_fd.get(), 0 /* offset */, read_ahead_bytes)) {
      PLOG(WARNING) << "readahead failed for " << name.value();
      continue;
    }

    ++num_files_read;
    num_bytes_read += read_ahead_bytes;
  }

  LOG(INFO) << "Read " << num_files_read << " files and " << num_bytes_read
            << " bytes in " << timer.Elapsed().InMillisecondsRoundedUp()
            << " ms";
  return std::make_pair(num_files_read, num_bytes_read);
}

}  // namespace arc
