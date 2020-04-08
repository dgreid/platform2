// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/utils.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/file_utils.h>

#include "dlcservice/dlc.h"
#include "dlcservice/dlc_service.h"

using base::FilePath;
using std::pair;
using std::set;
using std::string;

namespace dlcservice {

namespace {

bool SetFilePermissions(const base::FilePath& path, int perms) {
  if (!base::SetPosixFilePermissions(path, perms)) {
    PLOG(ERROR) << "Failed to set permissions for: " << path.value();
    return false;
  }
  return true;
}

}  // namespace

char kDlcDirAName[] = "dlc_a";
char kDlcDirBName[] = "dlc_b";

char kDlcImageFileName[] = "dlc.img";
char kManifestName[] = "imageloader.json";

char kRootDirectoryInsideDlcModule[] = "root";

const int kDlcFilePerms = 0644;
const int kDlcDirectoryPerms = 0755;

const int kImageLoaderTimeoutMs = 5000;

bool WriteToFile(const FilePath& path, const string& data) {
  base::ScopedFD fd(
      brillo::OpenSafely(path, O_CREAT | O_WRONLY, kDlcFilePerms));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open file for writting " << path.value();
    return false;
  }
  if (data.empty())
    return true;
  return base::WriteFileDescriptor(fd.get(), data.c_str(), data.size());
}

// Resizes the file in |path| to a new |size|. When shrinking, meaning current
// file size is > |size|, the file will only be resized and not unsparsed as the
// resized file is already assumed to be unsparse. When increasing, meaning
// current file size is <  |size|, the file will be resized and unsparsed only
// to the portions that increased from current file size to |size|. When neither
// shrinking nor increasing, nothing happens.
bool ResizeFile(const base::FilePath& path, int64_t size) {
  int64_t prev_size;
  base::File f(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  if (!f.IsValid()) {
    LOG(ERROR) << "Failed to open file to resize '" << path.value()
               << "': " << base::File::ErrorToString(f.error_details());
    return false;
  }
  prev_size = f.GetLength();
  if (prev_size < 0) {
    PLOG(ERROR) << "Failed to get file size for resizing " << path.value();
    return false;
  }
  if (!f.SetLength(size)) {
    PLOG(ERROR) << "Failed to set length (" << size << ") for "
                << path.value();
    return false;
  }
  // When shrinking files, there is no need to unsparse as it's not certainly
  // safe to unsparse potentially used portions of the file.
  if (size <= prev_size)
    return true;

  // Otherwise, unsparse the increased portion of the file.
  if (f.Seek(base::File::Whence::FROM_BEGIN, prev_size) < 0) {
    PLOG(ERROR) << "Failed to lseek() to offset " << prev_size << " for "
                << path.value();
    return false;
  }
  size -= prev_size;

  constexpr int64_t kMaxBufSize = 4096;
  constexpr char buf[kMaxBufSize] = {'\0'};
  for (; size > 0; size -= kMaxBufSize) {
    // Set the lesser of either |kMaxBufSize| or |size| bytes.
    const size_t len = std::min(size, kMaxBufSize);
    // Write out |len| from |buf| to |fd|.
    if (f.WriteAtCurrentPos(buf, len) != len) {
      PLOG(ERROR) << "Failed to write zero to " << path.value();
      return false;
    }
  }
  return true;
}

bool CreateDir(const base::FilePath& path) {
  base::File::Error file_err;
  if (!base::CreateDirectoryAndGetError(path, &file_err)) {
    PLOG(ERROR) << "Failed to create directory '" << path.value()
                << "': " << base::File::ErrorToString(file_err);
    return false;
  }
  return SetFilePermissions(path, kDlcDirectoryPerms);
}

// TODO(crbug.com/976074): When creating a file, provide the flexibility to be
// able to unsparse in |ResizeFile()| up to the actual size necessary and not
// the preallocated size from the manifest as is the |size| here for DLC to
// install successfully.
bool CreateFile(const base::FilePath& path, int64_t size) {
  if (!CreateDir(path.DirName()))
    return false;
  // Keep scoped to not explicitly close file.
  {
    base::File f(path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    if (!f.IsValid()) {
      LOG(ERROR) << "Failed to create file at " << path.value()
                 << " reason: " << base::File::ErrorToString(f.error_details());
      return false;
    }
  }
  return ResizeFile(path, size) && SetFilePermissions(path, kDlcFilePerms);
}

bool CopyAndResizeFile(const base::FilePath& from,
                       const base::FilePath& to,
                       int64_t size) {
  if (!base::CopyFile(from, to)) {
    PLOG(ERROR) << "Failed to copy from (" << from.value() << ") to ("
                << to.value() << ").";
    return false;
  }
  return ResizeFile(to, size) && SetFilePermissions(to, kDlcFilePerms);
}

FilePath GetDlcImagePath(const FilePath& dlc_module_root_path,
                         const string& id,
                         const string& package,
                         BootSlot::Slot slot) {
  return JoinPaths(dlc_module_root_path, id, package, BootSlot::ToString(slot),
                   kDlcImageFileName);
}

// Extract details about a DLC module from its manifest file.
bool GetDlcManifest(const FilePath& dlc_manifest_path,
                    const string& id,
                    const string& package,
                    imageloader::Manifest* manifest_out) {
  string dlc_json_str;
  FilePath dlc_manifest_file =
      JoinPaths(dlc_manifest_path, id, package, kManifestName);

  if (!base::ReadFileToString(dlc_manifest_file, &dlc_json_str)) {
    LOG(ERROR) << "Failed to read DLC manifest file '"
               << dlc_manifest_file.value() << "'.";
    return false;
  }

  if (!manifest_out->ParseManifest(dlc_json_str)) {
    LOG(ERROR) << "Failed to parse DLC manifest for DLC:" << id << ".";
    return false;
  }

  return true;
}

set<string> ScanDirectory(const FilePath& dir) {
  set<string> result;
  base::FileEnumerator file_enumerator(dir, false,
                                       base::FileEnumerator::DIRECTORIES);
  for (FilePath dir_path = file_enumerator.Next(); !dir_path.empty();
       dir_path = file_enumerator.Next()) {
    result.emplace(dir_path.BaseName().value());
  }
  return result;
}

DlcSet ToDlcSet(const DlcMap& dlcs,
                const std::function<bool(const DlcBase&)>& filter) {
  DlcSet s;
  for (const auto& pair : dlcs) {
    if (filter(pair.second))
      s.insert(pair.first);
  }
  return s;
}

}  // namespace dlcservice
