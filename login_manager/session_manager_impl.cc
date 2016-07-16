// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_impl.h"

#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>

#include <algorithm>
#include <locale>
#include <string>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/stl_util.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <crypto/scoped_nss_types.h>
#include <dbus/message.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "login_manager/crossystem.h"
#include "login_manager/dbus_error_types.h"
#include "login_manager/dbus_signal_emitter.h"
#include "login_manager/device_local_account_policy_service.h"
#include "login_manager/device_policy_service.h"
#include "login_manager/key_generator.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_service.h"
#include "login_manager/process_manager_service_interface.h"
#include "login_manager/regen_mitigator.h"
#include "login_manager/system_utils.h"
#include "login_manager/upstart_signal_emitter.h"
#include "login_manager/user_policy_service_factory.h"
#include "login_manager/vpd_process.h"

using base::FilePath;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::kGuestUserName;

namespace login_manager {  // NOLINT

const char SessionManagerImpl::kDemoUser[] = "demouser@";

const char SessionManagerImpl::kStarted[] = "started";
const char SessionManagerImpl::kStopping[] = "stopping";
const char SessionManagerImpl::kStopped[] = "stopped";

const char SessionManagerImpl::kLoggedInFlag[] =
    "/var/run/session_manager/logged_in";
const char SessionManagerImpl::kResetFile[] =
    "/mnt/stateful_partition/factory_install_reset";

const char SessionManagerImpl::kArcContainerName[] = "android";
const char SessionManagerImpl::kArcStartSignal[] = "start-arc-instance";
const char SessionManagerImpl::kArcStopSignal[] = "stop-arc-instance";
const char SessionManagerImpl::kArcNetworkStartSignal[] = "start-arc-network";
const char SessionManagerImpl::kArcNetworkStopSignal[] = "stop-arc-network";

const base::FilePath::CharType SessionManagerImpl::kAndroidDataDirName[] =
    FILE_PATH_LITERAL("android-data");

namespace {

// Constants used in email validation.
const char kEmailSeparator = '@';
const char kEmailLegalCharacters[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".@1234567890!#$%&'*+-/=?^_`{|}~";

// Should match chromium AccountId::kUserIdPrefix .
const char kUserIdPrefix[] = "g-";
const char kUserIdLegalCharacters[] =
    "-0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// The flag to pass to chrome to open a named socket for testing.
const char kTestingChannelFlag[] = "--testing-channel=NamedTestingInterface:";

// Device-local account state directory.
const base::FilePath::CharType kDeviceLocalAccountStateDir[] =
    FILE_PATH_LITERAL("/var/lib/device_local_accounts");

// SystemUtils::EnsureJobExit() DCHECKs if the timeout is zero, so this is the
// minimum amount of time we must wait before killing the containers.
constexpr int kContainerTimeoutSec = 1;

bool IsIncognitoUserId(const std::string& user_id) {
  const std::string lower_case_id(base::ToLowerASCII(user_id));
  return (lower_case_id == kGuestUserName) ||
         (lower_case_id == SessionManagerImpl::kDemoUser);
}

#if USE_ARC
base::FilePath GetAndroidDataDirForUser(const std::string& normalized_user_id) {
  return GetRootPath(normalized_user_id).Append(
      SessionManagerImpl::kAndroidDataDirName);
}
#endif  // USE_ARC

}  // namespace

SessionManagerImpl::Error::Error() : set_(false) {}

SessionManagerImpl::Error::Error(const std::string& name,
                                 const std::string& message)
    : name_(name), message_(message), set_(true) {}

SessionManagerImpl::Error::~Error() {}

void SessionManagerImpl::Error::Set(const std::string& name,
                                    const std::string& message) {
  name_ = name;
  message_ = message;
  set_ = true;
}

struct SessionManagerImpl::UserSession {
 public:
  UserSession(const std::string& username,
              const std::string& userhash,
              bool is_incognito,
              crypto::ScopedPK11Slot slot,
              std::unique_ptr<PolicyService> policy_service)
      : username(username),
        userhash(userhash),
        is_incognito(is_incognito),
        slot(std::move(slot)),
        policy_service(std::move(policy_service)) {}
  ~UserSession() {}

