// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_service.h"

#include <dbus/dbus.h>  // C dbus library header. Used in FilterMessage().
#include <stdint.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/switches/chrome_switches.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/scoped_dbus_error.h>

#include "login_manager/browser_job.h"
#include "login_manager/child_exit_handler.h"
#include "login_manager/container_manager_impl.h"
#include "login_manager/dbus_signal_emitter.h"
#include "login_manager/key_generator.h"
#include "login_manager/liveness_checker_impl.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/session_manager_dbus_adaptor.h"
#include "login_manager/session_manager_impl.h"
#include "login_manager/system_utils.h"
#include "login_manager/systemd_unit_starter.h"
#include "login_manager/upstart_signal_emitter.h"
#include "power_manager/proto_bindings/suspend.pb.h"

namespace em = enterprise_management;
namespace login_manager {

namespace {

const int kSignals[] = {SIGTERM, SIGINT, SIGHUP};
const int kNumSignals = sizeof(kSignals) / sizeof(int);

// Constants for susend delays and ARC cgroup control.
const int kSuspendDelayMs = 1000;
const char kSuspendDelayDescription[] = "session_manager";

// The only path where containers are allowed to be installed.  They must be
// part of the read-only, signed root image.
const char kContainerInstallDirectory[] = "/opt/google/containers";

const base::FilePath::CharType kArcCgroupFreezerStatePath[] =
    FILE_PATH_LITERAL("/sys/fs/cgroup/freezer/android/freezer.state");

// I need a do-nothing action for SIGALRM, or using alarm() will kill me.
void DoNothing(int signal) {
}

void FireAndForgetDBusMethodCall(dbus::ObjectProxy* proxy,
                                 const char* interface,
                                 const char* method) {
  dbus::MethodCall call(interface, method);
  proxy->CallMethod(&call,
                    dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                    dbus::ObjectProxy::EmptyResponseCallback());
}

void FireAndBlockOnDBusMethodCall(dbus::ObjectProxy* proxy,
                                  const char* interface,
                                  const char* method) {
  dbus::MethodCall call(interface, method);
  proxy->CallMethodAndBlock(&call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
}

void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << "Could not connect to signal " << signal
               << " on interface " << interface;
  }
}

}  // anonymous namespace

// TODO(mkrebs): Remove CollectChrome timeout and file when
// crosbug.com/5872 is fixed.
// When crash-reporter based crash reporting of Chrome is enabled
// (which should only be during test runs) we use
// kKillTimeoutCollectChrome instead of the kill timeout specified at
// the command line.
const int SessionManagerService::kKillTimeoutCollectChrome = 60;
const char SessionManagerService::kCollectChromeFile[] =
    "/mnt/stateful_partition/etc/collect_chrome_crashes";

// Write these to the ARC cgroup freezer state path to freeze or thaw all
// of the processes in the instance.
const char SessionManagerService::kFrozen[] = "FROZEN";
const char SessionManagerService::kThawed[] = "THAWED";

void SessionManagerService::TestApi::ScheduleChildExit(pid_t pid, int status) {
  siginfo_t info;
  info.si_pid = pid;
  if (WIFEXITED(status)) {
    info.si_code = CLD_EXITED;
    info.si_status = WEXITSTATUS(status);
  } else {
    info.si_status = WTERMSIG(status);
  }
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(
          &SessionManagerService::HandleExit, session_manager_service_, info));
}

SessionManagerService::SessionManagerService(
    scoped_ptr<BrowserJobInterface> child_job,
    uid_t uid,
    int kill_timeout,
    bool enable_browser_abort_on_hang,
    base::TimeDelta hang_detection_interval,
    LoginMetrics* metrics,
    SystemUtils* utils)
    : browser_(std::move(child_job)),
      exit_on_child_done_(false),
      kill_timeout_(base::TimeDelta::FromSeconds(kill_timeout)),
      match_rule_(base::StringPrintf("type='method_call', interface='%s'",
                                     kSessionManagerInterface)),
      arc_cgroup_freezer_state_path_(kArcCgroupFreezerStatePath),
      suspend_delay_set_up_(false),
      suspend_delay_id_(-1),
      login_metrics_(metrics),
      system_(utils),
      nss_(NssUtil::Create()),
      owner_key_(nss_->GetOwnerKeyFilePath(), nss_.get()),
      key_gen_(uid, utils),
      state_key_generator_(utils, metrics),
      vpd_process_(utils),
      android_container_(utils, base::FilePath(kContainerInstallDirectory),
                         SessionManagerImpl::kArcContainerName),
      enable_browser_abort_on_hang_(enable_browser_abort_on_hang),
      liveness_checking_interval_(hang_detection_interval),
      shutting_down_(false),
      shutdown_already_(false),
      exit_code_(SUCCESS) {
  SetUpHandlers();
}

