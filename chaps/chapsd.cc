// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the Chaps daemon. It handles calls from multiple processes via D-Bus.
//

#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "chaps/chaps_adaptor.h"
#include "chaps/chaps_factory_impl.h"
#include "chaps/chaps_service.h"
#include "chaps/chaps_utility.h"
#include "chaps/dbus_bindings/constants.h"
#include "chaps/platform_globals.h"
#include "chaps/slot_manager_impl.h"

#if USE_TPM2
#include "chaps/tpm2_utility_impl.h"
#else
#include "chaps/tpm_utility_impl.h"
#endif

using base::AutoLock;
using base::FilePath;
using base::Lock;
using base::PlatformThread;
using base::PlatformThreadHandle;
using base::WaitableEvent;
using chaps::kPersistentLogLevelPath;
using std::string;

namespace {

const char kTpmThreadName[] = "tpm_background_thread";
const char kInitThreadName[] = "async_init_thread";

void MaskSignals() {
  sigset_t signal_mask;
  CHECK_EQ(0, sigemptyset(&signal_mask));
  for (int signal : {SIGTERM, SIGINT, SIGHUP}) {
    CHECK_EQ(0, sigaddset(&signal_mask, signal));
  }
  CHECK_EQ(0, sigprocmask(SIG_BLOCK, &signal_mask, nullptr));
}

void InitAsync(WaitableEvent* started_event,
               Lock* lock,
               chaps::TPMUtility* tpm,
               chaps::SlotManagerImpl* slot_manager) {
  // It's important that we acquire 'lock' before signaling 'started_event'.
  // This will prevent any D-Bus requests from being processed until we've
  // finished initialization.
  AutoLock auto_lock(*lock);
  started_event->Signal();
  LOG(INFO) << "Starting asynchronous initialization.";
  if (!tpm->Init())
    // Just warn and continue in this case.  The effect will be a functional
    // daemon which handles dbus requests but any attempt to load a token will
    // fail.  To a PKCS #11 client this will look like a library with a few
    // empty slots.
    LOG(WARNING) << "TPM initialization failed (this is expected if no TPM is"
                 << " available).  PKCS #11 tokens will not be available.";
  if (!slot_manager->Init())
    LOG(FATAL) << "Slot initialization failed.";
}

void SetProcessUserAndGroup(const char* user_name, const char* group_name) {
  // Make the umask more restrictive: u + rwx, g + rx.
  umask(0027);

  ScopedMinijail j(minijail_new());
  minijail_change_user(j.get(), user_name);
  minijail_change_group(j.get(), group_name);
  minijail_inherit_usergroups(j.get());
  minijail_no_new_privs(j.get());
  minijail_enter(j.get());
}

}  // namespace

namespace chaps {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon(const std::string& srk_auth_data, bool auto_load_system_token)
      : DBusServiceDaemon(kChapsServiceName),
        srk_auth_data_(srk_auth_data),
        auto_load_system_token_(auto_load_system_token),
        tpm_background_thread_(kTpmThreadName),
        async_init_thread_(kInitThreadName) {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override {
    // We join these two threads here so that the code running in these two
    // threads can be certain that all the other members of this class will
    // be available when the thread is still running.
    async_init_thread_.Stop();

    // adaptor_ contains a pointer to service_
    adaptor_.reset();

    // service_ contains a pointer to slot_manager_
    service_.reset();

    // Destructor of slot_manager_ will use tpm_
    slot_manager_.reset();

#if USE_TPM2
    // tpm_ will need tpm_background_thread_ to function
    tpm_.reset();
    tpm_background_thread_.Stop();
#endif
  }

 protected:
  int OnInit() override {
#if USE_TPM2
    CHECK(tpm_background_thread_.StartWithOptions(base::Thread::Options(
        base::MessagePumpType::IO, 0 /* use default stack size */)));
    tpm_.reset(new TPM2UtilityImpl(tpm_background_thread_.task_runner()));
#else
    // Instantiate a TPM1.2 Utility.
    tpm_.reset(new TPMUtilityImpl(srk_auth_data_));
#endif

    factory_.reset(new ChapsFactoryImpl);
    system_shutdown_blocker_.reset(
        new SystemShutdownBlocker(base::ThreadTaskRunnerHandle::Get()));
    slot_manager_.reset(new SlotManagerImpl(factory_.get(), tpm_.get(),
                                            auto_load_system_token_,
                                            system_shutdown_blocker_.get()));
    service_.reset(new ChapsServiceImpl(slot_manager_.get()));

    // Initialize the TPM utility and slot manager asynchronously because
    // we might be able to serve some requests while they are being
    // initialized.
    WaitableEvent init_started(WaitableEvent::ResetPolicy::MANUAL,
                               WaitableEvent::InitialState::NOT_SIGNALED);
    CHECK(async_init_thread_.StartWithOptions(base::Thread::Options(
        base::MessagePumpType::IO, 0 /* use default stack size */)));
    async_init_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&InitAsync, &init_started, &lock_, tpm_.get(),
                              slot_manager_.get()));
    // We're not finished with initialization until the initialization thread
    // has had a chance to acquire the lock.
    init_started.Wait();

