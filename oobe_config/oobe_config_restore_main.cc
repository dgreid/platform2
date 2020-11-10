// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <dbus/oobe_config/dbus-constants.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/oobe_config_restore_service.h"
#include "oobe_config/rollback_constants.h"

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusObject;

namespace oobe_config {

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

}  // namespace

class OobeConfigRestoreDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit OobeConfigRestoreDaemon(bool allow_unencrypted,
                                   bool skip_reboot_for_testing)
      : DBusServiceDaemon(kOobeConfigRestoreServiceName),
        allow_unencrypted_(allow_unencrypted),
        skip_reboot_for_testing_(skip_reboot_for_testing) {}
  OobeConfigRestoreDaemon(const OobeConfigRestoreDaemon&) = delete;
  OobeConfigRestoreDaemon& operator=(const OobeConfigRestoreDaemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    auto dbus_object = std::make_unique<DBusObject>(
        nullptr, bus_,
        org::chromium::OobeConfigRestoreAdaptor::GetObjectPath());

    service_ = std::make_unique<OobeConfigRestoreService>(
        std::move(dbus_object), allow_unencrypted_, skip_reboot_for_testing_);
    service_->RegisterAsync(sequencer->GetHandler(
        "OobeConfigRestoreService.RegisterAsync() failed.", true));
  }

  void OnShutdown(int* return_code) override {
    DBusServiceDaemon::OnShutdown(return_code);
    service_.reset();
  }

 private:
  std::unique_ptr<OobeConfigRestoreService> service_;
  bool allow_unencrypted_;
  bool skip_reboot_for_testing_;
};

// Runs OobeConfigRestoreDaemon.
int RunDaemon(bool allow_unencrypted,
              bool force_start,
              bool skip_reboot_for_testing) {
  if (!force_start && base::PathExists(kOobeCompletedFile)) {
    LOG(INFO) << "OOBE is already complete.";
    return 0;
  }

  if (allow_unencrypted) {
    LOG(WARNING) << "OOBE config is starting in unencrypted mode";
  }

  if (skip_reboot_for_testing) {
    LOG(WARNING) << "OOBE config is starting with reboot disabled";
  }

  LOG(INFO) << "Starting oobe_config_restore daemon";
  OobeConfigRestoreDaemon daemon(allow_unencrypted, skip_reboot_for_testing);
  int res = daemon.Run();

  LOG(INFO) << "oobe_config_restore stopping with exit code " << res;
  return res;
}

}  // namespace oobe_config

// Execute the first stage of the restore process itself immediately (without
// waiting for Chrome to initiate it). Use only for testing.
constexpr char kTestUnencrypted[] = "test-unencrypted";
constexpr char kTestEncrypted[] = "test-encrypted";

// Starts the service using unencrypted rollback data. Use only for testing.
constexpr char kAllowUnencrypted[] = "allow-unencrypted";

// Don't reboot after stage 1. Use only for testing.
constexpr char kSkipReboot[] = "skip-reboot";

// Starts the service even if OOBE is already complete. Use only for testing.
constexpr char kForceStart[] = "force-start";

int main(int argc, char* argv[]) {
  oobe_config::InitLog();

  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch(kTestUnencrypted)) {
    return oobe_config::OobeConfig().UnencryptedRollbackRestore();
  } else if (cl->HasSwitch(kTestEncrypted)) {
    return oobe_config::OobeConfig().EncryptedRollbackRestore();
  } else {
    return oobe_config::RunDaemon(cl->HasSwitch(kAllowUnencrypted),
                                  cl->HasSwitch(kForceStart),
                                  cl->HasSwitch(kSkipReboot));
  }
}