SessionManagerService::~SessionManagerService() {
  RevertHandlers();
}

bool SessionManagerService::Initialize() {
  LOG(INFO) << "SessionManagerService starting";
  InitializeDBus();

  dbus::ObjectProxy* chrome_dbus_proxy =
      bus_->GetObjectProxy(chromeos::kLibCrosServiceName,
                           dbus::ObjectPath(chromeos::kLibCrosServicePath));

  powerd_dbus_proxy_ = bus_->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

#if USE_SYSTEMD
  using InitDaemonControllerImpl = SystemdUnitStarter;
#else
  using InitDaemonControllerImpl = UpstartSignalEmitter;
#endif
  dbus::ObjectProxy* init_dbus_proxy =
      bus_->GetObjectProxy(InitDaemonControllerImpl::kServiceName,
                           dbus::ObjectPath(InitDaemonControllerImpl::kPath));

  liveness_checker_.reset(new LivenessCheckerImpl(this,
                                                  chrome_dbus_proxy,
                                                  enable_browser_abort_on_hang_,
                                                  liveness_checking_interval_));

  // Initially store in derived-type pointer, so that we can initialize
  // appropriately below.
  scoped_ptr<InitDaemonController> init_controller(
      new InitDaemonControllerImpl(init_dbus_proxy));

  SessionManagerImpl* impl = new SessionManagerImpl(
      std::move(init_controller),
      dbus_emitter_.get(),
      base::Bind(&FireAndForgetDBusMethodCall,
                 base::Unretained(chrome_dbus_proxy),
                 chromeos::kLibCrosServiceInterface,
                 chromeos::kLockScreen),
      base::Bind(&FireAndBlockOnDBusMethodCall,
                 base::Unretained(powerd_dbus_proxy_),
                 power_manager::kPowerManagerInterface,
                 power_manager::kRequestRestartMethod),
      base::Bind(&SessionManagerService::SetUpSuspendHandler,
                 base::Unretained(this)),
      base::Bind(&SessionManagerService::TearDownSuspendHandler,
                 base::Unretained(this)),
      &key_gen_,
      &state_key_generator_,
      this,
      login_metrics_,
      nss_.get(),
      system_,
      &crossystem_,
      &vpd_process_,
      &owner_key_,
      &android_container_);

  adaptor_.reset(new SessionManagerDBusAdaptor(impl));
  impl_.reset(impl);
  if (!InitializeImpl())
    return false;

  // Set any flags that were specified system-wide.
  browser_->SetExtraArguments(impl_->GetStartUpFlags());

  adaptor_->ExportDBusMethods(session_manager_dbus_object_);
  TakeDBusServiceOwnership();

  return true;
}

void SessionManagerService::Finalize() {
  LOG(INFO) << "SessionManagerService exiting";
  impl_->Finalize();
  ShutDownDBus();
}

void SessionManagerService::ScheduleShutdown() {
  SetExitAndScheduleShutdown(SUCCESS);
}

void SessionManagerService::RunBrowser() {
  browser_->RunInBackground();
  DLOG(INFO) << "Browser is " << browser_->CurrentPid();
  liveness_checker_->Start();
  // Note that |child_exit_handler_| will catch browser process termination and
  // call HandleExit().
}

void SessionManagerService::AbortBrowser(int signal,
                                         const std::string& message) {
  browser_->Kill(signal, message);
  browser_->WaitAndAbort(GetKillTimeout());
}

void SessionManagerService::RestartBrowserWithArgs(
    const std::vector<std::string>& args,
    bool args_are_extra) {
  // Waiting for Chrome to shutdown takes too much time.
  // We're killing it immediately hoping that data Chrome uses before
  // logging in is not corrupted.
  // TODO(avayvod): Remove RestartJob when crosbug.com/6924 is fixed.
  browser_->KillEverything(SIGKILL, "Restarting browser on-demand.");
  if (args_are_extra)
    browser_->SetExtraArguments(args);
  else
    browser_->SetArguments(args);
  // The browser will be restarted in HandleExit().
}

