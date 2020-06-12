// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <sys/mount.h>

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/scoped_mount_namespace.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_info.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/system_mounter.h"

namespace cros_disks {
namespace {

// Mapping from a base path to its corresponding path inside the AVFS mount.
struct AVFSPathMapping {
  const char* const base_path;
  const char* const avfs_path;
};

const char kAVFSMountGroup[] = "chronos-access";
const char kAVFSMountUser[] = "avfs";
// TODO(wad,benchan): Revisit the location of policy files once more system
// daemons are sandboxed with seccomp filters.
const char kAVFSSeccompFilterPolicyFile[] =
    "/usr/share/policy/avfsd-seccomp.policy";
const char kAVFSMountProgram[] = "/usr/bin/avfsd";
const char kAVFSRootDirectory[] = "/run/avfsroot";
const mode_t kAVFSDirectoryPermissions = 0770;  // rwx by avfs user and group
const char kAVFSLogFile[] = "/run/avfsroot/avfs.log";
const char kAVFSMediaDirectory[] = "/run/avfsroot/media";
const char kAVFSUsersDirectory[] = "/run/avfsroot/users";
const char kMediaDirectory[] = "/media";
const char kUserRootDirectory[] = "/home/chronos";
const AVFSPathMapping kAVFSPathMapping[] = {
    {kMediaDirectory, kAVFSMediaDirectory},
    {kUserRootDirectory, kAVFSUsersDirectory},
};
const char kAVFSModulesOption[] = "modules=subdir";
const char kAVFSSubdirOptionPrefix[] = "subdir=";

}  // namespace

const char* const ArchiveManager::kChromeMountNamespacePath =
    "/run/namespaces/mnt_chrome";

class ArchiveManager::ArchiveMountPoint : public MountPoint {
 public:
  ArchiveMountPoint(std::unique_ptr<MountPoint> mount_point,
                    ArchiveManager* archive_manager)
      : MountPoint(mount_point->path()),
        mount_point_(std::move(mount_point)),
        archive_manager_(archive_manager) {
    DCHECK(mount_point_);
    DCHECK(archive_manager_);
  }

  ~ArchiveMountPoint() override { DestructorUnmount(); }

  void Release() override {
    MountPoint::Release();
    mount_point_->Release();
  }

 protected:
  MountErrorType UnmountImpl() override {
    MountErrorType error = mount_point_->Unmount();
    if (error == MOUNT_ERROR_NONE) {
      archive_manager_->RemoveMountVirtualPath(path().value());
    }
    return error;
  }

