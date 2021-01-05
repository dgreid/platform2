// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mounter.h"

// Has to come before linux/fs.h due to conflicting definitions of MS_*
// constants.
#include <sys/mount.h>

#include <fcntl.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process_reaper.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/uri.h"

namespace cros_disks {

namespace {

const char kFuseDeviceFile[] = "/dev/fuse";
const int kFUSEMountFlags = MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_DIRSYNC;

class FUSEMountPoint : public MountPoint {
 public:
  FUSEMountPoint(const base::FilePath& path, const Platform* platform)
      : MountPoint({path}), platform_(platform) {}

  FUSEMountPoint(const FUSEMountPoint&) = delete;
  FUSEMountPoint& operator=(const FUSEMountPoint&) = delete;

  ~FUSEMountPoint() override { DestructorUnmount(); }

  base::WeakPtr<FUSEMountPoint> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  static void CleanUpCallback(const base::FilePath& mount_path,
                              base::WeakPtr<FUSEMountPoint> ptr,
                              const siginfo_t& info) {
    CHECK_EQ(SIGCHLD, info.si_signo);
    if (info.si_code != CLD_EXITED) {
      LOG(WARNING) << "FUSE daemon for " << quote(mount_path)
                   << " crashed with code " << info.si_code << " and status "
                   << info.si_status;
    } else if (info.si_status != 0) {
      LOG(WARNING) << "FUSE daemon for " << quote(mount_path)
                   << " exited with status " << info.si_status;
    } else {
      LOG(INFO) << "FUSE daemon for " << quote(mount_path)
                << " exited normally";
    }
    if (!ptr) {
      // If the MountPoint instance has been deleted, it was
      // already unmounted and cleaned up due to a
      // request from the browser (or logout). In this
      // case, there's nothing to do.
      // TODO(dats): Consolidate this logic into centralized place (likely
      //  MountPoint base class).
      return;
    }
    ptr->CleanUp();
  }

 private:
  // MountPoint overrides:
  MountErrorType UnmountImpl() override {
    // We take a 2-step approach to unmounting FUSE filesystems. First, try a
    // normal unmount. This lets the VFS flush any pending data and lets the
    // filesystem shut down cleanly. If the filesystem is busy, force unmount
    // the filesystem. This is done because there is no good recovery path the
    // user can take, and these filesystem are sometimes unmounted implicitly on
    // login/logout/suspend. This action is similar to native filesystems (i.e.
    // FAT32, ext2/3/4, etc) which are lazy unmounted if a regular unmount fails
    // because the filesystem is busy.

    MountErrorType error = platform_->Unmount(path().value(), 0 /* flags */);
    if (error != MOUNT_ERROR_PATH_ALREADY_MOUNTED) {
      // MOUNT_ERROR_PATH_ALREADY_MOUNTED is returned on EBUSY.
      return error;
    }

    // For FUSE filesystems, MNT_FORCE will cause the kernel driver to
    // immediately close the channel to the user-space driver program and cancel
    // all outstanding requests. However, if any program is still accessing the
    // filesystem, the umount2() will fail with EBUSY and the mountpoint will
    // still be attached. Since the mountpoint is no longer valid, use
    // MNT_DETACH to also force the mountpoint to be disconnected.
    LOG(WARNING) << "Mount point " << quote(path())
                 << " is busy, using force unmount";
    return platform_->Unmount(path().value(), MNT_FORCE | MNT_DETACH);
  }

  void CleanUp() {
    MountErrorType unmount_error = Unmount();
    LOG_IF(ERROR, unmount_error != MOUNT_ERROR_NONE)
        << "Cannot unmount FUSE mount point " << quote(path())
        << " after process exit: " << unmount_error;

    if (!platform_->RemoveEmptyDirectory(path().value())) {
      PLOG(ERROR) << "Cannot remove FUSE mount point " << quote(path().value())
                  << " after process exit";
    }
  }

  const Platform* platform_;