    // Now we can export D-Bus objects.
    int return_code = DBusServiceDaemon::OnInit();
    if (return_code != EX_OK)
      return return_code;

    RegisterHandler(SIGTERM, base::Bind(&Daemon::ShutdownSignalHandler,
                                        base::Unretained(this)));
    RegisterHandler(SIGINT, base::Bind(&Daemon::ShutdownSignalHandler,
                                       base::Unretained(this)));

    return EX_OK;
  }

  void OnShutdown(int* exit_code) override {
    // TODO(https://crbug.com/844537): Remove when root cause of disappearing
    // system token certificates is found.
    LOG(INFO) << "chapsd Daemon::OnShutdown invoked.";
    DBusServiceDaemon::OnShutdown(exit_code);
  }

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_.reset(
        new ChapsAdaptor(bus_, &lock_, service_.get(), slot_manager_.get()));
    adaptor_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() failed", true));
  }

 private:
  // Mimicks |brillo::Daemon::Shutdown| but also logs the incoming signal.
  // TODO(https://crbug.com/844537): Remove when root cause of disappearing
  // system token certificates is found.
  bool ShutdownSignalHandler(const signalfd_siginfo& info) {
    // Trigger daemon shutdown, because the signal handler replaces the
    // original signal handler from |brillo::Daemon|.
    LOG(INFO) << "Shutdown triggered by signal " << info.ssi_signo << ".";
    Quit();
    return true;  // Unregister the signal handler.
  }

  std::string srk_auth_data_;
  bool auto_load_system_token_;
  base::Thread tpm_background_thread_;
  base::Thread async_init_thread_;
  Lock lock_;

  std::unique_ptr<TPMUtility> tpm_;
  std::unique_ptr<ChapsFactory> factory_;
  std::unique_ptr<SystemShutdownBlocker> system_shutdown_blocker_;
  std::unique_ptr<SlotManagerImpl> slot_manager_;
  std::unique_ptr<ChapsInterface> service_;
  std::unique_ptr<ChapsAdaptor> adaptor_;
};

}  // namespace chaps

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  chaps::ScopedOpenSSL openssl;

  if (!cl->HasSwitch("v")) {
    // Read persistent file for log level if no command line verbositity level
    // is specified.
    std::string log_level;
    bool success =
        base::ReadFileToString(FilePath(kPersistentLogLevelPath), &log_level);
    if (success) {
      int log_level_int;
      if (base::StringToInt(log_level, &log_level_int))
        logging::SetMinLogLevel(log_level_int);
      int delete_success = base::DeleteFile(FilePath(kPersistentLogLevelPath));
      VLOG_IF(2, !delete_success) << "Failed to delete log level file.";
    }
  }

  LOG(INFO) << "Starting PKCS #11 services.";
  // Run as 'chaps'.
  SetProcessUserAndGroup(chaps::kChapsdProcessUser, chaps::kChapsdProcessGroup);
  // Determine SRK authorization data from the command line.
  string srk_auth_data;
  if (cl->HasSwitch("srk_password")) {
    srk_auth_data = cl->GetSwitchValueASCII("srk_password");
  } else if (cl->HasSwitch("srk_zeros")) {
    int zero_count = 0;
    if (base::StringToInt(cl->GetSwitchValueASCII("srk_zeros"), &zero_count)) {
      srk_auth_data = string(zero_count, 0);
    } else {
      LOG(WARNING) << "Invalid value for srk_zeros: using empty string.";
    }
  }
  bool auto_load_system_token = cl->HasSwitch("auto_load_system_token");
  // Mask signals handled by the daemon thread. This makes sure we
  // won't handle shutdown signals on one of the other threads spawned
  // below.
  MaskSignals();
  LOG(INFO) << "Starting D-Bus dispatcher.";
  chaps::Daemon(srk_auth_data, auto_load_system_token).Run();
  return 0;
}
