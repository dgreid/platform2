// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_UTILS_H_
#define DLCSERVICE_UTILS_H_

#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <libimageloader/manifest.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/types.h"

namespace dlcservice {

extern char kDlcDirAName[];
extern char kDlcDirBName[];
extern char kDlcPreloadAllowedName[];

// Important DLC file names.
extern char kDlcImageFileName[];
extern char kManifestName[];

// The directory inside a DLC module that contains all the DLC files.
extern char kRootDirectoryInsideDlcModule[];

// Permissions for DLC files and directories.
extern const int kDlcFilePerms;
extern const int kDlcDirectoryPerms;

template <typename BindedCallback>
class ScopedCleanups {
 public:
  ScopedCleanups() = default;
  ~ScopedCleanups() {
    for (const auto& cleanup : queue_)
      cleanup.Run();
  }
  void Insert(BindedCallback cleanup) { queue_.push_back(cleanup); }
  // Clears everything so destructor is a no-op.
  void Cancel() { queue_.clear(); }

 private:
  std::vector<BindedCallback> queue_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCleanups<BindedCallback>);
};

template <typename Arg>
base::FilePath JoinPaths(Arg&& path) {
  return base::FilePath(path);
}

template <typename Arg, typename... Args>
base::FilePath JoinPaths(Arg&& path, Args&&... paths) {
  return base::FilePath(path).Append(JoinPaths(paths...));
}

// Wrapper to |base::WriteFileDescriptor()|, closes file descriptor after
// writing. Returns true if all |size| of |data| are written.
bool WriteToFile(const base::FilePath& path, const std::string& data);

// Creates a directory with permissions required for DLC modules.
bool CreateDir(const base::FilePath& path);

// Creates a directory with an empty file and resizes it.
bool CreateFile(const base::FilePath& path, int64_t size);

// Resizes an existing file, failure if file does not exist or failure to
// resize.
bool ResizeFile(const base::FilePath& path, int64_t size);

// Copies and then resizes the copied file.
bool CopyAndResizeFile(const base::FilePath& from,
                       const base::FilePath& to,
                       int64_t size);

// Returns the path to a DLC module image given the |id| and |package|.
base::FilePath GetDlcImagePath(const base::FilePath& dlc_module_root_path,
                               const std::string& id,
                               const std::string& package,
                               BootSlot::Slot current_slot);

bool GetDlcManifest(const base::FilePath& dlc_manifest_path,
                    const std::string& id,
                    const std::string& package,
                    imageloader::Manifest* manifest_out);

// Returns the directory inside a DLC module which is mounted at
// |dlc_mount_point|.
base::FilePath GetDlcRootInModulePath(const base::FilePath& dlc_mount_point);

// Scans a directory and returns all its subdirectory names in a list.
std::set<std::string> ScanDirectory(const base::FilePath& dir);

// Converts a |DlcRootMap| into a |DlcModuleList| based on filtering logic where
// a return value of true indicates insertion into |DlcModuleList|.
dlcservice::DlcModuleList ToDlcModuleList(
    const DlcRootMap& dlcs, std::function<bool(DlcId, DlcRoot)> filter);

// Converts a |DlcModuleList| into a |DlcRootMap| based on filtering logic where
// a return value of true indicates insertion into |DlcRootMap|.
DlcRootMap ToDlcRootMap(const dlcservice::DlcModuleList& dlc_module_list,
                        std::function<bool(dlcservice::DlcModuleInfo)> filter);

dlcservice::InstallStatus CreateInstallStatus(
    const dlcservice::Status& status,
    const std::string& error_code,
    const dlcservice::DlcModuleList& dlc_module_list,
    double progress);

}  // namespace dlcservice

#endif  // DLCSERVICE_UTILS_H_