  base::WeakPtrFactory<FUSEMountPoint> weak_factory_{this};
};

bool GetPhysicalBlockSize(const std::string& source, int* size) {
  base::ScopedFD fd(open(source.c_str(), O_RDONLY | O_CLOEXEC));

  *size = 0;
  if (!fd.is_valid()) {
    PLOG(WARNING) << "Couldn't open " << source;
    return false;
  }

  if (ioctl(fd.get(), BLKPBSZGET, size) < 0) {
    PLOG(WARNING) << "Failed to get block size for" << source;
    return false;
  }

  return true;
}

}  // namespace

FUSESandboxedProcessFactory::FUSESandboxedProcessFactory(
    const Platform* platform,
    SandboxedExecutable executable,
    OwnerUser run_as,
    bool has_network_access,
    std::vector<gid_t> supplementary_groups,
    base::Optional<base::FilePath> mount_namespace)
    : platform_(platform),
      executable_(std::move(executable.executable)),
      seccomp_policy_(std::move(executable.seccomp_policy)),
      run_as_(std::move(run_as)),
      has_network_access_(has_network_access),
      supplementary_groups_(std::move(supplementary_groups)),
      mount_namespace_(std::move(mount_namespace)) {
  CHECK(executable_.IsAbsolute());
  if (seccomp_policy_) {
    CHECK(seccomp_policy_.value().IsAbsolute());
  }
  if (mount_namespace_) {
    CHECK(mount_namespace_.value().IsAbsolute());
  }
}

FUSESandboxedProcessFactory::~FUSESandboxedProcessFactory() = default;

std::unique_ptr<SandboxedProcess>
FUSESandboxedProcessFactory::CreateSandboxedProcess() const {
  auto sandbox = std::make_unique<SandboxedProcess>();
  if (!ConfigureSandbox(sandbox.get()))
    return nullptr;
  return sandbox;
}

bool FUSESandboxedProcessFactory::ConfigureSandbox(
    SandboxedProcess* sandbox) const {
  sandbox->SetCapabilities(0);
  sandbox->SetNoNewPrivileges();

  // The FUSE mount program is put under a new mount namespace, so mounts
  // inside that namespace don't normally propagate.
  sandbox->NewMountNamespace();
  sandbox->SkipRemountPrivate();

  // TODO(benchan): Re-enable cgroup namespace when either Chrome OS
  // kernel 3.8 supports it or no more supported devices use kernel
  // 3.8.
  // mount_process.NewCgroupNamespace();

  sandbox->NewIpcNamespace();

  sandbox->NewPidNamespace();

  // Prepare mounts for pivot_root.
  if (!sandbox->SetUpMinimalMounts()) {
    LOG(ERROR) << "Cannot set up minijail mounts";
    return false;
  }

  // /run is the place where mutable system configs are being kept.
  // We don't expose them by default, but to be able to bind them when
  // needed /run needs to be writeable.
  if (!sandbox->Mount("tmpfs", "/run", "tmpfs", "mode=0755,size=1M")) {
    LOG(ERROR) << "Cannot mount /run";
    return false;
  }

  if (!has_network_access_) {
    sandbox->NewNetworkNamespace();
  } else {
    // Network DNS configs are in /run/shill.
    if (!sandbox->BindMount("/run/shill", "/run/shill", false, false)) {
      LOG(ERROR) << "Cannot bind /run/shill";
      return false;
    }
    // Hardcoded hosts are mounted into /etc/hosts.d when Crostini is enabled.
    if (platform_->PathExists("/etc/hosts.d") &&
        !sandbox->BindMount("/etc/hosts.d", "/etc/hosts.d", false, false)) {
      LOG(ERROR) << "Cannot bind /etc/hosts.d";
      return false;
    }
  }

  if (!sandbox->EnterPivotRoot()) {
    LOG(ERROR) << "Cannot pivot root";
    return false;
  }

  if (seccomp_policy_) {
    if (!platform_->PathExists(seccomp_policy_.value().value())) {
      LOG(ERROR) << "Seccomp policy " << quote(seccomp_policy_.value())
                 << " is missing";
      return false;
    }
    sandbox->LoadSeccompFilterPolicy(seccomp_policy_.value().value());
  }

  sandbox->SetUserId(run_as_.uid);
  sandbox->SetGroupId(run_as_.gid);
  if (!supplementary_groups_.empty()) {
    sandbox->SetSupplementaryGroupIds(supplementary_groups_);
  }

  // Enter mount namespace in the sandbox if necessary.
  if (mount_namespace_) {
    sandbox->EnterExistingMountNamespace(mount_namespace_.value().value());
  }

  if (!platform_->PathExists(executable_.value())) {
    LOG(ERROR) << "Cannot find mount program " << quote(executable_);
    return false;
  }
  sandbox->AddArgument(executable_.value());

  return true;
}

FUSEMounter::FUSEMounter(const Platform* platform,
                         brillo::ProcessReaper* process_reaper,
                         std::string filesystem_type,
                         Config config)
    : platform_(platform),
      process_reaper_(process_reaper),
      filesystem_type_(std::move(filesystem_type)),
      config_(std::move(config)) {}

FUSEMounter::~FUSEMounter() = default;

std::unique_ptr<MountPoint> FUSEMounter::Mount(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    MountErrorType* error) const {
  // Read-only is the only parameter that has any effect at this layer.
  const bool read_only = config_.read_only || IsReadOnlyMount(params);

  const base::File fuse_file = base::File(
      base::FilePath(kFuseDeviceFile),
      base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WRITE);
  if (!fuse_file.IsValid()) {
    LOG(ERROR) << "Unable to open FUSE device file. Error: "
               << fuse_file.error_details() << " "
               << base::File::ErrorToString(fuse_file.error_details());
    *error = MOUNT_ERROR_INTERNAL;
    return nullptr;
  }

  // Mount options for FUSE:
  // fd - File descriptor for /dev/fuse.
  // user_id/group_id - user/group for file access control. Essentially
  //     bypassed due to allow_other, but still required to be set.
  // allow_other - Allows users other than user_id/group_id to access files
  //     on the file system. By default, FUSE prevents any process other than
  //     ones running under user_id/group_id to access files, regardless of
  //     the file's permissions.
  // default_permissions - Enforce permission checking.
  // rootmode - Mode bits for the root inode.
  std::string fuse_mount_options = base::StringPrintf(
      "fd=%d,user_id=%u,group_id=%u,allow_other,default_permissions,"
      "rootmode=%o",
      fuse_file.GetPlatformFile(), kChronosUID, kChronosAccessGID, S_IFDIR);

  std::string fuse_type = "fuse";
  std::string source_descr = source;
  base::stat_wrapper_t statbuf = {0};
  if (platform_->Lstat(source, &statbuf) && S_ISBLK(statbuf.st_mode)) {
    int blksize = 0;

    // TODO(crbug.com/931500): It's possible that specifying a block size equal
    // to the file system cluster size (which might be larger than the physical
    // block size) might be more efficient. Data would be needed to see what
    // kind of performance benefit, if any, could be gained. At the very least,
    // specify the block size of the underlying device. Without this, UFS cards
    // with 4k sector size will fail to mount.
    if (GetPhysicalBlockSize(source, &blksize) && blksize > 0)
      fuse_mount_options.append(base::StringPrintf(",blksize=%d", blksize));

    LOG(INFO) << "Source file " << quote(source)
              << " is a block device with block size " << blksize;

    fuse_type = "fuseblk";
  } else {
    source_descr = "fuse:" + source;
  }

  if (!filesystem_type_.empty()) {
    fuse_type += ".";
    fuse_type += filesystem_type_;
  }
  *error = platform_->Mount(source_descr, target_path.value(), fuse_type,
                            kFUSEMountFlags | (read_only ? MS_RDONLY : 0) |
                                (config_.nosymfollow ? MS_NOSYMFOLLOW : 0),
                            fuse_mount_options);

  if (*error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Cannot perform unprivileged FUSE mount: " << *error;
    return nullptr;
  }

  pid_t pid =
      StartDaemon(fuse_file, source, target_path, std::move(params), error);
  if (*error != MOUNT_ERROR_NONE || pid == Process::kInvalidProcessId) {
    LOG(ERROR) << "FUSE daemon start failure: " << *error;
    LOG(INFO) << "FUSE cleanup on start failure for " << quote(target_path);
    MountErrorType unmount_error =
        platform_->Unmount(target_path.value(), MNT_FORCE | MNT_DETACH);
    LOG_IF(ERROR, unmount_error != MOUNT_ERROR_NONE)
        << "Cannot unmount FUSE mount point " << quote(target_path)
        << " after launch failure: " << unmount_error;
    return nullptr;
  }

  // At this point, the FUSE daemon has successfully started.
  std::unique_ptr<FUSEMountPoint> mount_point =
      std::make_unique<FUSEMountPoint>(target_path, platform_);

  // Add a watcher that cleans up the FUSE mount when the process exits.
  // This is defined as in-jail "init" process, denoted by pid(),
  // terminates, which happens only when the last process in the jailed PID
  // namespace terminates.
  process_reaper_->WatchForChild(
      FROM_HERE, pid,
      base::BindOnce(&FUSEMountPoint::CleanUpCallback, target_path,
                     mount_point->GetWeakPtr()));

  *error = MOUNT_ERROR_NONE;
  return std::move(mount_point);
}

pid_t FUSEMounter::StartDaemon(const base::File& fuse_file,
                               const std::string& source,
                               const base::FilePath& target_path,
                               std::vector<std::string> params,
                               MountErrorType* error) const {
  auto mount_process =
      PrepareSandbox(source, target_path, std::move(params), error);
  if (*error != MOUNT_ERROR_NONE) {
    return Process::kInvalidProcessId;
  }

  mount_process->AddArgument(
      base::StringPrintf("/dev/fd/%d", fuse_file.GetPlatformFile()));

  std::vector<std::string> output;
  const int return_code = mount_process->Run(&output);
  *error = InterpretReturnCode(return_code);

  if (*error != MOUNT_ERROR_NONE) {
    const auto& executable = mount_process->arguments()[0];
    if (!output.empty()) {
      LOG(ERROR) << "FUSE mount program " << quote(executable) << " outputted "
                 << output.size() << " lines:";
      for (const std::string& line : output) {
        LOG(ERROR) << line;
      }
    }
    LOG(ERROR) << "FUSE mount program " << quote(executable)
               << " returned error code " << return_code;
    return Process::kInvalidProcessId;
  }

  return mount_process->pid();
}

MountErrorType FUSEMounter::InterpretReturnCode(int return_code) const {
  if (return_code != 0)
    return MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
  return MOUNT_ERROR_NONE;
}

FUSEMounterHelper::FUSEMounterHelper(
    const Platform* platform,
    brillo::ProcessReaper* process_reaper,
    std::string filesystem_type,
    bool nosymfollow,
    const SandboxedProcessFactory* sandbox_factory)
    : FUSEMounter(platform,
                  process_reaper,
                  std::move(filesystem_type),
                  {.nosymfollow = nosymfollow}),
      sandbox_factory_(sandbox_factory) {}

FUSEMounterHelper::~FUSEMounterHelper() = default;

std::unique_ptr<SandboxedProcess> FUSEMounterHelper::PrepareSandbox(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    MountErrorType* error) const {
  auto sandbox = sandbox_factory_->CreateSandboxedProcess();
  if (!sandbox) {
    *error = MOUNT_ERROR_INTERNAL;
    return nullptr;
  }
  *error =
      ConfigureSandbox(source, target_path, std::move(params), sandbox.get());
  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }
  return sandbox;
}

}  // namespace cros_disks