 private:
  const std::unique_ptr<MountPoint> mount_point_;
  ArchiveManager* const archive_manager_;
};

ArchiveManager::ArchiveManager(const std::string& mount_root,
                               Platform* platform,
                               Metrics* metrics,
                               brillo::ProcessReaper* process_reaper)
    : MountManager(mount_root, platform, metrics, process_reaper),
      avfs_started_(false) {}

ArchiveManager::~ArchiveManager() {
  // StopAVFS() unmounts all mounted archives as well as AVFS mount points.
  StopAVFS();
}

bool ArchiveManager::Initialize() {
  RegisterDefaultFileExtensions();
  return MountManager::Initialize();
}

bool ArchiveManager::StopSession() {
  return StopAVFS();
}

bool ArchiveManager::ResolvePath(const std::string& path,
                                 std::string* real_path) {
  auto guard = brillo::ScopedMountNamespace::CreateFromPath(
      base::FilePath(kChromeMountNamespacePath));

  // If the path doesn't exist in Chrome's mount namespace, exit the namespace,
  // so that GetRealPath() below gets executed in cros-disks's mount namespace.
  if (!base::PathExists(base::FilePath(path)))
    guard.reset();

  return platform()->GetRealPath(path, real_path);
}

bool ArchiveManager::CanMount(const std::string& source_path) const {
  return IsInAllowedFolder(source_path);
}

bool ArchiveManager::IsInAllowedFolder(const std::string& source_path) {
  std::vector<std::string> parts;
  base::FilePath(source_path).GetComponents(&parts);

  if (parts.size() < 2 || parts[0] != "/")
    return false;

  if (parts[1] == "home")
    return parts.size() > 5 && parts[2] == "chronos" &&
           base::StartsWith(parts[3], "u-", base::CompareCase::SENSITIVE) &&
           brillo::cryptohome::home::IsSanitizedUserName(parts[3].substr(2)) &&
           parts[4] == "MyFiles";

  if (parts[1] == "media")
    return parts.size() > 4 && (parts[2] == "archive" || parts[2] == "fuse" ||
                                parts[2] == "removable");

  if (parts[1] == "run")
    return parts.size() > 8 && parts[2] == "arc" && parts[3] == "sdcard" &&
           parts[4] == "write" && parts[5] == "emulated" && parts[6] == "0";

  return false;
}

std::unique_ptr<MountPoint> ArchiveManager::DoMount(
    const std::string& source_path,
    const std::string& source_format,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* applied_options,
    MountErrorType* error) {
  CHECK(!source_path.empty()) << "Invalid source path argument";
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  std::string extension = GetFileExtension(source_format);
  if (extension.empty())
    extension = GetFileExtension(source_path);

  metrics()->RecordArchiveType(extension);

  std::string avfs_path = GetAVFSPath(source_path, extension);
  if (avfs_path.empty()) {
    LOG(ERROR) << "Path " << quote(source_path)
               << " is not a supported archive";
    *error = MOUNT_ERROR_UNSUPPORTED_ARCHIVE;
    return nullptr;
  }

  MountErrorType avfs_start_error = StartAVFS();
  if (avfs_start_error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to start AVFS mounts: " << avfs_start_error;
    *error = avfs_start_error;
    return nullptr;
  }

  // Perform a bind mount from the archive path under the AVFS mount
  // to /media/archive/<archive name>.
  std::vector<std::string> extended_options = options;
  extended_options.push_back(MountOptions::kOptionBind);
  MountOptions mount_options;
  mount_options.WhitelistOption(MountOptions::kOptionNoSymFollow);
  mount_options.Initialize(extended_options, false, "", "");
  MounterCompat mounter(std::make_unique<SystemMounter>("", platform()),
                        mount_options);

  // SystemMounter uses a lazy-fallback-on-busy approach to unmounting, so no
  // need to replicate that here.
  std::unique_ptr<MountPoint> mount_point =
      mounter.Mount(avfs_path, mount_path, mount_options.options(), error);
  if (mount_point) {
    AddMountVirtualPath(mount_path.value(), avfs_path);
    mount_point =
        std::make_unique<ArchiveMountPoint>(std::move(mount_point), this);
  }
  return mount_point;
}

std::string ArchiveManager::SuggestMountPath(
    const std::string& source_path) const {
  // Use the archive name to name the mount directory.
  base::FilePath base_name = base::FilePath(source_path).BaseName();
  return mount_root().Append(base_name).value();
}

void ArchiveManager::RegisterDefaultFileExtensions() {
  // Different archive formats can now be supported via an extension (built-in
  // or installed by user) using the chrome.fileSystemProvider API. Thus, zip,
  // tar, and gzip/bzip2 compressed tar formats are no longer supported here.

  // rar is still supported until there is a replacement using a built-in
  // extension.
  RegisterFileExtension("rar", "#urar");
}

void ArchiveManager::RegisterFileExtension(const std::string& extension,
                                           const std::string& avfs_handler) {
  extension_handlers_[extension] = avfs_handler;
}

std::string ArchiveManager::GetFileExtension(const std::string& path) const {
  base::FilePath file_path(path);
  std::string extension = file_path.Extension();
  if (!extension.empty()) {
    // Strip the leading dot and convert the extension to lower case.
    extension.erase(0, 1);
    extension = base::ToLowerASCII(extension);
  }
  return extension;
}

std::string ArchiveManager::GetAVFSPath(const std::string& path,
                                        const std::string& extension) const {
  // When mounting an archive within another mounted archive, we need to
  // resolve the virtual path of the inner archive to the "unfolded"
  // form within the AVFS mount, such as
  //   "/run/avfsroot/media/layer2.zip#/test/doc/layer1.zip#"
  // instead of the "nested" form, such as
  //   "/run/avfsroot/media/archive/layer2.zip/test/doc/layer1.zip#"
  // where "/media/archive/layer2.zip" is a mount point to the virtual
  // path "/run/avfsroot/media/layer2.zip#".
  //
  // Mounting the inner archive using the nested form may cause problems
  // reading files from the inner archive. To avoid that, we first try to
  // find the longest parent path of |path| that is an existing mount
  // point to a virtual path within the AVFS mount. If such a parent path
  // is found, we construct the virtual path of |path| within the AVFS
  // mount as a subpath of its parent's virtual path.
  //
  // e.g. Given |path| is "/media/archive/layer2.zip/test/doc/layer1.zip",
  //      and "/media/archive/layer2.zip" is a mount point to the virtual
  //      path "/run/avfsroot/media/layer2.zip#" within the AVFS mount.
  //      The following code should return the virtual path of |path| as
  //      "/run/avfsroot/media/layer2.zip#/test/doc/layer1.zip#".
  std::map<std::string, std::string>::const_iterator handler_iterator =
      extension_handlers_.find(extension);
  if (handler_iterator == extension_handlers_.end())
    return std::string();

  base::FilePath file_path(path);
  base::FilePath current_path = file_path.DirName();
  base::FilePath parent_path = current_path.DirName();
  while (current_path != parent_path) {  // Search till the root
    VirtualPathMap::const_iterator path_iterator =
        virtual_paths_.find(current_path.value());
    if (path_iterator != virtual_paths_.end()) {
      base::FilePath avfs_path(path_iterator->second);
      // As current_path is a parent of file_path, AppendRelativePath()
      // should return true here.
      CHECK(current_path.AppendRelativePath(file_path, &avfs_path));
      return avfs_path.value() + handler_iterator->second;
    }
    current_path = parent_path;
    parent_path = parent_path.DirName();
  }

  // If no parent path is a mounted via AVFS, we are not mounting a nested
  // archive and thus construct the virtual path of the archive based on a
  // corresponding AVFS mount path.
  for (const auto& mapping : kAVFSPathMapping) {
    base::FilePath base_path(mapping.base_path);
    base::FilePath avfs_path(mapping.avfs_path);
    if (base_path.AppendRelativePath(file_path, &avfs_path)) {
      return avfs_path.value() + handler_iterator->second;
    }
  }
  return std::string();
}

MountErrorType ArchiveManager::StartAVFS() {
  if (avfs_started_)
    return MOUNT_ERROR_NONE;

  // As cros-disks is now a non-privileged process, the directory tree under
  // |kAVFSRootDirectory| is created by the pre-start script of the cros-disks
  // upstart job. We simply check to make sure the directory tree is created
  // with the expected file ownership and permissions.
  uid_t avfs_user_id, dir_user_id;
  gid_t avfs_group_id, dir_group_id;
  mode_t dir_mode;
  if (!platform()->PathExists(kAVFSRootDirectory) ||
      !platform()->GetUserAndGroupId(kAVFSMountUser, &avfs_user_id,
                                     &avfs_group_id) ||
      !platform()->GetOwnership(kAVFSRootDirectory, &dir_user_id,
                                &dir_group_id) ||
      !platform()->GetPermissions(kAVFSRootDirectory, &dir_mode) ||
      (dir_user_id != avfs_user_id) || (dir_group_id != avfs_group_id) ||
      ((dir_mode & 07777) != kAVFSDirectoryPermissions)) {
    LOG(ERROR) << kAVFSRootDirectory << " isn't created properly";
    return MOUNT_ERROR_INTERNAL;
  }

  // Set the AVFS_LOGFILE environment variable so that the AVFS daemon
  // writes log messages to a file instead of syslog. Otherwise, writing
  // to syslog may trigger the socket/connect/send system calls, which are
  // disabled by the seccomp filters policy file. This only affects the
  // child processes spawned by cros-disks and does not persist after
  // cros-disks restarts.
  setenv("AVFS_LOGFILE", kAVFSLogFile, 1);

  avfs_started_ = true;
  for (const auto& mapping : kAVFSPathMapping) {
    MountErrorType mount_error =
        MountAVFSPath(mapping.base_path, mapping.avfs_path);
    if (mount_error != MOUNT_ERROR_NONE) {
      LOG(ERROR) << "Cannot mount AVFS path " << quote(mapping.avfs_path)
                 << ": " << mount_error;
      StopAVFS();
      return mount_error;
    }
  }
  return MOUNT_ERROR_NONE;
}

bool ArchiveManager::StopAVFS() {
  if (!avfs_started_)
    return true;

  avfs_started_ = false;
  // Unmounts all mounted archives before unmounting AVFS mounts.
  bool all_unmounted = UnmountAll();

  for (auto it = avfsd_mounts_.begin(); it != avfsd_mounts_.end();) {
    std::unique_ptr<MountPoint> mount_point = std::move(it->second);
    it = avfsd_mounts_.erase(it);

    const MountErrorType error = mount_point->Unmount();
    if (error != MOUNT_ERROR_NONE) {
      all_unmounted = false;
    }
  }

  return all_unmounted;
}

bool ArchiveManager::CreateMountDirectory(const std::string& path) const {
  // If an empty directory was left behind for any reason, remove it first.
  if (platform()->DirectoryExists(path) &&
      !platform()->RemoveEmptyDirectory(path)) {
    return false;
  }

  // Create directory. This works because /run/avfsroot is owned by avfs:avfs,
  // and cros-disks is in the avfs group.
  if (!platform()->CreateDirectory(path)) {
    return false;
  }

  uid_t uid;
  gid_t gid;

  // Set directory's permissions and owner.
  if (!platform()->SetPermissions(path, kAVFSDirectoryPermissions) ||
      !platform()->GetUserAndGroupId(kAVFSMountUser, &uid, &gid) ||
      !platform()->SetOwnership(path, uid, gid)) {
    // Remove directory in case of error.
    platform()->RemoveEmptyDirectory(path);
    return false;
  }

  return true;
}

MountErrorType ArchiveManager::MountAVFSPath(const std::string& base_path,
                                             const std::string& avfs_path) {
  base::FilePath mount_path(avfs_path);
  if (base::Contains(avfsd_mounts_, mount_path)) {
    LOG(ERROR) << "AVFS mount point " << quote(mount_path) << " already exists";
    return MOUNT_ERROR_INTERNAL;
  }

  MountInfo mount_info;
  if (!mount_info.RetrieveFromCurrentProcess())
    return MOUNT_ERROR_INTERNAL;

  if (mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Path " << quote(avfs_path) << " is already mounted";
    // Not using MOUNT_ERROR_PATH_ALREADY_MOUNTED here because that implies an
    // error on the user-requested mount. The error here is for the avfsd
    // daemon.
    return MOUNT_ERROR_INTERNAL;
  }

  // Create avfs_path with the right uid, gid and permissions.
  if (!CreateMountDirectory(avfs_path)) {
    LOG(ERROR) << "Cannot create mount directory " << quote(avfs_path);
    return MOUNT_ERROR_INTERNAL;
  }

  MountOptions mount_options;
  mount_options.WhitelistOption(FUSEHelper::kOptionAllowOther);
  mount_options.WhitelistOption(kAVFSModulesOption);
  mount_options.WhitelistOptionPrefix(kAVFSSubdirOptionPrefix);
  std::vector<std::string> options = {
      MountOptions::kOptionReadOnly,
      kAVFSModulesOption,
      kAVFSSubdirOptionPrefix + base_path,
  };
  mount_options.Initialize(options, false, "", "");

  FUSEMounter mounter(
      "avfs", mount_options, platform(), process_reaper(), kAVFSMountProgram,
      kAVFSMountUser, kAVFSSeccompFilterPolicyFile,
      std::vector<FUSEMounter::BindPath>({
          // This needs to be recursively bind mounted so that any external
          // media (mounted under /media) or user (under /home/chronos) mounts
          // are visible to AVFS.
          {base_path, false /* writable*/, true /* recursive */},
      }),
      false /* permit_network_access */, kAVFSMountGroup);

  // To access Play Files.
  if (!mounter.AddGroup("android-everybody"))
    LOG(INFO) << "Group 'android-everybody' does not exist";

  MountErrorType mount_error = MOUNT_ERROR_UNKNOWN;
  std::unique_ptr<MountPoint> mount_point =
      mounter.Mount("", mount_path, mount_options.options(), &mount_error);
  if (mount_error != MOUNT_ERROR_NONE) {
    DCHECK(!mount_point);
    return mount_error;
  }

  DCHECK(mount_point);

  if (!mount_info.RetrieveFromCurrentProcess() ||
      !mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Cannot mount " << quote(base_path) << " to "
                 << quote(avfs_path) << " via AVFS";
    return MOUNT_ERROR_INTERNAL;
  }

  LOG(INFO) << "Mounted " << quote(base_path) << " to " << quote(avfs_path)
            << " via AVFS";
  avfsd_mounts_[mount_path] = std::move(mount_point);
  return MOUNT_ERROR_NONE;
}

void ArchiveManager::AddMountVirtualPath(const std::string& mount_path,
                                         const std::string& virtual_path) {
  virtual_paths_[mount_path] = virtual_path;
}

void ArchiveManager::RemoveMountVirtualPath(const std::string& mount_path) {
  virtual_paths_.erase(mount_path);
}

}  // namespace cros_disks
