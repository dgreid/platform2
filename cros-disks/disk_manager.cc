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
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/system_mounter.h"

namespace cros_disks {

namespace {

// Implementation of FUSEMounter aimed at removable storage with
// exFAT or NTFS filesystems.
class DiskFUSEMounter : public FUSEMounter {
 public:
  DiskFUSEMounter(const Platform* platform,
                  brillo::ProcessReaper* reaper,
                  std::string filesystem_type,
                  const SandboxedProcessFactory* upstream_factory,
                  SandboxedExecutable executable,
                  OwnerUser run_as,
                  std::vector<std::string> options)
      : FUSEMounter(platform, reaper, std::move(filesystem_type), {}),
        upstream_factory_(upstream_factory),
        sandbox_factory_(platform,
                         std::move(executable),
                         run_as,
                         false,  // no network needed
                         {},
                         {}),
        options_(std::move(options)) {}

 private:
  // FUSEMounter overrides:
  bool CanMount(const std::string& source,
                const std::vector<std::string>& params,
                base::FilePath* suggested_name) const override {
    if (suggested_name)
      *suggested_name = base::FilePath("disk");
    return true;
  }

  std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath&,
      std::vector<std::string>,
      MountErrorType* error) const override {
    auto device = base::FilePath(source);

    if (!device.IsAbsolute() || device.ReferencesParent() ||
        !base::StartsWith(device.value(), "/dev/",
                          base::CompareCase::SENSITIVE)) {
      LOG(ERROR) << "Source path " << quote(device) << " is invalid";
      *error = MOUNT_ERROR_INVALID_ARGUMENT;
      return nullptr;
    }

    if (!platform()->PathExists(device.value())) {
      LOG(ERROR) << "Source path " << quote(device) << " does not exist";
      *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
      return nullptr;
    }

    // Make sure the FUSE user can read-write to the device.
    if (!platform()->SetOwnership(device.value(), getuid(),
                                  sandbox_factory_.run_as().gid) ||
        !platform()->SetPermissions(source,
                                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
      LOG(ERROR) << "Can't set up permissions on " << quote(source);
      *error = MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
      return nullptr;
    }

    std::unique_ptr<SandboxedProcess> sandbox;
    // For tests we use injected factory.
    if (upstream_factory_) {
      sandbox = upstream_factory_->CreateSandboxedProcess();
      sandbox->AddArgument(sandbox_factory_.executable().value());
    } else {
      sandbox = sandbox_factory_.CreateSandboxedProcess();
    }

    // Bind-mount the device into the sandbox.
    if (!sandbox->BindMount(device.value(), device.value(), true, false)) {
      LOG(ERROR) << "Can't bind the device " << quote(device)
                 << " into the sandbox";
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }

    if (!options_.empty()) {
      sandbox->AddArgument("-o");
      sandbox->AddArgument(base::JoinString(options_, ","));
    }

    sandbox->AddArgument(device.value());

    *error = MOUNT_ERROR_NONE;
    return sandbox;
  }

  // Used to inject mocks for testing.
  const SandboxedProcessFactory* const upstream_factory_;

  const FUSESandboxedProcessFactory sandbox_factory_;
  const std::vector<std::string> options_;
};

// Specialization of a system mounter which deals with FAT-specific
// mount options.
class FATMounter : public SystemMounter {
 public:
  FATMounter(const Platform* platform, std::vector<std::string> options)
      : SystemMounter(
            platform, "vfat", /* read_only= */ false, std::move(options)) {}

 private:
  MountErrorType ParseParams(
      std::vector<std::string> params,
      std::vector<std::string>* mount_options) const override {
    // FAT32 stores times as local time instead of UTC. By default, the vfat
    // kernel module will use the kernel's time zone, which is set using
    // settimeofday(), to interpret time stamps as local time. However, time
    // zones are complicated and generally a user-space concern in modern Linux.
    // The man page for {get,set}timeofday comments that the |timezone| fields
    // of these functions is obsolete. Chrome OS doesn't appear to set these
    // either. Instead, we pass the time offset explicitly as a mount option so
    // that the user can see file time stamps as local time. This mirrors what
    // the user will see in other operating systems.
    time_t now = base::Time::Now().ToTimeT();
    struct tm timestruct;
    // The time zone might have changed since cros-disks was started. Force a
    // re-read of the time zone to ensure the local time is what the user
    // expects.
    tzset();
    localtime_r(&now, &timestruct);
    // tm_gmtoff is a glibc extension.
    int64_t offset_minutes = static_cast<int64_t>(timestruct.tm_gmtoff) / 60;
    std::string offset_option =
        base::StringPrintf("time_offset=%" PRId64, offset_minutes);

    mount_options->push_back(offset_option);

    return SystemMounter::ParseParams(std::move(params), mount_options);
  }
};

}  // namespace

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

  EjectingMountPoint(const EjectingMountPoint&) = delete;
  EjectingMountPoint& operator=(const EjectingMountPoint&) = delete;

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
};

