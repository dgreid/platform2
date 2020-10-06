// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_manager.h"

#include <errno.h>
#include <inttypes.h>
#include <libudev.h>
#include <string.h>
#include <sys/mount.h>
#include <time.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

#include "cros-disks/device_ejector.h"
#include "cros-disks/disk_monitor.h"
#include "cros-disks/exfat_mounter.h"
#include "cros-disks/filesystem.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/ntfs_mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/system_mounter.h"

namespace cros_disks {

class DiskManager::EjectingMountPoint : public MountPoint {
 public:
  EjectingMountPoint(std::unique_ptr<MountPoint> mount_point,
                     DiskManager* disk_manager,
                     const std::string& device_file)
      : MountPoint(mount_point->path()),
        mount_point_(std::move(mount_point)),
        disk_manager_(disk_manager),
        device_file_(device_file) {
    DCHECK(mount_point_);
    DCHECK(disk_manager_);
    DCHECK(!device_file_.empty());
  }

  ~EjectingMountPoint() override { DestructorUnmount(); }

  void Release() override {
    MountPoint::Release();
    mount_point_->Release();
  }

 protected:
  MountErrorType UnmountImpl() override {
    MountErrorType error = mount_point_->Unmount();
    if (error == MOUNT_ERROR_NONE) {
      bool success = disk_manager_->EjectDevice(device_file_);
      LOG_IF(ERROR, !success)
          << "Unable to eject device " << quote(device_file_)
          << " for mount path " << quote(path());
    }
    return error;
  }

