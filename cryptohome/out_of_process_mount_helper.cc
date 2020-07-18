// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/out_of_process_mount_helper.h"

#include <poll.h>
#include <sys/stat.h>
#include <sysexits.h>

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/mount_constants.h"
#include "cryptohome/mount_utils.h"

#include "cryptohome/namespace_mounter_ipc.pb.h"

using base::FilePath;
using base::StringPrintf;

namespace {

// Wait up to three seconds for the ephemeral mount to be performed.
// Normally, setting up a full ephemeral mount takes about 300 ms, so
// give ourselves a healthy 10x margin.
constexpr base::TimeDelta kOutOfProcessHelperMountTimeout =
    base::TimeDelta::FromSeconds(3);

// Wait one second for the helper to exit and be reaped.
// The brillo::Process::Kill() function that takes this timeout does not allow
// for sub-second granularity, and waiting more than one second for the helper
// to exit makes little sense: the helper is designed to clean up and exit
// quickly: it takes about 100 ms to clean up ephemeral mounts.
constexpr base::TimeDelta kOutOfProcessHelperReapTimeout =
    base::TimeDelta::FromSeconds(1);

bool WaitForHelper(int read_from_helper, const base::TimeDelta& timeout) {
  struct pollfd poll_fd = {};
  poll_fd.fd = read_from_helper;
  poll_fd.events = POLLIN;

  // While HANDLE_EINTR will restart the timeout, this happening repeatedly
  // should be exceedingly rare.
  int ret = HANDLE_EINTR(poll(&poll_fd, 1U, timeout.InMilliseconds()));

  if (ret < 0) {
    PLOG(ERROR) << "poll(read_from_helper) failed";
    return false;
  }

  if (ret == 0) {
    LOG(ERROR) << "WaitForHelper timed out";
    return false;
  }

  return (poll_fd.revents & POLLIN) == POLLIN;
}

std::map<cryptohome::MountType, cryptohome::OutOfProcessMountRequest_MountType>
    mount_type = {
        // Not mounted.
        {cryptohome::MountType::NONE,
         cryptohome::OutOfProcessMountRequest_MountType_NONE},
        // Encrypted with ecryptfs.
        {cryptohome::MountType::ECRYPTFS,
         cryptohome::OutOfProcessMountRequest_MountType_ECRYPTFS},
        // Encrypted with dircrypto.
        {cryptohome::MountType::DIR_CRYPTO,
         cryptohome::OutOfProcessMountRequest_MountType_DIR_CRYPTO},
        // Ephemeral mount.
        {cryptohome::MountType::EPHEMERAL,
         cryptohome::OutOfProcessMountRequest_MountType_EPHEMERAL},
};

}  // namespace