DiskManager::DiskManager(const std::string& mount_root,
                         Platform* platform,
                         Metrics* metrics,
                         brillo::ProcessReaper* process_reaper,
                         DiskMonitor* disk_monitor,
                         DeviceEjector* device_ejector,
                         const SandboxedProcessFactory* test_sandbox_factory)
    : MountManager(mount_root, platform, metrics, process_reaper),
      disk_monitor_(disk_monitor),
      device_ejector_(device_ejector),
      test_sandbox_factory_(test_sandbox_factory),
      eject_device_on_unmount_(true) {}

DiskManager::~DiskManager() {
  UnmountAll();
}

bool DiskManager::Initialize() {
  OwnerUser run_as_exfat;
  if (!platform()->GetUserAndGroupId("fuse-exfat", &run_as_exfat.uid,
                                     &run_as_exfat.gid)) {
    PLOG(ERROR) << "Cannot resolve fuse-exfat user";
    return false;
  }
  OwnerUser run_as_ntfs;
  if (!platform()->GetUserAndGroupId("ntfs-3g", &run_as_ntfs.uid,
                                     &run_as_ntfs.gid)) {
    PLOG(ERROR) << "Cannot resolve ntfs-3g user";
    return false;
  }

  std::string uid = base::StringPrintf("uid=%d", kChronosUID);
  std::string gid = base::StringPrintf("gid=%d", kChronosAccessGID);

  // FAT32 - typical USB stick/SD card filesystem.
  mounters_["vfat"] = std::make_unique<FATMounter>(
      platform(),
      std::vector<std::string>{MountOptions::kOptionFlush, "shortname=mixed",
                               MountOptions::kOptionUtf8, uid, gid});

  // Fancier newer version of FAT used for new big SD cards and USB sticks.
  mounters_["exfat"] = std::make_unique<DiskFUSEMounter>(
      platform(), process_reaper(), "exfat", test_sandbox_factory_,
      SandboxedExecutable{base::FilePath("/usr/sbin/mount.exfat-fuse")},
      run_as_exfat,
      std::vector<std::string>{MountOptions::kOptionDirSync, uid, gid});

  // External drives and some big USB sticks would likely have NTFS.
  mounters_["ntfs"] = std::make_unique<DiskFUSEMounter>(
      platform(), process_reaper(), "ntfs", test_sandbox_factory_,
      SandboxedExecutable{base::FilePath("/usr/bin/ntfs-3g")}, run_as_ntfs,
      std::vector<std::string>{MountOptions::kOptionDirSync, uid, gid});

  // Typical CD/DVD filesystem. Inherently read-only.
  mounters_["iso9660"] = std::make_unique<SystemMounter>(
      platform(), "iso9660", true,
      std::vector<std::string>{MountOptions::kOptionUtf8, uid, gid});

  // Newer DVD filesystem. Inherently read-only.
  mounters_["udf"] = std::make_unique<SystemMounter>(
      platform(), "udf", true,
      std::vector<std::string>{MountOptions::kOptionUtf8, uid, gid});

  // MacOS's HFS+ is not properly/officially supported, but sort of works,
  // although with severe limitaions.
  mounters_["hfsplus"] = std::make_unique<SystemMounter>(
      platform(), "hfsplus", false, std::vector<std::string>{uid, gid});

  // Have no reasonable explanation why would one have external media
  // with a native Linux, filesystem and use CrOS to access it, given
  // all the problems and limitations they would face, but for compatibility
  // with previous versions we keep it unofficially supported.
  mounters_["ext4"] = std::make_unique<SystemMounter>(
      platform(), "ext4", false, std::vector<std::string>{});
  mounters_["ext3"] = std::make_unique<SystemMounter>(
      platform(), "ext3", false, std::vector<std::string>{});
  mounters_["ext2"] = std::make_unique<SystemMounter>(
      platform(), "ext2", false, std::vector<std::string>{});

  return MountManager::Initialize();
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
    MountOptions*,
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

  if (!platform()->PathExists(disk.device_file)) {
    LOG(ERROR) << quote(source_path) << " has device file "
               << quote(disk.device_file) << " which is missing";
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

  auto it = mounters_.find(device_filesystem_type);
  if (it == mounters_.end()) {
    LOG(ERROR) << "Unsupported file system type "
               << quote(device_filesystem_type) << " of device "
               << quote(source_path);
    *error = MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
    return nullptr;
  }

  const Mounter* mounter = it->second.get();

  auto applied_options = options;
  bool media_read_only = disk.is_read_only || disk.IsOpticalDisk();
  if (media_read_only && !IsReadOnlyMount(applied_options)) {
    applied_options.push_back(MountOptions::kOptionReadOnly);
  }

  std::unique_ptr<MountPoint> mount_point =
      mounter->Mount(disk.device_file, mount_path, applied_options, error);
  if (*error != MOUNT_ERROR_NONE) {
    DCHECK(!mount_point);
    // Try to mount the filesystem read-only if mounting it read-write failed.
    if (!IsReadOnlyMount(applied_options)) {
      LOG(INFO) << "Trying to mount " << quote(source_path) << " read-only";
      applied_options.push_back(MountOptions::kOptionReadOnly);
      mount_point =
          mounter->Mount(disk.device_file, mount_path, applied_options, error);
    }
  }

  if (*error != MOUNT_ERROR_NONE) {
    DCHECK(!mount_point);
    return nullptr;
  }

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
