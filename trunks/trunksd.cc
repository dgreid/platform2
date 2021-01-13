// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <sysexits.h>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/command_line.h>
#include <base/threading/thread.h>
#include <brillo/minijail/minijail.h>
#include <brillo/syslog_logging.h>
#include <brillo/userdb_utils.h>

#include "trunks/background_command_transceiver.h"
#include "trunks/power_manager.h"
#include "trunks/resource_manager.h"
#include "trunks/tpm_handle.h"
#include "trunks/tpm_simulator_handle.h"
#include "trunks/trunks_dbus_service.h"
#include "trunks/trunks_factory_impl.h"
#include "trunks/trunks_ftdi_spi.h"

namespace {

namespace switches {

constexpr char kNoCloseOnDaemonize[] = "noclose";
constexpr char kNoDaemonize[] = "nodaemonize";
constexpr char kLogToStderr[] = "log_to_stderr";

}  // namespace switches

const uid_t kRootUID = 0;
const char kTrunksUser[] = "trunks";
const char kTrunksGroup[] = "trunks";
const char kTrunksSeccompPath[] = "/usr/share/policy/trunksd-seccomp.policy";
const char kBackgroundThreadName[] = "trunksd_background_thread";

void InitMinijailSandbox() {
  uid_t trunks_uid;
  gid_t trunks_gid;
  CHECK(brillo::userdb::GetUserInfo(kTrunksUser, &trunks_uid, &trunks_gid))
      << "Error getting trunks uid and gid.";
  CHECK_EQ(getuid(), kRootUID) << "trunksd not initialized as root.";
  brillo::Minijail* minijail = brillo::Minijail::GetInstance();
  struct minijail* jail = minijail->New();
  minijail_log_seccomp_filter_failures(jail);
  minijail->UseSeccompFilter(jail, kTrunksSeccompPath);
  minijail->DropRoot(jail, kTrunksUser, kTrunksGroup);
  minijail->Enter(jail);
  minijail->Destroy(jail);
  CHECK_EQ(getuid(), trunks_uid)
      << "trunksd was not able to drop user privilege.";
  CHECK_EQ(getgid(), trunks_gid)
      << "trunksd was not able to drop group privilege.";
}

// Add the signals, for which the handlers are added by brillo::Daemon
// to the blocked mask.
void MaskSignals() {
  sigset_t signal_mask;
  CHECK_EQ(0, sigemptyset(&signal_mask));
  for (int signal : {SIGTERM, SIGINT, SIGHUP}) {
    CHECK_EQ(0, sigaddset(&signal_mask, signal));
  }
  CHECK_EQ(0, sigprocmask(SIG_BLOCK, &signal_mask, nullptr));
  VLOG(2) << "Signal mask set.";
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch(switches::kLogToStderr)) {
    flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(flags);

  bool noclose = cl->HasSwitch(switches::kNoCloseOnDaemonize);
  bool daemonize = !cl->HasSwitch(switches::kNoDaemonize);

  // Chain together command transceivers:
  //   [IPC] --> BackgroundCommandTransceiver
  //         --> ResourceManager
  //         --> TpmHandle
  //         --> [TPM]
  trunks::CommandTransceiver* low_level_transceiver;
  if (cl->HasSwitch("ftdi")) {
    LOG(INFO) << "Sending commands to FTDI SPI.";
    low_level_transceiver = new trunks::TrunksFtdiSpi();
  } else if (cl->HasSwitch("simulator")) {
    LOG(INFO) << "Sending commands to simulator.";
    low_level_transceiver = new trunks::TpmSimulatorHandle();
  } else {
    low_level_transceiver = new trunks::TpmHandle();
  }
  CHECK(low_level_transceiver->Init())
      << "Error initializing TPM communication.";

  // Upstart would know trunksd is ready after trunksd daemonized.
  if (daemonize) {
    PLOG_IF(FATAL, daemon(0, noclose) == -1) << "Failed to daemonize";
  }

  // Create a service instance so objects like AtExitManager exist.
  trunks::TrunksDBusService service;

  // This needs to be *after* opening the TPM handle and *before* starting the
  // background thread.
  InitMinijailSandbox();
  // Make sure signals handled by the server are blocked in all threads,
  // otherwise the process still dies.
  // This needs to be *before* starting the background thread.
  MaskSignals();
  base::Thread background_thread(kBackgroundThreadName);
  CHECK(background_thread.Start()) << "Failed to start background thread.";
  trunks::TrunksFactoryImpl factory(low_level_transceiver);
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";
  trunks::ResourceManager resource_manager(factory, low_level_transceiver);
  background_thread.task_runner()->PostNonNestableTask(
      FROM_HERE, base::Bind(&trunks::ResourceManager::Initialize,
                            base::Unretained(&resource_manager)));
  trunks::BackgroundCommandTransceiver background_transceiver(
      &resource_manager, background_thread.task_runner());
  service.set_transceiver(&background_transceiver);
  trunks::PowerManager power_manager(&resource_manager);
  service.set_power_manager(&power_manager);
  LOG(INFO) << "Trunks service started.";
  int exit_code = service.Run();
  // Need to stop the background thread before destroying ResourceManager
  // and PowerManager. Otherwise, a task posted by BackgroundCommandTransceiver
  // may attempt to access those destroyed objects.
  background_thread.Stop();
  return exit_code;
}