 private:
  const std::unique_ptr<MountPoint> mount_point_;
  DiskManager* const disk_manager_;
  const std::string device_file_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(EjectingMountPoint);
};

DiskManager::DiskManager(const std::string& mount_root,
                         Platform* platform,
                         Metrics* metrics,
                         brillo::ProcessReaper* process_reaper,
                         DiskMonitor* disk_monitor,
                         DeviceEjector* device_ejector)
    : MountManager(mount_root, platform, metrics, process_reaper),
      disk_monitor_(disk_monitor),
      device_ejector_(device_ejector),
      eject_device_on_unmount_(true) {}

DiskManager::~DiskManager() {
  UnmountAll();
}

bool DiskManager::Initialize() {
  RegisterDefaultFilesystems();
  return MountManager::Initialize();
}

const Filesystem* DiskManager::GetFilesystem(
    const std::string& filesystem_type) const {
  std::map<std::string, Filesystem>::const_iterator filesystem_iterator =
      filesystems_.find(filesystem_type);
  if (filesystem_iterator == filesystems_.end())
    return nullptr;

  return &filesystem_iterator->second;
}

void DiskManager::RegisterDefaultFilesystems() {
  std::string uid = base::StringPrintf("uid=%d", platform()->mount_user_id());
  std::string gid = base::StringPrintf("gid=%d", platform()->mount_group_id());

  Filesystem vfat_fs("vfat");
  vfat_fs.extra_mount_options = {MountOptions::kOptionFlush, "shortname=mixed",
                                 MountOptions::kOptionUtf8, uid, gid};
  RegisterFilesystem(vfat_fs);

  Filesystem exfat_fs("exfat");
  exfat_fs.mounter_type = ExFATMounter::kMounterType;
  exfat_fs.accepts_user_and_group_id = true;
  exfat_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(exfat_fs);

  Filesystem ntfs_fs("ntfs");
  ntfs_fs.mounter_type = NTFSMounter::kMounterType;
  ntfs_fs.accepts_user_and_group_id = true;
  ntfs_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(ntfs_fs);

  Filesystem hfsplus_fs("hfsplus");
  hfsplus_fs.extra_mount_options = {uid, gid};
  RegisterFilesystem(hfsplus_fs);

  Filesystem iso9660_fs("iso9660");
  iso9660_fs.is_mounted_read_only = true;
  iso9660_fs.extra_mount_options = {MountOptions::kOptionUtf8, uid, gid};
  RegisterFilesystem(iso9660_fs);

  Filesystem udf_fs("udf");
  udf_fs.is_mounted_read_only = true;
  udf_fs.extra_mount_options = {MountOptions::kOptionUtf8, uid, gid};
  RegisterFilesystem(udf_fs);

  Filesystem ext2_fs("ext2");
  RegisterFilesystem(ext2_fs);

  Filesystem ext3_fs("ext3");
  RegisterFilesystem(ext3_fs);

  Filesystem ext4_fs("ext4");
  RegisterFilesystem(ext4_fs);
}

void DiskManager::RegisterFilesystem(const Filesystem& filesystem) {
  filesystems_.emplace(filesystem.type, filesystem);
}

std::unique_ptr<MounterCompat> DiskManager::CreateMounter(
    const Disk& disk,
    const Filesystem& filesystem,
    const std::string& target_path,
    const std::vector<std::string>& options) const {
  std::vector<std::string> extended_options = filesystem.extra_mount_options;
  if (filesystem.type == "vfat") {
    // FAT32 stores times as local time instead of UTC. By default, the vfat
    // kernel module will use the kernel's time zone, which is set using
    // settimeofday(), to interpret time stamps as local time. However, time
    // zones are complicated and generally a user-space concern in modern Linux.
    // The man page for {get,set}timeofday comments that the |timezone| fields
    // of these functions is obsolete. Chrome OS doesn't appear to set these
    // either. Instead, we pass the time offset explicitly as a mount option so
    // that the user can see file time stamps as local time. This mirrors what
    // the user will see in other operating systems.
    // TODO(amistry): Consider moving this code into a FATMounter.
    time_t now = base::Time::Now().ToTimeT();
    struct tm timestruct;
    // The time zone might have changed since cros-disks was started. Force a
    // re-read of the time zone to ensure the local time is what the user
    // expects.
    tzset();
    localtime_r(&now, &timestruct);
    // tm_gmtoff is a glibc extension.
    int64_t offset_minutes = static_cast<int64_t>(timestruct.tm_gmtoff) / 60;
    extended_options.push_back(
        base::StringPrintf("time_offset=%" PRId64, offset_minutes));
  }

  std::string default_user_id, default_group_id;
  if (filesystem.accepts_user_and_group_id) {
    default_user_id = base::StringPrintf("%d", platform()->mount_user_id());
    default_group_id = base::StringPrintf("%d", platform()->mount_group_id());
  }

  MountOptions mount_options;
  mount_options.AllowOption(MountOptions::kOptionNoSymFollow);
  // TODO(crbug.com/950442): Remove this hack when MountOptions are gone.
  std::vector<std::string> workaround_options = extended_options;
  MountOptions junk_options;
  junk_options.Initialize(options, false, "", "");
  bool read_only_requested =
      (junk_options.ToMountFlagsAndData().first & MS_RDONLY) == MS_RDONLY;
  if (!read_only_requested)
    workaround_options.push_back(MountOptions::kOptionReadWrite);
  mount_options.Initialize(workaround_options,
                           filesystem.accepts_user_and_group_id,
                           default_user_id, default_group_id);

  bool mount_read_only = read_only_requested;
  if (filesystem.is_mounted_read_only || disk.is_read_only ||
      disk.IsOpticalDisk()) {
    mount_read_only = true;
    mount_options.SetReadOnlyOption();
  }

  if (filesystem.mounter_type.empty())
    return std::make_unique<MounterCompat>(
        MountOptions(), std::make_unique<SystemMounter>(
                            platform(), filesystem.mount_type, mount_read_only,
                            std::move(extended_options)));

  if (filesystem.mounter_type == ExFATMounter::kMounterType)
    return std::make_unique<ExFATMounter>(filesystem.mount_type,
                                          std::move(mount_options), platform(),
                                          process_reaper());

  if (filesystem.mounter_type == NTFSMounter::kMounterType)
    return std::make_unique<NTFSMounter>(filesystem.mount_type,
                                         std::move(mount_options), platform(),
                                         process_reaper());

  LOG(FATAL) << "Invalid mounter type " << quote(filesystem.mounter_type);
  return nullptr;
}

bool DiskManager::CanMount(const std::string& source_path) const {
  // The following paths can be mounted:
  //     /sys/...
  //     /devices/...
  //     /dev/...
  return base::StartsWith(source_path, "/sys/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/devices/",
                          base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/dev/", base::CompareCase::SENSITIVE);
}

std::unique_ptr<MountPoint> DiskManager::DoMount(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* applied_options,
    MountErrorType* error) {
  CHECK(!source_path.empty()) << "Invalid source path argument";
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  Disk disk;
  if (!disk_monitor_->GetDiskByDevicePath(base::FilePath(source_path), &disk)) {
    LOG(ERROR) << quote(source_path) << " is not a valid device";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }

  if (disk.is_on_boot_device) {
    LOG(ERROR) << quote(source_path)
               << " is on boot device and not allowed to mount";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }

  if (disk.device_file.empty()) {
    LOG(ERROR) << quote(source_path) << " does not have a device file";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }

  std::string device_filesystem_type =
      filesystem_type.empty() ? disk.filesystem_type : filesystem_type;
  metrics()->RecordDeviceMediaType(disk.media_type);
  metrics()->RecordFilesystemType(device_filesystem_type);
  if (device_filesystem_type.empty()) {
    LOG(ERROR) << "Cannot determine the file system type of device "
               << quote(source_path);
    *error = MOUNT_ERROR_UNKNOWN_FILESYSTEM;
    return nullptr;
  }

  const Filesystem* filesystem = GetFilesystem(device_filesystem_type);
  if (filesystem == nullptr) {
    LOG(ERROR) << "File system type " << quote(device_filesystem_type)
               << " on device " << quote(source_path) << " is not supported";
    *error = MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
    return nullptr;
  }

  std::unique_ptr<MounterCompat> mounter(
      CreateMounter(disk, *filesystem, mount_path.value(), options));
  CHECK(mounter) << "Failed to create a mounter";

  std::unique_ptr<MountPoint> mount_point = mounter->Mount(
      disk.device_file, mount_path, mounter->mount_options().options(), error);
  if (*error != MOUNT_ERROR_NONE) {
    DCHECK(!mount_point);
    // Try to mount the filesystem read-only if mounting it read-write failed.
    if (!mounter->mount_options().IsReadOnlyOptionSet()) {
      LOG(INFO) << "Trying to mount " << quote(source_path) << " read-only";
      std::vector<std::string> ro_options = options;
      ro_options.push_back("ro");
      mounter =
          CreateMounter(disk, *filesystem, mount_path.value(), ro_options);
      CHECK(mounter) << "Failed to create a 'ro' mounter";
      mount_point = mounter->Mount(disk.device_file, mount_path,
                                   mounter->mount_options().options(), error);
    }
  }

  if (*error != MOUNT_ERROR_NONE) {
    DCHECK(!mount_point);
    return nullptr;
  }

  *applied_options = mounter->mount_options();
  return MaybeWrapMountPointForEject(std::move(mount_point), disk);
}

std::string DiskManager::SuggestMountPath(
    const std::string& source_path) const {
  Disk disk;
  disk_monitor_->GetDiskByDevicePath(base::FilePath(source_path), &disk);
  // If GetDiskByDevicePath fails, disk.GetPresentationName() returns
  // the fallback presentation name.
  return mount_root().Append(disk.GetPresentationName()).value();
}

bool DiskManager::ShouldReserveMountPathOnError(
    MountErrorType error_type) const {
  return error_type == MOUNT_ERROR_UNKNOWN_FILESYSTEM ||
         error_type == MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
}

bool DiskManager::EjectDevice(const std::string& device_file) {
  if (eject_device_on_unmount_) {
    return device_ejector_->Eject(device_file);
  }
  return true;
}

std::unique_ptr<MountPoint> DiskManager::MaybeWrapMountPointForEject(
    std::unique_ptr<MountPoint> mount_point, const Disk& disk) {
  if (!disk.IsOpticalDisk()) {
    return mount_point;
  }
  return std::make_unique<EjectingMountPoint>(std::move(mount_point), this,
                                              disk.device_file);
}

bool DiskManager::UnmountAll() {
  // UnmountAll() is called when a user session ends. We do not want to eject
  // devices in that situation and thus set |eject_device_on_unmount_| to
  // false temporarily to prevent devices from being ejected upon unmount.
  eject_device_on_unmount_ = false;
  bool all_unmounted = MountManager::UnmountAll();
  eject_device_on_unmount_ = true;
  return all_unmounted;
}

}  // namespace cros_disks