  const std::string username;
  const std::string userhash;
  const bool is_incognito;
  crypto::ScopedPK11Slot slot;
  std::unique_ptr<PolicyService> policy_service;
};

SessionManagerImpl::SessionManagerImpl(
    scoped_ptr<InitDaemonController> init_controller,
    DBusSignalEmitterInterface* dbus_emitter,
    base::Closure lock_screen_closure,
    base::Closure restart_device_closure,
    base::Closure start_arc_instance_closure,
    KeyGenerator* key_gen,
    ServerBackedStateKeyGenerator* state_key_generator,
    ProcessManagerServiceInterface* manager,
    LoginMetrics* metrics,
    NssUtil* nss,
    SystemUtils* utils,
    Crossystem* crossystem,
    VpdProcess* vpd_process,
    PolicyKey* owner_key,
    ContainerManagerInterface* android_container)
    : session_started_(false),
      session_stopping_(false),
      screen_locked_(false),
      supervised_user_creation_ongoing_(false),
      init_controller_(std::move(init_controller)),
      lock_screen_closure_(lock_screen_closure),
      restart_device_closure_(restart_device_closure),
      start_arc_instance_closure_(start_arc_instance_closure),
      dbus_emitter_(dbus_emitter),
      key_gen_(key_gen),
      state_key_generator_(state_key_generator),
      manager_(manager),
      login_metrics_(metrics),
      nss_(nss),
      system_(utils),
      crossystem_(crossystem),
      vpd_process_(vpd_process),
      owner_key_(owner_key),
      android_container_(android_container),
      mitigator_(key_gen),
      weak_ptr_factory_(this) {}

SessionManagerImpl::~SessionManagerImpl() {
  STLDeleteValues(&user_sessions_);
  device_policy_->set_delegate(NULL);  // Could use WeakPtr instead?
}

void SessionManagerImpl::InjectPolicyServices(
    std::unique_ptr<DevicePolicyService> device_policy,
    std::unique_ptr<UserPolicyServiceFactory> user_policy_factory,
    std::unique_ptr<DeviceLocalAccountPolicyService>
        device_local_account_policy) {
  device_policy_ = std::move(device_policy);
  user_policy_factory_ = std::move(user_policy_factory);
  device_local_account_policy_ = std::move(device_local_account_policy);
}

void SessionManagerImpl::AnnounceSessionStoppingIfNeeded() {
  if (session_started_) {
    session_stopping_ = true;
    DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStopping;
    dbus_emitter_->EmitSignalWithString(kSessionStateChangedSignal, kStopping);
  }
}

void SessionManagerImpl::AnnounceSessionStopped() {
  session_stopping_ = session_started_ = false;
  DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStopped;
  dbus_emitter_->EmitSignalWithString(kSessionStateChangedSignal, kStopped);
}

bool SessionManagerImpl::ShouldEndSession() {
  return screen_locked_ || supervised_user_creation_ongoing_;
}

bool SessionManagerImpl::Initialize() {
  key_gen_->set_delegate(this);

  device_policy_.reset(DevicePolicyService::Create(login_metrics_, owner_key_,
                                                   &mitigator_, nss_,
                                                   crossystem_, vpd_process_));
  device_policy_->set_delegate(this);

  user_policy_factory_.reset(
      new UserPolicyServiceFactory(getuid(), nss_, system_));
  device_local_account_policy_.reset(new DeviceLocalAccountPolicyService(
      base::FilePath(kDeviceLocalAccountStateDir), owner_key_));

  if (device_policy_->Initialize()) {
    device_local_account_policy_->UpdateDeviceSettings(
        device_policy_->GetSettings());
    if (device_policy_->MayUpdateSystemSettings()) {
      device_policy_->UpdateSystemSettings(PolicyService::Completion());
    }
    return true;
  }
  return false;
}

void SessionManagerImpl::Finalize() {
  device_policy_->PersistPolicySync();
  for (UserSessionMap::const_iterator it = user_sessions_.begin();
       it != user_sessions_.end(); ++it) {
    if (it->second)
      it->second->policy_service->PersistPolicySync();
  }
  // We want to stop any running containers.  Containers are per-session and
  // cannot persist across sessions.
  android_container_->RequestJobExit();
  android_container_->EnsureJobExit(
      base::TimeDelta::FromSeconds(kContainerTimeoutSec));
}

void SessionManagerImpl::EmitLoginPromptVisible(Error* error) {
  login_metrics_->RecordStats("login-prompt-visible");
  dbus_emitter_->EmitSignal(kLoginPromptVisibleSignal);
  scoped_ptr<dbus::Response> response = init_controller_->TriggerImpulse(
      "login-prompt-visible", std::vector<std::string>());
  if (!response) {
    static const char msg[] =
        "Emitting login-prompt-visible upstart signal failed.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kEmitFailed, msg);
  }
}

std::string SessionManagerImpl::EnableChromeTesting(
    bool relaunch,
    std::vector<std::string> extra_args,
    Error* error) {
  // Check to see if we already have Chrome testing enabled.
  bool already_enabled = !chrome_testing_path_.empty();

  if (!already_enabled) {
    base::FilePath temp_file_path;  // So we don't clobber chrome_testing_path_;
    if (!system_->GetUniqueFilenameInWriteOnlyTempDir(&temp_file_path)) {
      error->Set(dbus_error::kTestingChannelError,
                 "Could not create testing channel filename.");
      return std::string();
    }
    chrome_testing_path_ = temp_file_path;
  }

  if (!already_enabled || relaunch) {
    // Delete testing channel file if it already exists.
    system_->RemoveFile(chrome_testing_path_);

    // Add testing channel argument to extra arguments.
    std::string testing_argument = kTestingChannelFlag;
    testing_argument.append(chrome_testing_path_.value());
    extra_args.push_back(testing_argument);

    manager_->RestartBrowserWithArgs(extra_args, true);
  }
  return chrome_testing_path_.value();
}

bool SessionManagerImpl::StartSession(const std::string& user_id,
                                      const std::string& unique_id,
                                      Error* error) {
  std::string actual_user_id;
  if (!NormalizeUserId(user_id, &actual_user_id, error)) {
    return false;
  }

  // Check if this user already started a session.
  if (user_sessions_.count(actual_user_id) > 0) {
    static const char msg[] = "Provided user id already started a session.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSessionExists, msg);
    return false;
  }

  // Create a UserSession object for this user.
  const bool is_incognito = IsIncognitoUserId(actual_user_id);
  std::string error_name;
  std::unique_ptr<UserSession> user_session(
      CreateUserSession(actual_user_id, is_incognito, &error_name));
  if (!user_session.get()) {
    error->Set(error_name, "Can't create session.");
    return false;
  }
  // Check whether the current user is the owner, and if so make sure she is
  // whitelisted and has an owner key.
  bool user_is_owner = false;
  PolicyService::Error policy_error;
  if (!device_policy_->CheckAndHandleOwnerLogin(
          user_session->username, user_session->slot.get(), &user_is_owner,
          &policy_error)) {
    error->Set(policy_error.code(), policy_error.message());
    return false;
  }

  // If all previous sessions were incognito (or no previous sessions exist).
  bool is_first_real_user = AllSessionsAreIncognito() && !is_incognito;

  // Send each user login event to UMA (right before we start session
  // since the metrics library does not log events in guest mode).
  int dev_mode = system_->IsDevMode();
  if (dev_mode > -1)
    login_metrics_->SendLoginUserType(dev_mode, is_incognito, user_is_owner);

  scoped_ptr<dbus::Response> response = init_controller_->TriggerImpulse(
      "start-user-session",
      std::vector<std::string>(1, "CHROMEOS_USER=" + actual_user_id));

  if (!response) {
    static const char msg[] =
        "Emitting start-user-session upstart signal failed.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kEmitFailed, msg);
    return false;
  }
  LOG(INFO) << "Starting user session";
  manager_->SetBrowserSessionForUser(actual_user_id, user_session->userhash);
  session_started_ = true;
  user_sessions_[actual_user_id] = user_session.release();
  DLOG(INFO) << "emitting D-Bus signal SessionStateChanged:" << kStarted;
  dbus_emitter_->EmitSignalWithString(kSessionStateChangedSignal, kStarted);

  if (device_policy_->KeyMissing() && !device_policy_->Mitigating() &&
      is_first_real_user) {
    // This is the first sign-in on this unmanaged device.  Take ownership.
    key_gen_->Start(actual_user_id);
  }

  // Record that a login has successfully completed on this boot.
  system_->AtomicFileWrite(base::FilePath(kLoggedInFlag), "1");
  return true;
}

bool SessionManagerImpl::StopSession() {
  LOG(INFO) << "Stopping all sessions";
  // Most calls to StopSession() will log the reason for the call.
  // If you don't see a log message saying the reason for the call, it is
  // likely a DBUS message. See session_manager_dbus_adaptor.cc for that call.
  manager_->ScheduleShutdown();
  // TODO(cmasone): re-enable these when we try to enable logout without exiting
  //                the session manager
  // browser_.job->StopSession();
  // user_policy_.reset();
  // session_started_ = false;
  return true;
}

void SessionManagerImpl::StorePolicy(
    const uint8_t* policy_blob,
    size_t policy_blob_len,
    const PolicyService::Completion& completion) {
  int flags = PolicyService::KEY_ROTATE;
  if (!session_started_)
    flags |= PolicyService::KEY_INSTALL_NEW | PolicyService::KEY_CLOBBER;
  device_policy_->Store(policy_blob, policy_blob_len, completion, flags);
}

void SessionManagerImpl::RetrievePolicy(std::vector<uint8_t>* policy_data,
                                        Error* error) {
  if (!device_policy_->Retrieve(policy_data)) {
    static const char msg[] = "Failed to retrieve policy data.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSigEncodeFail, msg);
    return;
  }
}

void SessionManagerImpl::StorePolicyForUser(
    const std::string& user_id,
    const uint8_t* policy_blob,
    size_t policy_blob_len,
    const PolicyService::Completion& completion) {
  PolicyService* policy_service = GetPolicyService(user_id);
  if (!policy_service) {
    PolicyService::Error error(
        dbus_error::kSessionDoesNotExist,
        "Cannot store user policy before session is started.");
    LOG(ERROR) << error.message();
    completion.Run(error);
    return;
  }

  policy_service->Store(
      policy_blob, policy_blob_len, completion,
      PolicyService::KEY_INSTALL_NEW | PolicyService::KEY_ROTATE);
}

void SessionManagerImpl::RetrievePolicyForUser(
    const std::string& user_id,
    std::vector<uint8_t>* policy_data,
    Error* error) {
  PolicyService* policy_service = GetPolicyService(user_id);
  if (!policy_service) {
    static const char msg[] =
        "Cannot retrieve user policy before session is started.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSessionDoesNotExist, msg);
    return;
  }
  if (!policy_service->Retrieve(policy_data)) {
    static const char msg[] = "Failed to retrieve policy data.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSigEncodeFail, msg);
  }
}

void SessionManagerImpl::StoreDeviceLocalAccountPolicy(
    const std::string& account_id,
    const uint8_t* policy_blob,
    size_t policy_blob_len,
    const PolicyService::Completion& completion) {
  device_local_account_policy_->Store(account_id, policy_blob, policy_blob_len,
                                      completion);
}

void SessionManagerImpl::RetrieveDeviceLocalAccountPolicy(
    const std::string& account_id,
    std::vector<uint8_t>* policy_data,
    Error* error) {
  if (!device_local_account_policy_->Retrieve(account_id, policy_data)) {
    static const char msg[] = "Failed to retrieve policy data.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSigEncodeFail, msg);
    return;
  }
}

const char* SessionManagerImpl::RetrieveSessionState() {
  if (!session_started_)
    return kStopped;
  else
    return (session_stopping_ ? kStopping : kStarted);
}

void SessionManagerImpl::RetrieveActiveSessions(
    std::map<std::string, std::string>* active_sessions) {
  for (UserSessionMap::const_iterator it = user_sessions_.begin();
       it != user_sessions_.end(); ++it) {
    if (it->second) {
      (*active_sessions)[it->second->username] = it->second->userhash;
    }
  }
}

void SessionManagerImpl::HandleSupervisedUserCreationStarting() {
  supervised_user_creation_ongoing_ = true;
}

void SessionManagerImpl::HandleSupervisedUserCreationFinished() {
  supervised_user_creation_ongoing_ = false;
}

void SessionManagerImpl::LockScreen(Error* error) {
  if (!session_started_) {
    static const char msg[] = "Attempt to lock screen outside of user session.";
    LOG(WARNING) << msg;
    error->Set(dbus_error::kSessionDoesNotExist, msg);
    return;
  }
  // If all sessions are incognito, then locking is not allowed.
  if (AllSessionsAreIncognito()) {
    static const char msg[] = "Attempt to lock screen during Guest session.";
    LOG(WARNING) << msg;
    error->Set(dbus_error::kSessionExists, msg);
    return;
  }
  if (!screen_locked_) {
    screen_locked_ = true;
    lock_screen_closure_.Run();
  }
  LOG(INFO) << "LockScreen() method called.";
}

void SessionManagerImpl::HandleLockScreenShown() {
  LOG(INFO) << "HandleLockScreenShown() method called.";
  dbus_emitter_->EmitSignal(kScreenIsLockedSignal);
}

void SessionManagerImpl::HandleLockScreenDismissed() {
  screen_locked_ = false;
  LOG(INFO) << "HandleLockScreenDismissed() method called.";
  dbus_emitter_->EmitSignal(kScreenIsUnlockedSignal);
}

bool SessionManagerImpl::RestartJob(int fd,
                                    const std::vector<std::string>& argv,
                                    Error* error) {
  struct ucred ucred = {0};
  socklen_t len = sizeof(struct ucred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len) == -1) {
    PLOG(ERROR) << "Can't get peer creds";
    error->Set("GetPeerCredsFailed", strerror(errno));
    return false;
  }

