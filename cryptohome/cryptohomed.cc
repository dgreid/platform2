// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/dbus_service.h"
#include "cryptohome/service.h"

#include <cstdlib>
#include <string>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <chaps/pkcs11/cryptoki.h>
#include <brillo/syslog_logging.h>
#include <dbus/dbus.h>
#include <glib.h>
#include <openssl/evp.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/platform.h"
#include "cryptohome/userdataauth.h"

namespace switches {
static const char* kAttestationMode = "attestation_mode";
static const char* kDistributedModeOption = "dbus";
// Keeps std* open for debugging.
static const char* kNoCloseOnDaemonize = "noclose";
static const char* kNoLegacyMount = "nolegacymount";
static const char* kNoDownloadsBindMount = "no_downloads_bind_mount";
static const char* kDirEncryption = "direncryption";
static const char* kNoDaemonize = "nodaemonize";
static const char* kUserDataAuthInterface = "user_data_auth_interface";
static const char* kCleanupThreshold = "cleanup_threshold";
static const char* kAggressiveThreshold = "aggressive_cleanup_threshold";
static const char* kTargetFreeSpace = "target_free_space";

}  // namespace switches

uint64_t ReadCleanupThreshold(const base::CommandLine* cl,
                              const char* switch_name,
                              uint64_t default_value) {
  std::string value = cl->GetSwitchValueASCII(switch_name);

  if (value.size() == 0) {
    return default_value;
  }

  uint64_t parsed_value;
  if (!base::StringToUint64(value, &parsed_value)) {
    LOG(ERROR) << "Failed to parse " << switch_name << "; using defaults";
    return default_value;
  }

  return parsed_value;
}

int main(int argc, char** argv) {
  // Initialize command line configuration early, as logging will require
  // command line to be initialized
  base::CommandLine::Init(argc, argv);

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  // Read the file before we daemonize so it can be deleted as soon as we exit.
  cryptohome::Platform platform;

  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  // Validity check of attestation mode. Historically we had monolithic and
  // distributed mode, and now the monolithic mode has been obsoleted, so we
  // expect either the switch is missing or explicitly set to distributed mode.
  if (cl->HasSwitch(switches::kAttestationMode) &&
      cl->GetSwitchValueASCII(switches::kAttestationMode) !=
          switches::kDistributedModeOption) {
    LOG(FATAL) << "Unrecognized or obsoleted " << switches::kAttestationMode
               << " option: "
               << cl->GetSwitchValueASCII(switches::kAttestationMode);
  }
  int noclose = cl->HasSwitch(switches::kNoCloseOnDaemonize);
  bool nolegacymount = cl->HasSwitch(switches::kNoLegacyMount);
  bool nodownloadsbind = cl->HasSwitch(switches::kNoDownloadsBindMount);
  bool direncryption = cl->HasSwitch(switches::kDirEncryption);
  bool daemonize = !cl->HasSwitch(switches::kNoDaemonize);
  bool use_new_dbus_interface = cl->HasSwitch(switches::kUserDataAuthInterface);
  uint64_t cleanup_threshold =
      ReadCleanupThreshold(cl, switches::kCleanupThreshold,
                           cryptohome::kFreeSpaceThresholdToTriggerCleanup);
  uint64_t aggressive_cleanup_threshold = ReadCleanupThreshold(
      cl, switches::kAggressiveThreshold,
      cryptohome::kFreeSpaceThresholdToTriggerAggressiveCleanup);
  uint64_t target_free_space = ReadCleanupThreshold(
      cl, switches::kTargetFreeSpace, cryptohome::kTargetFreeSpaceAfterCleanup);

  if (daemonize) {
    PLOG_IF(FATAL, daemon(0, noclose) == -1) << "Failed to daemonize";
  }

  // Initialize OpenSSL.
  OpenSSL_add_all_algorithms();

  // Initialize cryptohome metrics
  // Because mount thread may use metrics after main scope, don't
  // TearDownMetrics after main finished.
  cryptohome::InitializeMetrics();

  // Make sure scrypt parameters are correct.
  cryptohome::CryptoLib::AssertProductionScryptParams();

  if (use_new_dbus_interface) {
    // Note that there's an AtExitManager in the constructor of
    // UserDataAuthDaemon
    // TODO(b/171533643): Fix the AutomaticCleanup test and no longer leak this
    // object.
    cryptohome::UserDataAuthDaemon* user_data_auth_daemon =
        new cryptohome::UserDataAuthDaemon();

    // Set options on whether we are going to use legacy mount. See comments on
    // Mount::MountLegacyHome() for more information.
    user_data_auth_daemon->GetUserDataAuth()->set_legacy_mount(!nolegacymount);
    user_data_auth_daemon->GetUserDataAuth()->set_bind_mount_downloads(
        !nodownloadsbind);

    // Set options on whether we are going to use ext4 directory encryption or
    // eCryptfs.
    user_data_auth_daemon->GetUserDataAuth()->set_force_ecryptfs(
        !direncryption);

    // Set automatic cleanup thresholds.
    user_data_auth_daemon->GetUserDataAuth()->set_cleanup_threshold(
        cleanup_threshold);
    user_data_auth_daemon->GetUserDataAuth()->set_aggressive_cleanup_threshold(
        aggressive_cleanup_threshold);
    user_data_auth_daemon->GetUserDataAuth()->set_target_free_space(
        target_free_space);

    // Note the startup sequence is as following:
    // 1. UserDataAuthDaemon constructor => UserDataAuth constructor
    // 2. UserDataAuthDaemon::OnInit() (called by Daemon::Run())
    // 3. UserDataAuthDaemon::RegisterDBusObjectsAsync() (called by 2.)
    // 4. UserDataAuth::Initialize() (called by 3.)
    // 5. UserDataAuth::PostDBusInitialize() (called by 3.)
    // Daemon::OnInit() needs to be called before Initialize(), because
    // Initialize() create threads, and thus mess with Daemon's
    // AsynchronousSignalHandler.

    // Start UserDataAuth daemon if the option is selected
    user_data_auth_daemon->Run();
  } else {
    // Start the old interface if nothing is selected

    // Setup threading. This needs to be called before other calls into glib and
    // before multiple threads are created that access dbus.
    dbus_threads_init_default();

    // Create an AtExitManager
    base::AtExitManager exit_manager;

    cryptohome::Service* service = cryptohome::Service::CreateDefault();

    service->set_legacy_mount(!nolegacymount);
    service->set_bind_mount_downloads(!nodownloadsbind);
    service->set_force_ecryptfs(!direncryption);

    if (!service->Initialize()) {
      LOG(FATAL) << "Service initialization failed";
      return 1;
    }

    service->set_cleanup_threshold(cleanup_threshold);
    service->set_aggressive_cleanup_threshold(aggressive_cleanup_threshold);
    service->set_target_free_space(target_free_space);

    if (!service->Register(brillo::dbus::GetSystemBusConnection())) {
      LOG(FATAL) << "DBUS service registration failed";
      return 1;
    }

    if (!service->Run()) {
      LOG(FATAL) << "Service run failed.";
      return 1;
    }
  }
  // If PKCS #11 was initialized, this will tear it down.
  C_Finalize(NULL);

  return 0;
}