namespace cryptohome {

bool OutOfProcessMountHelper::CanPerformEphemeralMount() const {
  return !helper_process_ || helper_process_->pid() == 0;
}

bool OutOfProcessMountHelper::MountPerformed() const {
  return helper_process_ && helper_process_->pid() > 0;
}

bool OutOfProcessMountHelper::IsPathMounted(const base::FilePath& path) const {
  return mounted_paths_.count(path.value()) > 0;
}

void OutOfProcessMountHelper::KillOutOfProcessHelperIfNecessary() {
  if (helper_process_->pid() == 0) {
    return;
  }

  ReportTimerStart(kOOPMountCleanupTimer);

  if (helper_process_->Kill(SIGTERM,
                            kOutOfProcessHelperReapTimeout.InSeconds())) {
    ReportTimerStop(kOOPMountCleanupTimer);
    ReportOOPMountCleanupResult(OOPMountCleanupResult::kSuccess);
  } else {
    LOG(ERROR) << "Failed to send SIGTERM to OOP mount helper";

    // If the process didn't exit on SIGTERM, attempt SIGKILL.
    if (helper_process_->Kill(SIGKILL, 0)) {
      // If SIGKILL succeeds (with SIGTERM having failed) log the fact that
      // poking failed.
      ReportOOPMountCleanupResult(OOPMountCleanupResult::kFailedToPoke);
    } else {
      LOG(ERROR) << "Failed to kill OOP mount helper";
      ReportOOPMountCleanupResult(OOPMountCleanupResult::kFailedToKill);
    }
  }

  // Reset the brillo::Process object to close pipe file descriptors.
  helper_process_->Reset(0);
}

bool OutOfProcessMountHelper::PerformEphemeralMount(
    const std::string& username) {
  OutOfProcessMountRequest request;
  request.set_username(username);
  request.set_system_salt(SecureBlobToSecureHex(system_salt_).to_string());
  request.set_legacy_home(legacy_home_);
  request.set_mount_namespace_path(
      chrome_mnt_ns_ ? chrome_mnt_ns_->path().value() : "");
  request.set_type(cryptohome::OutOfProcessMountRequest_MountType_EPHEMERAL);

  OutOfProcessMountResponse response;
  if (!LaunchOutOfProcessHelper(request, &response)) {
    return false;
  }

  username_ = request.username();
  if (response.paths_size() > 0) {
    for (int i = 0; i < response.paths_size(); i++) {
      mounted_paths_.insert(response.paths(i));
    }
  }

  return true;
}

bool OutOfProcessMountHelper::LaunchOutOfProcessHelper(
    const OutOfProcessMountRequest& request,
    OutOfProcessMountResponse* response) {
  std::unique_ptr<brillo::Process> mount_helper =
      platform_->CreateProcessInstance();

  mount_helper->AddArg("/usr/sbin/cryptohome-namespace-mounter");

  mount_helper->RedirectUsingPipe(
      STDIN_FILENO, true /* is_input, from child's perspective */);
  mount_helper->RedirectUsingPipe(
      STDOUT_FILENO, false /* is_input, from child's perspective */);

  ReportTimerStart(kOOPMountOperationTimer);

  if (!mount_helper->Start()) {
    LOG(ERROR) << "Failed to start OOP mount helper";
    ReportOOPMountOperationResult(OOPMountOperationResult::kFailedToStart);
    return false;
  }

  helper_process_ = std::move(mount_helper);
  write_to_helper_ = helper_process_->GetPipe(STDIN_FILENO);
  int read_from_helper = helper_process_->GetPipe(STDOUT_FILENO);

  base::ScopedClosureRunner kill_runner(
      base::Bind(&OutOfProcessMountHelper::KillOutOfProcessHelperIfNecessary,
                 base::Unretained(this)));

  if (!WriteProtobuf(write_to_helper_, request)) {
    LOG(ERROR) << "Failed to write request protobuf";
    ReportOOPMountOperationResult(
        OOPMountOperationResult::kFailedToWriteRequestProtobuf);
    return false;
  }

  // Avoid blocking forever in the read(2) call below by poll(2)-ing the file
  // descriptor with a |kOutOfProcessHelperMountTimeout| long timeout.
  if (!WaitForHelper(read_from_helper, kOutOfProcessHelperMountTimeout)) {
    LOG(ERROR) << "OOP mount helper did not respond in time";
    ReportOOPMountOperationResult(
        OOPMountOperationResult::kHelperProcessTimedOut);
    return false;
  }

  if (!ReadProtobuf(read_from_helper, response)) {
    LOG(ERROR) << "Failed to read response protobuf";
    ReportOOPMountOperationResult(
        OOPMountOperationResult::kFailedToReadResponseProtobuf);
    return false;
  }

  // OOP mount helper started successfully, report elapsed time since process
  // was started.
  ReportTimerStop(kOOPMountOperationTimer);

  // OOP mount helper started successfully, release the clean-up closure.
  ignore_result(kill_runner.Release());

  LOG(INFO) << "OOP mount helper started successfully";
  ReportOOPMountOperationResult(OOPMountOperationResult::kSuccess);
  return true;
}

void OutOfProcessMountHelper::TearDownEphemeralMount() {
  if (!helper_process_) {
    LOG(WARNING) << "Can't tear down mount, OOP mount helper is not running";
    return;
  }

  // While currently a MountHelper instance is not used for more than one
  // cryptohome mount operation, this function should ensure that the
  // MountHelper instance is left in a state suited to perform subsequent
  // mounts.
  KillOutOfProcessHelperIfNecessary();
  mounted_paths_.clear();
  username_.clear();
}

bool OutOfProcessMountHelper::PerformMount(const Options& mount_opts,
                                           const std::string& username,
                                           const std::string& fek_signature,
                                           const std::string& fnek_signature,
                                           bool is_pristine,
                                           MountError* error) {
  OutOfProcessMountRequest request;
  request.set_username(username);
  request.set_system_salt(SecureBlobToSecureHex(system_salt_).to_string());
  request.set_legacy_home(legacy_home_);
  request.set_mount_namespace_path(
      chrome_mnt_ns_ ? chrome_mnt_ns_->path().value() : "");
  request.set_type(mount_type[mount_opts.type]);
  request.set_to_migrate_from_ecryptfs(mount_opts.to_migrate_from_ecryptfs);
  request.set_shadow_only(mount_opts.shadow_only);
  request.set_fek_signature(fek_signature);
  request.set_fnek_signature(fnek_signature);
  request.set_is_pristine(is_pristine);

  OutOfProcessMountResponse response;
  if (!LaunchOutOfProcessHelper(request, &response)) {
    return false;
  }

  username_ = request.username();
  if (response.paths_size() > 0) {
    for (int i = 0; i < response.paths_size(); i++) {
      mounted_paths_.insert(response.paths(i));
    }
  }

  *error = static_cast<MountError>(response.mount_error());
  return true;
}

}  // namespace cryptohome