  if (!manager_->IsBrowser(ucred.pid)) {
    static const char msg[] = "Provided pid is unknown.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kUnknownPid, msg);
    return false;
  }

  // To set "logged-in" state for BWSI mode.
  if (!StartSession(kGuestUserName, "", error))
    return false;
  manager_->RestartBrowserWithArgs(argv, false);
  return true;
}

void SessionManagerImpl::StartDeviceWipe(const std::string& reason,
                                         Error* error) {
  const base::FilePath session_path(kLoggedInFlag);
  if (system_->Exists(session_path)) {
    static const char msg[] = "A user has already logged in this boot.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSessionExists, msg);
    return;
  }
  InitiateDeviceWipe(reason);
}

void SessionManagerImpl::SetFlagsForUser(
    const std::string& user_id,
    const std::vector<std::string>& session_user_flags) {
  manager_->SetFlagsForUser(user_id, session_user_flags);
}

void SessionManagerImpl::RequestServerBackedStateKeys(
    const ServerBackedStateKeyGenerator::StateKeyCallback& callback) {
  state_key_generator_->RequestStateKeys(callback);
}

void SessionManagerImpl::InitMachineInfo(const std::string& data,
                                         Error* error) {
  std::map<std::string, std::string> params;
  if (!ServerBackedStateKeyGenerator::ParseMachineInfo(data, &params))
    error->Set(dbus_error::kInitMachineInfoFail, "Parse failure.");

  if (!state_key_generator_->InitMachineInfo(params))
    error->Set(dbus_error::kInitMachineInfoFail, "Missing parameters.");
}

bool SessionManagerImpl::CheckArcAvailability() {
#if USE_ARC
  return true;
#else
  return false;
#endif  // USE_ARC
}

void SessionManagerImpl::StartArcInstance(const std::string& user_id,
                                          Error* error) {
#if USE_ARC
  arc_start_time_ = base::TimeTicks::Now();

  std::string actual_user_id;
  if (!NormalizeUserId(user_id, &actual_user_id, error)) {
    return;
  }
  if (user_sessions_.count(actual_user_id) == 0) {
    static const char msg[] = "Provided user ID does not have a session.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kSessionDoesNotExist, msg);
    return;
  }

  const base::FilePath android_data_dir =
      GetAndroidDataDirForUser(actual_user_id);
  const std::vector<std::string>& keyvals = {
      base::StringPrintf("ANDROID_DATA_DIR=%s",
                         android_data_dir.value().c_str()),
  };

  if (!init_controller_->TriggerImpulse(kArcStartSignal, keyvals)) {
    static const char msg[] = "Emitting start-arc-instance signal failed.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kEmitFailed, msg);
    return;
  }

  bool started_container = false;
  const char* dbus_error = nullptr;
  std::string error_message;
  if (!StartArcInstanceInternal(&started_container, &dbus_error,
                                &error_message)) {
    LOG(ERROR) << error_message;
    error->Set(dbus_error, error_message);
    init_controller_->TriggerImpulse(kArcStopSignal,
                                     std::vector<std::string>());
    if (started_container) {
      android_container_->RequestJobExit();
      android_container_->EnsureJobExit(
          base::TimeDelta::FromSeconds(kContainerTimeoutSec));
    }
    return;
  }
  start_arc_instance_closure_.Run();
#else
  error->Set(dbus_error::kNotAvailable, "ARC not supported.");
#endif  // !USE_ARC
}

void SessionManagerImpl::StopArcInstance(Error* error) {
#if USE_ARC
  pid_t pid;
  if (!android_container_->GetContainerPID(&pid)) {
    static const char msg[] = "Error getting Android container pid.";
    LOG(ERROR) << msg;
    error->Set(dbus_error::kContainerShutdownFail, msg);
    return;
  }

  android_container_->RequestJobExit();
  android_container_->EnsureJobExit(
      base::TimeDelta::FromSeconds(kContainerTimeoutSec));
#else
  error->Set(dbus_error::kNotAvailable, "ARC not supported.");
#endif  // USE_ARC
}

base::TimeTicks SessionManagerImpl::GetArcStartTime(Error* error) {
#if USE_ARC
  if (arc_start_time_.is_null())
    error->Set(dbus_error::kNotStarted, "ARC is not started yet.");
#else
  error->Set(dbus_error::kNotAvailable, "ARC not supported.");
#endif  // !USE_ARC
  return arc_start_time_;
}

void SessionManagerImpl::StartContainer(const std::string& name, Error* error) {
  // TODO(dgreid): Add support for other containers.
  const char msg[] = "Container not found.";
  LOG(ERROR) << msg;
  error->Set(dbus_error::kContainerStartupFail, msg);
}

void SessionManagerImpl::StopContainer(const std::string& name, Error* error) {
  // TODO(dgreid): Add support for other containers.
  const char msg[] = "Container not found.";
  LOG(ERROR) << msg;
  error->Set(dbus_error::kContainerShutdownFail, msg);
}

void SessionManagerImpl::RemoveArcData(const std::string& user_id,
                                       Error* error) {
#if USE_ARC
  pid_t pid = 0;
  if (android_container_->GetContainerPID(&pid)) {
    error->Set(dbus_error::kArcInstanceRunning, "ARC is currently running.");
    return;
  }

  std::string actual_user_id;
  if (!NormalizeUserId(user_id, &actual_user_id, error)) {
    return;
  }
  const base::FilePath android_data_dir =
      GetAndroidDataDirForUser(actual_user_id);
  system_->RemoveDirTree(android_data_dir);
#else
  error->Set(dbus_error::kNotAvailable, "ARC not supported.");
#endif  // USE_ARC
}

void SessionManagerImpl::OnPolicyPersisted(bool success) {
  device_local_account_policy_->UpdateDeviceSettings(
      device_policy_->GetSettings());
  dbus_emitter_->EmitSignalWithSuccessFailure(kPropertyChangeCompleteSignal,
                                              success);
}

void SessionManagerImpl::OnKeyPersisted(bool success) {
  dbus_emitter_->EmitSignalWithSuccessFailure(kOwnerKeySetSignal, success);
}

void SessionManagerImpl::OnKeyGenerated(const std::string& username,
                                        const base::FilePath& temp_key_file) {
  ImportValidateAndStoreGeneratedKey(username, temp_key_file);
}

void SessionManagerImpl::ImportValidateAndStoreGeneratedKey(
    const std::string& username,
    const base::FilePath& temp_key_file) {
  DLOG(INFO) << "Processing generated key at " << temp_key_file.value();
  std::string key;
  base::ReadFileToString(temp_key_file, &key);
  PLOG_IF(WARNING, !base::DeleteFile(temp_key_file, false))
      << "Can't delete " << temp_key_file.value();
  device_policy_->ValidateAndStoreOwnerKey(
      username, key, user_sessions_[username]->slot.get());
}

void SessionManagerImpl::InitiateDeviceWipe(const std::string& reason) {
  // The log string must not be confused with other clobbers-state parameters.
  // Sanitize by replacing all non-alphanumeric characters with underscores and
  // clamping size to 50 characters.
  std::string sanitized_reason(reason.substr(0, 50));
  std::locale locale("C");
  std::replace_if(sanitized_reason.begin(), sanitized_reason.end(),
                  [&locale](const std::string::value_type character) {
                    return !std::isalnum(character, locale);
                  },
                  '_');
  const base::FilePath reset_path(kResetFile);
  system_->AtomicFileWrite(reset_path,
                           "fast safe keepimg reason=" + sanitized_reason);
  restart_device_closure_.Run();
}

// static
bool SessionManagerImpl::NormalizeUserId(const std::string& user_id,
                                         std::string* actual_user_id_out,
                                         Error* error_out) {
  // Validate the |user_id|.
  if (IsIncognitoUserId(user_id) || ValidateUserId(user_id)) {
    *actual_user_id_out = user_id;
    return true;
  }

  // Support legacy email addresses.
  // TODO(alemate): remove this after ChromeOS will stop using email as
  // cryptohome identifier.
  const std::string& lower_user_id = base::ToLowerASCII(user_id);
  if (!ValidateEmail(lower_user_id)) {
    static const char msg[] =
        "Provided email address is not valid.  ASCII only.";
    LOG(ERROR) << msg;
    error_out->Set(dbus_error::kInvalidAccount, msg);
    return false;
  }
  *actual_user_id_out = lower_user_id;
  return true;
}

// static
bool SessionManagerImpl::ValidateUserId(const std::string& user_id) {
  if (user_id.find_first_not_of(kUserIdLegalCharacters) != std::string::npos)
    return false;

  return base::StartsWith(user_id, kUserIdPrefix, base::CompareCase::SENSITIVE);
}

// static
bool SessionManagerImpl::ValidateEmail(const std::string& email_address) {
  if (email_address.find_first_not_of(kEmailLegalCharacters) !=
      std::string::npos) {
    return false;
  }

  size_t at = email_address.find(kEmailSeparator);
  // it has NO @.
  if (at == std::string::npos)
    return false;

  // it has more than one @.
  if (email_address.find(kEmailSeparator, at + 1) != std::string::npos)
    return false;

  return true;
}

bool SessionManagerImpl::AllSessionsAreIncognito() {
  size_t incognito_count = 0;
  for (UserSessionMap::const_iterator it = user_sessions_.begin();
       it != user_sessions_.end(); ++it) {
    if (it->second)
      incognito_count += it->second->is_incognito;
  }
  return incognito_count == user_sessions_.size();
}

SessionManagerImpl::UserSession* SessionManagerImpl::CreateUserSession(
    const std::string& username,
    bool is_incognito,
    std::string* error_message) {
  std::unique_ptr<PolicyService> user_policy(
      user_policy_factory_->Create(username));
  if (!user_policy) {
    LOG(ERROR) << "User policy failed to initialize.";
    if (error_message)
      *error_message = dbus_error::kPolicyInitFail;
    return NULL;
  }
  crypto::ScopedPK11Slot slot(nss_->OpenUserDB(GetUserPath(username)));
  if (!slot) {
    LOG(ERROR) << "Could not open the current user's NSS database.";
    if (error_message)
      *error_message = dbus_error::kNoUserNssDb;
    return NULL;
  }
  return new SessionManagerImpl::UserSession(
      username, SanitizeUserName(username), is_incognito, std::move(slot),
      std::move(user_policy));
}

PolicyService* SessionManagerImpl::GetPolicyService(const std::string& user) {
  UserSessionMap::const_iterator it = user_sessions_.find(user);
  return it == user_sessions_.end() ? NULL : it->second->policy_service.get();
}

bool SessionManagerImpl::StartArcInstanceInternal(
    bool* started_container_out,
    const char** dbus_error_out,
    std::string* error_message_out) {
  *started_container_out = false;
  *dbus_error_out = nullptr;
  error_message_out->clear();
  if (!android_container_->StartContainer(
          base::Bind(&SessionManagerImpl::OnAndroidContainerStopped,
                     weak_ptr_factory_.GetWeakPtr()))) {
    *dbus_error_out = dbus_error::kContainerStartupFail;
    *error_message_out = "Starting Android container failed.";
    return false;
  }
  *started_container_out = true;

  login_metrics_->StartTrackingArcUseTime();

  base::FilePath root_path;
  pid_t pid = 0;
  if (!android_container_->GetRootFsPath(&root_path) ||
      !android_container_->GetContainerPID(&pid)) {
    *dbus_error_out = dbus_error::kContainerStartupFail;
    *error_message_out = "Getting Android container info failed.";
    return false;
  }

  // Also tell init to configure the network.
  std::vector<std::string> env;
  env.emplace_back("CONTAINER_NAME=" + std::string(kArcContainerName));
  env.emplace_back("CONTAINER_PATH=" + root_path.value());
  env.emplace_back("CONTAINER_PID=" + std::to_string(pid));
  if (!init_controller_->TriggerImpulse(kArcNetworkStartSignal, env)) {
    *dbus_error_out = dbus_error::kEmitFailed;
    *error_message_out = "Emitting start-arc-network signal failed.";
    return false;
  }
  LOG(INFO) << "Started Android container pid " << pid;

  return true;
}

void SessionManagerImpl::OnAndroidContainerStopped(pid_t pid, bool clean) {
  if (clean) {
    LOG(INFO) << "Android Container with pid " << pid << " stopped";
  } else {
    LOG(ERROR) << "Android Container with pid " << pid << " crashed";
  }

  login_metrics_->StopTrackingArcUseTime();

  std::vector<std::string> env;
  env.emplace_back("ANDROID_PID=" + std::to_string(pid));
  if (!init_controller_->TriggerImpulse(kArcStopSignal, env)) {
    static const char msg[] = "Emitting stop-arc-instance init signal failed.";
    LOG(ERROR) << msg;
  }

  if (!init_controller_->TriggerImpulse(kArcNetworkStopSignal,
                                        std::vector<std::string>())) {
    static const char msg[] = "Emitting stop-arc-network init signal failed.";
    LOG(ERROR) << msg;
  }

  dbus_emitter_->EmitSignalWithBool(kArcInstanceStopped, clean);
}

}  // namespace login_manager