void SessionManagerService::SetBrowserSessionForUser(
    const std::string& account_id,
    const std::string& userhash) {
  browser_->StartSession(account_id, userhash);
}

void SessionManagerService::SetFlagsForUser(
    const std::string& account_id,
    const std::vector<std::string>& flags) {
  browser_->SetExtraArguments(flags);
}

bool SessionManagerService::IsBrowser(pid_t pid) {
  return (browser_ && browser_->CurrentPid() > 0 &&
          pid == browser_->CurrentPid());
}

bool SessionManagerService::IsManagedJob(pid_t pid) {
  return IsBrowser(pid);
}

void SessionManagerService::HandleExit(const siginfo_t& ignored) {
  LOG(INFO) << "Exiting process is " << browser_->GetName() << ".";

  // Clears up the whole job's process group.
  browser_->KillEverything(SIGKILL, "Ensuring browser processes are gone.");
  browser_->WaitAndAbort(GetKillTimeout());
  browser_->ClearPid();

  // Also ensure all containers are gone.
  android_container_.RequestJobExit();
  android_container_.EnsureJobExit(GetKillTimeout());

  // Do nothing if already shutting down.
  if (shutting_down_)
    return;

  liveness_checker_->Stop();

  if (impl_->ShouldEndSession()) {
    LOG(ERROR) << "Choosing to end session rather than restart browser.";
    SetExitAndScheduleShutdown(CRASH_WHILE_RESTART_DISABLED);
    return;
  }

  DCHECK(browser_.get());
  if (browser_->ShouldStop()) {
    LOG(WARNING) << "Child stopped, shutting down";
    SetExitAndScheduleShutdown(CHILD_EXITING_TOO_FAST);
  } else if (browser_->ShouldRunBrowser()) {
    // TODO(cmasone): deal with fork failing in RunBrowser()
    RunBrowser();
  } else {
    LOG(INFO) << "Should NOT run " << browser_->GetName() << " again.";
    AllowGracefulExitOrRunForever();
  }
}

void SessionManagerService::RequestJobExit() {
  if (browser_ && browser_->CurrentPid() > 0)
    browser_->Kill(SIGTERM, "");
}

void SessionManagerService::EnsureJobExit(base::TimeDelta timeout) {
  if (browser_ && browser_->CurrentPid() > 0)
    browser_->WaitAndAbort(timeout);
}

