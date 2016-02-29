// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_
#define LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_

#include "login_manager/session_manager_interface.h"

#include <stdint.h>
#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

#include <base/callback.h>

#include "login_manager/device_policy_service.h"
#include "login_manager/key_generator.h"
#include "login_manager/policy_service.h"
#include "login_manager/regen_mitigator.h"
#include "login_manager/server_backed_state_key_generator.h"

class Crossystem;

namespace login_manager {
class DBusSignalEmitterInterface;
class DeviceLocalAccountPolicyService;
class KeyGenerator;
class LoginMetrics;
class NssUtil;
class PolicyKey;
class ProcessManagerServiceInterface;
class SystemUtils;
class UpstartSignalEmitter;
class UserPolicyServiceFactory;
class VpdProcess;

// Friend test classes.
class SessionManagerImplStaticTest;
class SessionManagerImplTest;

// Implements the DBus SessionManagerInterface.
//
// All signatures used in the methods of the ownership API are
// SHA1 with RSA encryption.
class SessionManagerImpl : public SessionManagerInterface,
                           public KeyGenerator::Delegate,
                           public PolicyService::Delegate {
 public:
  // Magic user name strings.
  static const char kDemoUser[];

  // Payloads for SessionStateChanged DBus signal.
  static const char kStarted[];
  static const char kStopping[];
  static const char kStopped[];

  // Path to flag file indicating that a user has logged in since last boot.
  static const char kLoggedInFlag[];

  // Path to magic file that will trigger device wiping on next boot.
  static const char kResetFile[];

  class Error {
   public:
    Error();
    Error(const std::string& name, const std::string& message);
    virtual ~Error();

    void Set(const std::string& name, const std::string& message);
    bool is_set() const { return set_; }
    const std::string& name() const { return name_; }
    const std::string& message() const { return message_; }

   private:
    std::string name_;
    std::string message_;
    bool set_;
  };

  SessionManagerImpl(scoped_ptr<UpstartSignalEmitter> emitter,
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
                     PolicyKey* owner_key);
  virtual ~SessionManagerImpl();

  void InjectPolicyServices(
      scoped_ptr<DevicePolicyService> device_policy,
      scoped_ptr<UserPolicyServiceFactory> user_policy_factory,
      scoped_ptr<DeviceLocalAccountPolicyService> device_local_account_policy);

  // SessionManagerInterface implementation.
  // Should set up policy stuff; if false DIE.
  bool Initialize() override;
  void Finalize() override;

  void AnnounceSessionStoppingIfNeeded() override;
  void AnnounceSessionStopped() override;
  bool ShouldEndSession() override;
  std::vector<std::string> GetStartUpFlags() override {
    return device_policy_->GetStartUpFlags();
  }

  // Starts a 'Powerwash' of the device by touching a flag file, then
  // rebooting to allow early-boot code to wipe parts of stateful we
  // need wiped. Have a look at /src/platform/init/chromeos_startup
  // for the gory details.
  void InitiateDeviceWipe(const std::string& reason) override;

  //////////////////////////////////////////////////////////////////////////////
  // Methods exposed via RPC are defined below.

  void EmitLoginPromptVisible(Error* error);
  std::string EnableChromeTesting(bool force_relaunch,
                                  std::vector<std::string> extra_args,
                                  Error* error);
  bool StartSession(const std::string& user_id,
                    const std::string& unique_id,
                    Error* error);
  bool StopSession();

  void StorePolicy(const uint8_t* policy_blob,
                   size_t policy_blob_len,
                   PolicyService::Completion completion);
  void RetrievePolicy(std::vector<uint8_t>* policy_data, Error* error);

  void StorePolicyForUser(const std::string& user_id,
                          const uint8_t* policy_blob,
                          size_t policy_blob_len,
                          PolicyService::Completion completion);
  void RetrievePolicyForUser(const std::string& user_id,
                             std::vector<uint8_t>* policy_data,
                             Error* error);

  void StoreDeviceLocalAccountPolicy(const std::string& account_id,
                                     const uint8_t* policy_blob,
                                     size_t policy_blob_len,
                                     PolicyService::Completion completion);
  void RetrieveDeviceLocalAccountPolicy(const std::string& account_id,
                                        std::vector<uint8_t>* policy_data,
                                        Error* error);

  const char* RetrieveSessionState();
  void RetrieveActiveSessions(std::map<std::string, std::string>* sessions);

  void HandleSupervisedUserCreationStarting();
  void HandleSupervisedUserCreationFinished();

  void LockScreen(Error* error);
  void HandleLockScreenShown();
  void HandleLockScreenDismissed();

  bool RestartJob(int fd, const std::vector<std::string>& argv, Error* error);
  void StartDeviceWipe(const std::string& reason, Error* error);
  void SetFlagsForUser(const std::string& user_id,
                       const std::vector<std::string>& session_user_flags);

  void RequestServerBackedStateKeys(
      const ServerBackedStateKeyGenerator::StateKeyCallback& callback);
  void InitMachineInfo(const std::string& data, Error* error);

  bool CheckArcAvailability();
  void StartArcInstance(const std::string& socket_path, Error* error);
  void StopArcInstance(Error* error);

  // PolicyService::Delegate implementation:
  void OnPolicyPersisted(bool success) override;
  void OnKeyPersisted(bool success) override;

  // KeyGenerator::Delegate implementation:
  void OnKeyGenerated(const std::string& username,
                              const base::FilePath& temp_key_file) override;

 private:
  // Holds the state related to one of the signed in users.
  struct UserSession;

  friend class SessionManagerImplStaticTest;
  friend class SessionManagerImplTest;

  typedef std::map<std::string, UserSession*> UserSessionMap;

  // Given a policy key stored at temp_key_file, pulls it off disk,
  // validates that it is a correctly formed key pair, and ensures it is
  // stored for the future in the provided user's NSSDB.
  void ImportValidateAndStoreGeneratedKey(const std::string& username,
                                          const base::FilePath& temp_key_file);

  // Checks if string looks like valid cryptohome user id.
  static bool ValidateUserId(const std::string& user_id);

  // Perform very, very basic validation of |email_address|.
  static bool ValidateEmail(const std::string& email_address);

  bool AllSessionsAreIncognito();

  UserSession* CreateUserSession(const std::string& username,
                                 bool is_incognito,
                                 std::string* error);

  PolicyService* GetPolicyService(const std::string& user_id);

  // Updates system settings according to |device_policy_|.
  void UpdateSystemSettings();

  bool session_started_;
  bool session_stopping_;
  bool screen_locked_;
  bool supervised_user_creation_ongoing_;
  std::string cookie_;

  base::FilePath chrome_testing_path_;

  scoped_ptr<UpstartSignalEmitter> upstart_signal_emitter_;

  base::Closure lock_screen_closure_;
  base::Closure restart_device_closure_;
  base::Closure start_arc_instance_closure_;

  DBusSignalEmitterInterface* dbus_emitter_;            // Owned by the caller.
  KeyGenerator* key_gen_;                               // Owned by the caller.
  ServerBackedStateKeyGenerator* state_key_generator_;  // Owned by the caller.
  ProcessManagerServiceInterface* manager_;             // Owned by the caller.
  LoginMetrics* login_metrics_;                         // Owned by the caller.
  NssUtil* nss_;                                        // Owned by the caller.
  SystemUtils* system_;                                 // Owned by the caller.
  Crossystem* crossystem_;                              // Owned by the caller.
  VpdProcess* vpd_process_;                             // Owned by the caller.
  PolicyKey* owner_key_;                                // Owned by the caller.

  scoped_ptr<DevicePolicyService> device_policy_;
  scoped_ptr<UserPolicyServiceFactory> user_policy_factory_;
  scoped_ptr<DeviceLocalAccountPolicyService> device_local_account_policy_;
  RegenMitigator mitigator_;

  // Map of the currently signed-in users to their state.
  UserSessionMap user_sessions_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerImpl);
};

}  // namespace login_manager
#endif  // LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_
