// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file gets compiled into the 'cryptohome-namespace-mounter' executable.
// This executable performs an ephemeral mount (for Guest sessions) on behalf of
// cryptohome.
// Eventually, this executable will perform all cryptohome mounts.
// The lifetime of this executable's process matches the lifetime of the mount:
// it's launched by cryptohome when a Guest session is requested, and it's
// killed by cryptohome when the Guest session exits.

#include <sysexits.h>

#include <memory>
#include <vector>

#include <base/at_exit.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/cryptohome.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/scoped_mount_namespace.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/mount_constants.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/mount_utils.h"

#include "cryptohome/namespace_mounter_ipc.pb.h"

using base::FilePath;

namespace {

const std::vector<FilePath> kDaemonDirPaths = {
    FilePath("session_manager"), FilePath("shill"), FilePath("shill_logs")};

void CleanUpGuestDaemonDirectories(cryptohome::Platform* platform) {
  FilePath root_home_dir = brillo::cryptohome::home::GetRootPath(
      brillo::cryptohome::home::kGuestUserName);
  if (!platform->DirectoryExists(root_home_dir)) {
    // No previous Guest sessions have been started, do nothing.
    return;
  }

  for (const FilePath& daemon_path : kDaemonDirPaths) {
    FilePath to_delete = root_home_dir.Append(daemon_path);
    if (platform->DirectoryExists(to_delete)) {
      LOG(INFO) << "Attempting to delete " << to_delete.value();
      // Platform::DeleteFile() works with directories too.
      if (!platform->DeleteFile(to_delete, /*recursive=*/true)) {
        LOG(WARNING) << "Failed to delete " << to_delete.value();
      }
    }
  }
}

void TearDown(cryptohome::MountHelper* mounter) {
  mounter->TearDownEphemeralMount();
}

bool TearDownFromSignal(cryptohome::MountHelper* mounter,
                        base::Closure quit_closure,
                        const struct signalfd_siginfo&) {
  VLOG(1) << "Got signal";
  TearDown(mounter);
  quit_closure.Run();
  return true;  // unregister the handler
}

}  // namespace

int main(int argc, char** argv) {
  brillo::BaseMessageLoop message_loop;
  message_loop.SetAsCurrent();

  brillo::AsynchronousSignalHandler sig_handler;
  sig_handler.Init();

  brillo::InitLog(brillo::kLogToSyslog);

  cryptohome::ScopedMetricsInitializer metrics;

  constexpr uid_t uid = 1000;  // UID for 'chronos'.
  constexpr gid_t gid = 1000;  // GID for 'chronos'.
  constexpr gid_t access_gid = 1001;  // GID for 'chronos-access'.

  cryptohome::OutOfProcessMountRequest request;
  if (!cryptohome::ReadProtobuf(STDIN_FILENO, &request)) {
    LOG(ERROR) << "Failed to read request protobuf";
    return EX_NOINPUT;
  }

  brillo::SecureBlob system_salt;
  brillo::SecureBlob::HexStringToSecureBlob(request.system_salt(),
                                            &system_salt);

  cryptohome::Platform platform;

  // Before performing any mounts, check whether there are any leftover
  // Guest session daemon directories in /home/root/<hashed username>/.
  // See crbug.com/1069501 for details.
  if (request.username() == brillo::cryptohome::home::kGuestUserName) {
    CleanUpGuestDaemonDirectories(&platform);
  }

  std::unique_ptr<brillo::ScopedMountNamespace> ns_mnt;
  if (!request.mount_namespace_path().empty()) {
    // Enter the required mount namespace.
    ns_mnt = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(request.mount_namespace_path()));
  }

  cryptohome::MountHelper mounter(
      uid, gid, access_gid, FilePath(cryptohome::kDefaultShadowRoot),
      FilePath(cryptohome::kDefaultSkeletonSource), system_salt,
      request.legacy_home(), &platform);

  // If PerformEphemeralMount fails, or reporting back to cryptohome fails,
  // attempt to clean up.
  base::ScopedClosureRunner tear_down_runner(
      base::Bind(&TearDown, base::Unretained(&mounter)));

  cryptohome::ReportTimerStart(cryptohome::kPerformEphemeralMountTimer);
  if (!mounter.PerformEphemeralMount(request.username())) {
    cryptohome::ForkAndCrash("PerformEphemeralMount failed");
    return EX_SOFTWARE;
  }

  cryptohome::ReportTimerStop(cryptohome::kPerformEphemeralMountTimer);
  VLOG(1) << "PerformEphemeralMount succeeded";

  cryptohome::OutOfProcessMountResponse response;
  for (const auto& path : mounter.MountedPaths()) {
    response.add_paths(path.value());
  }

  if (!cryptohome::WriteProtobuf(STDOUT_FILENO, response)) {
    cryptohome::ForkAndCrash("Failed to write response protobuf");
    return EX_OSERR;
  }
  VLOG(1) << "Sent protobuf";

  // Mount and ack succeeded, release the closure without running it.
  ignore_result(tear_down_runner.Release());

  base::RunLoop run_loop;

  // Clean up mounts when we get signalled.
  sig_handler.RegisterHandler(
      SIGTERM, base::Bind(&TearDownFromSignal, base::Unretained(&mounter),
                          run_loop.QuitClosure()));

  run_loop.Run();

  return EX_OK;
}