DBusHandlerResult SessionManagerService::FilterMessage(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* data) {
  SessionManagerService* service = static_cast<SessionManagerService*>(data);
  if (::dbus_message_is_method_call(
          message, kSessionManagerInterface, kSessionManagerRestartJob)) {
    const char* sender = ::dbus_message_get_sender(message);
    if (!sender) {
      LOG(ERROR) << "Call to RestartJob has no sender";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    LOG(INFO) << "Received RestartJob from " << sender;
    DBusMessage* get_pid =
        ::dbus_message_new_method_call("org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "GetConnectionUnixProcessID");
    CHECK(get_pid);
    ::dbus_message_append_args(
        get_pid, DBUS_TYPE_STRING, &sender, DBUS_TYPE_INVALID);
    DBusMessage* got_pid =
        ::dbus_connection_send_with_reply_and_block(conn, get_pid, -1, NULL);
    ::dbus_message_unref(get_pid);
    if (!got_pid) {
      LOG(ERROR) << "Could not look up sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    uint32_t pid;
    if (!::dbus_message_get_args(
            got_pid, NULL, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID)) {
      ::dbus_message_unref(got_pid);
      LOG(ERROR) << "Could not extract pid of sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    ::dbus_message_unref(got_pid);
    if (!service->IsBrowser(pid)) {
      LOG(WARNING) << "Sender of RestartJob is no child of mine!";
      DBusMessage* denial = dbus_message_new_error(message,
                                                   DBUS_ERROR_ACCESS_DENIED,
                                                   "Sender is not browser.");
      if (!denial || !::dbus_connection_send(conn, denial, NULL))
        LOG(ERROR) << "Could not create error response to RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void SessionManagerService::SetUpHandlers() {
  // I have to ignore SIGUSR1, because Xorg sends it to this process when it's
  // got no clients and is ready for new ones.  If we don't ignore it, we die.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  CHECK_EQ(sigaction(SIGUSR1, &action, NULL), 0);

  action.sa_handler = DoNothing;
  CHECK_EQ(sigaction(SIGALRM, &action, NULL), 0);

  std::vector<JobManagerInterface*> job_managers;
  job_managers.push_back(this);
  job_managers.push_back(&key_gen_);
  job_managers.push_back(&vpd_process_);
  job_managers.push_back(&android_container_);
  signal_handler_.Init();
  child_exit_handler_.Init(&signal_handler_, job_managers);
  for (int i = 0; i < kNumSignals; ++i) {
    signal_handler_.RegisterHandler(
        kSignals[i],
        base::Bind(&SessionManagerService::OnTerminationSignal,
                   base::Unretained(this)));
  }
}

bool SessionManagerService::CallPowerdMethod(
    const std::string& method_name,
    const google::protobuf::MessageLite& request,
    google::protobuf::MessageLite* reply_out) {
  dbus::MethodCall method_call(
      power_manager::kPowerManagerInterface, method_name);
  dbus::MessageWriter writer(&method_call);
  writer.AppendProtoAsArrayOfBytes(request);

  scoped_ptr<dbus::Response> response(
      powerd_dbus_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
  if (!response)
    return false;

  if (reply_out) {
    dbus::MessageReader reader(response.get());
    reader.PopArrayOfBytesAsProto(reply_out);
  }
  return true;
}

void SessionManagerService::SetUpSuspendHandler() {
  powerd_dbus_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kSuspendImminentSignal,
      base::Bind(&SessionManagerService::HandleSuspendImminent,
                 base::Unretained(this)),
      base::Bind(&HandleSignalConnected));
  powerd_dbus_proxy_->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kSuspendDoneSignal,
      base::Bind(&SessionManagerService::HandleSuspendDone,
                 base::Unretained(this)),
      base::Bind(&HandleSignalConnected));

  power_manager::RegisterSuspendDelayRequest request;
  request.set_timeout(
      base::TimeDelta::FromMilliseconds(kSuspendDelayMs).ToInternalValue());
  request.set_description(kSuspendDelayDescription);
  power_manager::RegisterSuspendDelayReply reply;

  if (!CallPowerdMethod(power_manager::kRegisterSuspendDelayMethod,
                        request, &reply)) {
    LOG(ERROR) << "Failed to set up suspend handler";
    return;
  }
  LOG(INFO) << "Registered delay " << reply.delay_id();
  suspend_delay_id_ = reply.delay_id();
  suspend_delay_set_up_ = true;
}

void SessionManagerService::TearDownSuspendHandler() {
  if (!suspend_delay_set_up_) {
    LOG(WARNING) << "TearDownSuspendHandler called, no delay registered?";
    return;
  }

  power_manager::UnregisterSuspendDelayRequest request;
  request.set_delay_id(suspend_delay_id_);

  CallPowerdMethod(power_manager::kUnregisterSuspendDelayMethod,
                   request, nullptr);
  LOG(INFO) << "Unregistered delay " << suspend_delay_id_;
  suspend_delay_set_up_ = false;
}

void SessionManagerService::SetArcCgroupState(const std::string& state) {
  // TODO(ejcaruso): Move this to wherever the ARC instance control
  // lands.
  LOG(INFO) << "Setting ARC instance state to " << state;
  if (base::WriteFile(arc_cgroup_freezer_state_path_,
                      state.c_str(), state.size()) < 0) {
    PLOG(WARNING) << "Failed to write to cgroup state file";
  }
}

void SessionManagerService::HandleSuspendImminent(dbus::Signal* signal) {
  if (!suspend_delay_set_up_) {
    LOG(WARNING) << "HandleSuspendImminent raced with unregister";
    return;
  }

  power_manager::SuspendImminent info;
  dbus::MessageReader reader(signal);
  CHECK(reader.PopArrayOfBytesAsProto(&info));
  int suspend_id = info.suspend_id();

  SetArcCgroupState(kFrozen);

  power_manager::SuspendReadinessInfo request;
  request.set_delay_id(suspend_delay_id_);
  request.set_suspend_id(suspend_id);
  CallPowerdMethod(power_manager::kHandleSuspendReadinessMethod,
                   request, nullptr);
}

void SessionManagerService::HandleSuspendDone(dbus::Signal* signal) {
  SetArcCgroupState(kThawed);
}

// This _must_ be async signal safe. No library calls or malloc'ing allowed.
void SessionManagerService::RevertHandlers() {
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  RAW_CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
  RAW_CHECK(sigaction(SIGALRM, &action, NULL) == 0);
}

base::TimeDelta SessionManagerService::GetKillTimeout() {
  if (base::PathExists(base::FilePath(kCollectChromeFile)))
    return base::TimeDelta::FromSeconds(kKillTimeoutCollectChrome);
  else
    return kill_timeout_;
}

bool SessionManagerService::InitializeImpl() {
  if (!impl_->Initialize()) {
    LOG(ERROR) << "Policy key is likely corrupt. Initiating device wipe.";
    impl_->InitiateDeviceWipe("bad_policy_key");
    impl_->Finalize();
    exit_code_ = MUST_WIPE_DEVICE;
    return false;
  }
  return true;
}

void SessionManagerService::InitializeDBus() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  CHECK(bus_->Connect());
  CHECK(bus_->SetUpAsyncOperations());

  bus_->AddFilterFunction(&SessionManagerService::FilterMessage, this);
  dbus::ScopedDBusError error;
  bus_->AddMatch(match_rule_, error.get());
  CHECK(!error.is_set()) << "Failed to add match to bus: " << error.name()
                         << ", message="
                         << (error.message() ? error.message() : "unknown.");

  session_manager_dbus_object_ =
      bus_->GetExportedObject(dbus::ObjectPath(kSessionManagerServicePath));

  dbus_emitter_.reset(new DBusSignalEmitter(session_manager_dbus_object_,
                                            kSessionManagerInterface));
}

void SessionManagerService::TakeDBusServiceOwnership() {
  // Note that this needs to happen *after* all methods are exported
  // (http://crbug.com/331431).
  // This should pass dbus::Bus::REQUIRE_PRIMARY once on the new libchrome.
  CHECK(bus_->RequestOwnershipAndBlock(kSessionManagerServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY))
      << "Unable to take ownership of " << kSessionManagerServiceName;
}

void SessionManagerService::ShutDownDBus() {
  dbus::ScopedDBusError error;
  bus_->RemoveMatch(match_rule_, error.get());
  if (error.is_set()) {
    LOG(ERROR) << "Failed to remove match from bus: " << error.name()
               << ", message="
               << (error.message() ? error.message() : "unknown.");
  }
  bus_->RemoveFilterFunction(&SessionManagerService::FilterMessage, this);
  bus_->ShutdownAndBlock();
}

void SessionManagerService::AllowGracefulExitOrRunForever() {
  if (exit_on_child_done_) {
    LOG(INFO) << "SessionManagerService set to exit on child done";
    brillo::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(&SessionManagerService::ScheduleShutdown),
                   this));
  } else {
    DLOG(INFO) << "OK, running forever...";
  }
}

void SessionManagerService::SetExitAndScheduleShutdown(ExitCode code) {
  shutting_down_ = true;
  exit_code_ = code;
  impl_->AnnounceSessionStoppingIfNeeded();

  child_exit_handler_.Reset();
  liveness_checker_->Stop();
  CleanupChildren(GetKillTimeout());
  impl_->AnnounceSessionStopped();

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&brillo::MessageLoop::BreakLoop,
                 base::Unretained(brillo::MessageLoop::current())));
  LOG(INFO) << "SessionManagerService quitting run loop";
}

void SessionManagerService::CleanupChildren(base::TimeDelta timeout) {
  RequestJobExit();
  key_gen_.RequestJobExit();
  android_container_.RequestJobExit();
  EnsureJobExit(timeout);
  key_gen_.EnsureJobExit(timeout);
  android_container_.EnsureJobExit(timeout);
}

bool SessionManagerService::OnTerminationSignal(
    const struct signalfd_siginfo& info) {
  ScheduleShutdown();
  return true;
}

}  // namespace login_manager
