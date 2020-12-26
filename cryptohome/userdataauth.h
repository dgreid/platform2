// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USERDATAAUTH_H_
#define CRYPTOHOME_USERDATAAUTH_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/location.h>
#include <base/threading/thread.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <dbus/bus.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/bootlockbox/boot_lockbox.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/arc_disk_quota.h"
#include "cryptohome/storage/disk_cleanup.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"
#include "cryptohome/storage/mount_factory.h"
#include "cryptohome/user_session.h"
#include "cryptohome/UserDataAuth.pb.h"

namespace cryptohome {

class UserDataAuth {
 public:
  // TestThreadId used to indicate the thread type.
  enum class TestThreadId {
    kOriginThread,
    kMountThread,
  };

  UserDataAuth();
  ~UserDataAuth();

  // Note that this function must be called from the thread that created this
  // object, so that |origin_task_runner_| is initialized correctly.
  bool Initialize();

  // This is the initialization function that is called after DBus is connected
  // and set_dbus() has been called. Usually Initialize() is called before this
  // method.
  bool PostDBusInitialize();

  // =============== Mount Related Public DBus API ===============
  // Methods below are used directly by the DBus interface

  // If username is empty, returns true if any mount is mounted, otherwise,
  // returns true if the mount associated with the given |username| is mounted.
  // For |is_ephemeral_out|, if no username is given, then is_ephemeral_out is
  // set to true when any mount is ephemeral. Otherwise, is_ephemeral_out is set
  // to true when the mount associated with the given |username| is mounted in
  // an ephemeral manner. If nullptr is passed in for is_ephemeral_out, then it
  // won't be touched. Ephemeral mount means that the content of the mount is
  // cleared once the user logs out.
  bool IsMounted(const std::string& username = "",
                 bool* is_ephemeral_out = nullptr);

  // Returns true if the mount that corresponds to the username is mounted,
  // false otherwise. If that mount is ephemeral, then is_ephemeral_out is set
  // to true, false otherwise. If nullptr is passed in for is_ephemeral_out,
  // then it won't be touched. Ephemeral mount means that the content of the
  // mount is cleared once the user logs out.
  bool IsMountedForUser(const std::string& username,
                        bool* is_ephemeral_out = nullptr);

  // Calling this function will unmount all mounted cryptohomes. It'll return
  // true if all mounts are cleanly unmounted.
  // Note: This must only be called on mount thread
  bool Unmount();

  // This function will attempt to mount the requested user's home directory, as
  // specified in |request|. Once that's done, it'll call |on_done| to notify
  // the result. Note that there's no guarantee on whether |on_done| is called
  // before or after this function returns.
  void DoMount(
      user_data_auth::MountRequest request,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Calling this method will kick start the migration to Dircrypto format (from
  // eCryptfs). |request| contains the account whose cryptohome to migrate, and
  // what whether minimal migration is to be performed. See definition of
  // message StartMigrateToDircryptoRequest for more information on minimal
  // migration. |progress_callback| is a callback that will be called whenever
  // there's progress update from the migration, or if the migration
  // completes/fails.
  void StartMigrateToDircrypto(
      const user_data_auth::StartMigrateToDircryptoRequest& request,
      base::Callback<void(const user_data_auth::DircryptoMigrationProgress&)>
          progress_callback);

  // Determine if the account specified by |account| needs to do Dircrypto
  // migration. Returns CRYPTOHOME_ERROR_NOT_SET if the query is successful, and
  // the result is stored in |result| (true for migration needed). Otherwise, an
  // error code is returned and result is in an undefined state.
  user_data_auth::CryptohomeErrorCode NeedsDircryptoMigration(
      const cryptohome::AccountIdentifier& account, bool* result);

  // Return the size of the user's home directory in number of bytes. If the
  // |account| given is non-existent, then 0 is returned.
  // Negative values are reserved for future cases whereby we need to do some
  // form of error reporting.
  int64_t GetAccountDiskUsage(const cryptohome::AccountIdentifier& account);

  // =============== Mount Related Public Utilities ===============

  // Called during initialization (and on mount events) to ensure old mounts
  // are marked for unmount when possible by the kernel.  Returns true if any
  // mounts were stale and not cleaned up (because of open files).
  // Note: This must only be called on mount thread
  //
  // Parameters
  // - force: if true, unmounts all existing shadow mounts.
  //          if false, unmounts shadows mounts with no open files.
  bool CleanUpStaleMounts(bool force);

  // Calling this function will reset the TPM context of every mount, that is,
  // it'll force a reload of all cryptohome key that is associated with mounts.
  void ResetAllTPMContext();

  // Set the |force_ecryptfs_| variable, if true, all mounts will use eCryptfs
  // for encryption. If eCryptfs is not used, then dircrypto (the ext4
  // directory encryption mechanism) is used. Note that this is usually used in
  // main() because there's a command line switch for selecting dircrypto or
  // eCryptfs.
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  // Set the |legacy_mount_| variable. For more information on legacy_mount_,
  // see comment of Mount::MountLegacyHome(). Note that this is usually used in
  // main() because there's a command line switch for selecting this.
  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }

  // Set |bind_mount_downloads_|. The variable is passed to Mount to define
  // whether the Downloads/ directory shall be bind mounted.
  void set_bind_mount_downloads(bool bind) { bind_mount_downloads_ = bind; }

  // Set thresholds for automatic disk cleanup.
  void set_cleanup_threshold(uint64_t cleanup_threshold);
  void set_aggressive_cleanup_threshold(uint64_t aggressive_cleanup_threshold);
  void set_target_free_space(uint64_t target_free_space);

  // Set the |low_disk_space_callback_| variable. This is usually called by the
  // DBus adaptor.
  void SetLowDiskSpaceCallback(const base::Callback<void(uint64_t)>& callback) {
    low_disk_space_callback_ = callback;
  }

  // =============== Key Related Public Utilities ===============
  // Add the key specified in the request, and return a CryptohomeErrorCode to
  // indicate the status of adding the key. If CryptohomeErrorCode is
  // CRYPTOHOME_ERROR_NOT_SET, then the key is successfully added.
  user_data_auth::CryptohomeErrorCode AddKey(
      const user_data_auth::AddKeyRequest request);

  // Add a data restore key, and save the key in |key_out|. If
  // CRYPTOHOME_ERROR_NOT_SET is returned, then the key is added and |key_out|
  // is valid.
  user_data_auth::CryptohomeErrorCode AddDataRestoreKey(
      const user_data_auth::AddDataRestoreKeyRequest request,
      brillo::SecureBlob* key_out);

  // Check the key given in |request| again the currently mounted directories
  // and other credentials. |on_done| is called once the operation is completed,
  // and the error code is CRYPTOHOME_ERROR_NOT_SET if the key is found. Note
  // that this method is asynchronous.
  void CheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);

  // Remove the key given in |request.key| with the authorization given in
  // |request.authorization_request|. Returns CRYPTOHOME_ERROR_NOT_SET if the
  // key is successfully removed, returns other Error Code if not.
  user_data_auth::CryptohomeErrorCode RemoveKey(
      const user_data_auth::RemoveKeyRequest request);

  // Remove all keys under the account specified by |request.account_id| except
  // those listed in |request.exempt_key_data| (only the label).
  // |request.authorization_request| contains the authorization to authorize
  // this change. Returns CRYPTOHOME_ERROR_NOT_SET if the operation is
  // successful.
  user_data_auth::CryptohomeErrorCode MassRemoveKeys(
      const user_data_auth::MassRemoveKeysRequest request);

  // List the keys stored in |homedirs_|. If CRYPTOHOME_ERROR_NOT_SET is
  // returned, then |labels_out| contains the label of the keys. Otherwise, the
  // content of |labels_out| is undefined.
  user_data_auth::CryptohomeErrorCode ListKeys(
      const user_data_auth::ListKeysRequest& request,
      std::vector<std::string>* labels_out);

  // Get the KeyData associated with key that have the label specified in
  // |request.key.data.label|. If there's an error processing this request, then
  // an error code will be returned, and |found|/|data_out| will be untouched.
  // If there's no key with the lable found, then no error will be returned, and
  // |found| will be set to false. Otherwise, the key's keydata will be written
  // to |data_out| and |found| will be set to true. Note that KeyData is
  // actually the key's metadata.
  user_data_auth::CryptohomeErrorCode GetKeyData(
      const user_data_auth::GetKeyDataRequest& request,
      cryptohome::KeyData* data_out,
      bool* found);

  // This will migrate, or rather say, change the underlying secret that is used
  // to protect the user's home directory. The home directory key to change is
  // specified by |request.account_id|, the change is authorized by
  // |request.authorization_request| and the new secret is specified by
  // |request.secret|. This function will return CRYPTOHOME_ERROR_NOT_SET if the
  // operation is successful, and other error code if it failed.
  user_data_auth::CryptohomeErrorCode MigrateKey(
      const user_data_auth::MigrateKeyRequest& request);

  // Remove the cryptohome (user's home directory) specified in
  // |request.identifier|. If removed successfully, then return
  // CRYPTOHOME_ERROR_NOT_SET, otherwise, some error code is returned.
  user_data_auth::CryptohomeErrorCode Remove(
      const user_data_auth::RemoveRequest& request);

  // Rename the cryptohome (user's home directory) specified by
  // |request.id_from| to |request.id_to|. If renamed successfully, then return
  // CRYPTOHOME_ERROR_NOT_SET, otherwise, some error code is returned.
  user_data_auth::CryptohomeErrorCode Rename(
      const user_data_auth::RenameRequest& request);

  // Return true if we support low entropy credential.
  bool IsLowEntropyCredentialSupported();

  // =============== ARC Quota Related Public Methods ===============

  // Return true is ARC Disk Quota is supported, false otherwise.
  bool IsArcQuotaSupported();

  // Return the current disk usage for an android uid (a shifted uid) in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcUid(uid_t android_uid);

  // Return the current disk usage for an android gid (a shifted gid) in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcGid(uid_t android_gid);

  // Return the current disk usage for an android project id in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcProjectId(int project_id);

  // Sets the project ID to the file/directory pointed by path.
  // See cryptohome/arc_disk_quota.h for more details.
  bool SetProjectId(int project_id,
                    user_data_auth::SetProjectIdAllowedPathType parent_path,
                    const FilePath& child_path,
                    const cryptohome::AccountIdentifier& account);

  // =============== PKCS#11 Related Public Methods ===============

  // This initializes the PKCS#11 for a particular mount. Note that this is
  // used mostly internally, by Mount related functions to bring up the PKCS#11
  // functionalities after mounting.
  void InitializePkcs11(UserSession* mount);

  // Returns true if and only if PKCS#11 tokens are ready for all mounts.
  bool Pkcs11IsTpmTokenReady();

  // Return the information regarding a token. If username is empty, then system
  // token's information is given. Otherwise, the corresponding user token
  // information is given. Note that this function doesn't check if the given
  // username is valid or not. If a non-existent user is given, then the result
  // is undefined.
  // Note that if this method fails to get the slot associated with the token,
  // then -1 will be supplied for slot.
  user_data_auth::TpmTokenInfo Pkcs11GetTpmTokenInfo(
      const std::string& username);

  // Calling this method will remove PKCS#11 tokens on all mounts.
  // Note that this should only be called from mount thread.
  void Pkcs11Terminate();

  // =============== Install Attributes Related Public Methods ===============

  // Retrieve the key value pair in install attributes with the key of |name|,
  // and return its value in |data_out|. Returns true if and only if the key
  // value pair is successfully retrieved. If false is returned, then
  // |data_out|'s content is undefined.
  bool InstallAttributesGet(const std::string& name,
                            std::vector<uint8_t>* data_out);

  // Insert the key value pair (name, data) into install attributes. Return true
  // if and only if the key value pair is successfully inserted.
  bool InstallAttributesSet(const std::string& name,
                            const std::vector<uint8_t>& data);

  // Finalize the install attributes. Return true if and only if the install
  // attributes is finalized.
  bool InstallAttributesFinalize();

  // Get the number of key value pair stored in install attributes.
  int InstallAttributesCount();

  // Return true if and only if the attribute storage is securely stored, that
  // is, if the system TPM/Lockbox is being used.
  bool InstallAttributesIsSecure();

  // Return the current status of the install attributes.
  InstallAttributes::Status InstallAttributesGetStatus();

  // Convert the InstallAttributes::Status enum to
  // user_data_auth::InstallAttributesState protobuf enum.
  static user_data_auth::InstallAttributesState
  InstallAttributesStatusToProtoEnum(InstallAttributes::Status status);

  // =============== Install Attributes Related Utilities ===============

  // Return true if this device is enterprise owned.
  bool IsEnterpriseOwned() { return enterprise_owned_; }

  // ============= Fingerprint Auth Related Public Methods ==============

  // Start fingerprint auth session asynchronously for the user specified in
  // |request|, and call |on_done|.
  void StartFingerprintAuthSession(
      const user_data_auth::StartFingerprintAuthSessionRequest& request,
      base::OnceCallback<void(
          const user_data_auth::StartFingerprintAuthSessionReply&)> on_done);

  // End the current fingerprint auth session.
  void EndFingerprintAuthSession();

  user_data_auth::GetWebAuthnSecretReply GetWebAuthnSecret(
      const user_data_auth::GetWebAuthnSecretRequest& request);

  // ========= Firmware Management Parameters Related Public Methods =========

  // Retrieve the firmware management parameters. Returns
  // CRYPTOHOME_ERROR_NOT_SET if successful, and in that case, |fwmp| will be
  // filled with the firmware management parameters. Otherwise, an error code is
  // returned and |fwmp|'s content is undefined.
  user_data_auth::CryptohomeErrorCode GetFirmwareManagementParameters(
      user_data_auth::FirmwareManagementParameters* fwmp);

  // Set the firmware management parameters to the value given in |fwmp|.
  // Returns CRYPTOHOME_ERROR_NOT_SET if the operation is successful, and other
  // error code if it failed.
  user_data_auth::CryptohomeErrorCode SetFirmwareManagementParameters(
      const user_data_auth::FirmwareManagementParameters& fwmp);

  // Remove the firmware management parameters, that is, undefine its NVRAM
  // space (if defined). Return true if and only if the firmware management
  // parameters are gone
  bool RemoveFirmwareManagementParameters();

  // =============== Miscellaneous Public APIs ===============

  // Retrieve the current system salt. This method call is always successful.
  // Note that this should never be called before Initialize() is successful,
  // otherwise an assertion will fail.
  const brillo::SecureBlob& GetSystemSalt();

  // Update the current user activity timestamp for all mounts. time_shift_sec
  // is the time, expressed in number of seconds since the last user activity.
  // For instance, if the unix timestamp now is x, if this value is 5, then the
  // last user activity happened at x-5 unix timestamp.
  // This method will return true if the update is successful for all mounts.
  // Note that negative |time_shift_sec| values are reserved and should not be
  // used.
  bool UpdateCurrentUserActivityTimestamp(int time_shift_sec);

  // Calling this method will prevent another user from logging in later by
  // extending PCR, causing PCR-bound VKKs to be inaccessible. This is used by
  // ARC++. |account_id| contains the user that we'll lock to before reboot.
  user_data_auth::CryptohomeErrorCode LockToSingleUserMountUntilReboot(
      const cryptohome::AccountIdentifier& account_id);

  // Retrieve the RSU Device ID, return true if and only if |rsu_device_id| is
  // set to the RSU Device ID.
  bool GetRsuDeviceId(std::string* rsu_device_id);

  // Return true iff powerwash is required. i.e. cannot unseal with user auth.
  bool RequiresPowerwash();

  // Returns true if and only if the loaded device policy specifies an owner
  // user.
  bool OwnerUserExists();

  // Get a JSON string that represents various state of this service.
  // Note: This can only be called on mount thread.
  std::string GetStatusString();

  // =============== Miscellaneous ===============

  // This is called by tpm_init_ when there's any update on ownership status
  // of the TPM.
  void OwnershipCallback(bool status, bool took_ownership);

  // Set the current dbus connection, this is usually used by the dbus daemon
  // object that owns the instance of this object. During testing, the test
  // fixture might call this for dependency injection.
  void set_dbus(scoped_refptr<::dbus::Bus> bus) { bus_ = bus; }

  // Set the current dbus connection for mount thread, this is usually used by
  // the dbus daemon object that owns the instance of this object. During
  // testing, the test fixture might call this for dependency injection.
  // Note that it is the responsibility of the caller to cleanup/destroy the Bus
  // object during destruction.
  void set_mount_thread_dbus(scoped_refptr<::dbus::Bus> bus) {
    mount_thread_bus_ = bus;
  }

  // ================= Threading Utilities ==================

  // Returns true if we are currently running on the origin thread
  bool IsOnOriginThread() {
    // Note that this function should not rely on |origin_task_runner_| because
    // it may be unavailable when this function is first called by
    // UserDataAuth::Initialize()
    if (disable_threading_) {
      return true;
    }
    if (!mount_thread_ && mount_task_runner_) {
      return current_thread_id_for_test_ == TestThreadId::kOriginThread;
    }
    return base::PlatformThread::CurrentId() == origin_thread_id_;
  }

  // Returns true if we are currently running on the mount thread
  bool IsOnMountThread() {
    if (disable_threading_) {
      return true;
    }
    if (!mount_thread_) {
      return current_thread_id_for_test_ == TestThreadId::kMountThread;
    }
    // GetThreadId blocks if the thread is not started yet.
    return mount_thread_->IsRunning() &&
           base::PlatformThread::CurrentId() == mount_thread_->GetThreadId();
  }

  // DCHECK if we are running on the origin thread. Will have no effect
  // in production.
  void AssertOnOriginThread() { DCHECK(IsOnOriginThread()); }

  // DCHECK if we are running on the mount thread. Will have no effect
  // in production.
  void AssertOnMountThread() { DCHECK(IsOnMountThread()); }

  // Post Task to origin thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToOriginThread(const base::Location& from_here,
                              base::OnceClosure task,
                              const base::TimeDelta& delay = base::TimeDelta());

  // Post Task to mount thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToMountThread(const base::Location& from_here,
                             base::OnceClosure task,
                             const base::TimeDelta& delay = base::TimeDelta());

  // ================= Testing Utilities ==================
  // Note that all functions below in this section should only be used for unit
  // testing purpose only.

  // Override |crypto_| for testing purpose
  void set_crypto(cryptohome::Crypto* crypto) { crypto_ = crypto; }

  // Override |keyset_management_| for testing purpose
  void set_keyset_management(KeysetManagement* value) {
    keyset_management_ = value;
  }

  // Override |homedirs_| for testing purpose
  void set_homedirs(cryptohome::HomeDirs* homedirs) { homedirs_ = homedirs; }

  // Override |tpm_| for testing purpose
  void set_tpm(Tpm* tpm) { tpm_ = tpm; }

  // Override |tpm_init_| for testing purpose
  void set_tpm_init(TpmInit* tpm_init) { tpm_init_ = tpm_init; }

  // Override |platform_| for testing purpose
  void set_platform(cryptohome::Platform* platform) { platform_ = platform; }

  // override |chaps_client_| for testing purpose
  void set_chaps_client(chaps::TokenManagerClient* chaps_client) {
    chaps_client_ = chaps_client;
  }

  // Override |install_attrs_| for testing purpose
  void set_install_attrs(InstallAttributes* install_attrs) {
    install_attrs_ = install_attrs;
  }

  // Override |arc_disk_quota_| for testing purpose
  void set_arc_disk_quota(cryptohome::ArcDiskQuota* arc_disk_quota) {
    arc_disk_quota_ = arc_disk_quota;
  }

  // Override |disk_cleanup_| for testing purpose
  void set_disk_cleanup(DiskCleanup* disk_cleanup) {
    disk_cleanup_ = disk_cleanup;
  }

  // Override |pkcs11_init_| for testing purpose
  void set_pkcs11_init(Pkcs11Init* pkcs11_init) { pkcs11_init_ = pkcs11_init; }

  // Override |firmware_management_parameters_| for testing purpose
  void set_firmware_management_parameters(FirmwareManagementParameters* fwmp) {
    firmware_management_parameters_ = fwmp;
  }

  // Override |fingerprint_manager_| for testing purpose
  void set_fingerprint_manager(FingerprintManager* fingerprint_manager) {
    fingerprint_manager_ = fingerprint_manager;
  }

  // Override |mount_factory_| for testing purpose
  void set_mount_factory(MountFactory* mount_factory) {
    mount_factory_ = mount_factory;
  }

  // Override |challenge_credentials_helper_| for testing purpose
  void set_challenge_credentials_helper(
      ChallengeCredentialsHelper* challenge_credentials_helper) {
    challenge_credentials_helper_ = challenge_credentials_helper;
  }

  // Override |key_challenge_service_factory_| for testing purpose
  void set_key_challenge_service_factory(
      KeyChallengeServiceFactory* key_challenge_service_factory) {
    key_challenge_service_factory_ = key_challenge_service_factory;
  }

  // Override |origin_task_runner_| for testing purpose
  void set_origin_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner) {
    origin_task_runner_ = origin_task_runner;
  }

  // Override |mount_task_runner_| for testing purpose
  void set_mount_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner) {
    mount_task_runner_ = mount_task_runner;
  }

  // Override |current_thread_id_for_test_| for testing purpose
  void set_current_thread_id_for_test(TestThreadId current_thread_id_for_test) {
    current_thread_id_for_test_ = current_thread_id_for_test;
  }

  // Retrieve the current thread id, for testing purpose only.
  TestThreadId get_current_thread_id_for_test() {
    return current_thread_id_for_test_;
  }

  // Override |boot_lockbox_| for testing purpose
  void set_boot_lockbox(BootLockbox* boot_lockbox) {
    boot_lockbox_ = boot_lockbox;
  }

  // Retrieve the session associated with the given user, for testing purpose
  // only.
  UserSession* get_session_for_user(const std::string& username) {
    if (sessions_.count(username) == 0)
      return nullptr;
    return sessions_[username].get();
  }

  // Associate a particular session object |session| with the username
  // |username| for testing purpose
  void set_session_for_user(const std::string& username, UserSession* session) {
    sessions_[username] = session;
  }

  // Override the time between each LowDiskCallback() for testing. This is so
  // that we can finish the unit test in a shorter time. And we shouldn't call
  // it on origin thread when mount thread is started.
  void set_low_disk_notification_period_ms(int value) {
    low_disk_notification_period_ms_ = value;
  }

  // Override the time between each run of UploadAlertsDataCallback() for
  // testing. This is so that we can finish the unit test in a shorter time.
  void set_upload_alerts_period_ms(int value) {
    upload_alerts_period_ms_ = value;
  }

  // Set |disable_threading_|, so that we can disable threading in this class
  // for testing purpose. See comment of |disable_threading_| for more
  // information.
  void set_disable_threading(bool disable_threading) {
    disable_threading_ = disable_threading;
  }

  bool StartAuthSession(
      user_data_auth::StartAuthSessionRequest request,
      base::OnceCallback<void(const user_data_auth::StartAuthSessionReply&)>
          on_done);

 private:
  // Note: In Service class (the class that this class is refactored from),
  // there is a initialize_tpm_ member variable, but it is almost unused and
  // always set to true there, so in this class, if we are migrating any code
  // from Service class and initialize_tpm_ is used there, then we'll just
  // assume it's true and not have a initialize_tpm_ variable here.

  // =============== Mount Related Utilities ===============
  // Returns the UserSession object associated with the given username
  scoped_refptr<UserSession> GetUserSession(const std::string& username);

  // Filters out active mounts from |mounts|, populating |active_mounts| set.
  // If |include_busy_mount| is false, then stale mounts with open files and
  // mount points connected to children of the mount source will be treated as
  // active mount, and be moved from |mounts| to |active_mounts|. Otherwise, all
  // stale mounts are included in |mounts|. Returns true if |include_busy_mount|
  // is true and there's at least one stale mount with open file(s) and treated
  // as active mount during the process.
  bool FilterActiveMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts,
      std::multimap<const base::FilePath, const base::FilePath>* active_mounts,
      bool include_busy_mount);

  // Populates |mounts| with ephemeral cryptohome mount points.
  void GetEphemeralLoopDevicesMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Unload any user pkcs11 tokens _not_ belonging to one of the mounts in
  // |exclude|. This is used to clean up any stale loaded tokens after a
  // cryptohome crash.
  // Note that system tokens are not affected.
  bool UnloadPkcs11Tokens(const std::vector<base::FilePath>& exclude);

  // Safely empties the MountMap and may request unmounting. If |unmount| is
  // true, the return value will reflect if all mounts unmounted cleanly or not.
  // That is, it'll be true if all unmounts are clean and successful.
  // Note: This must only be called on mount thread
  bool RemoveAllMounts(bool unmount);

  // Returns true if any of the path in |prefixes| starts with |path|
  // Note that this function is case insensitive
  static bool PrefixPresent(const std::vector<base::FilePath>& prefixes,
                            const std::string path);

  // Calling this function will try to ensure that |public_mount_salt_| is ready
  // to use. If it's not ready, we'll generate it. Returns true if
  // |public_mount_salt_| is ready.
  bool CreatePublicMountSaltIfNeeded();

  // Gets passkey for |public_mount_id|. Returns true if a passkey is generated
  // successfully. Otherwise, returns false.
  bool GetPublicMountPassKey(const std::string& public_mount_id,
                             std::string* public_mount_passkey);

  // Determines whether the mount request should be ephemeral. On error, returns
  // false and sets the error code in |error|. Otherwise, returns true and fills
  // the result in |is_ephemeral|.
  bool GetShouldMountAsEphemeral(
      const std::string& account_id,
      bool is_ephemeral_mount_requested,
      bool has_create_request,
      bool* is_ephemeral,
      user_data_auth::CryptohomeErrorCode* error) const;

  // Returns either and existing or a newly created UserSession, if not present.
  scoped_refptr<UserSession> GetOrCreateUserSession(
      const std::string& username);

  // Called during mount requests to ensure old hidden mounts are unmounted.
  // Note that this only cleans up |mounts_| entries which were mounted with the
  // hidden_mount=true parameter, as these are supposed to be temporary. Old
  // mounts from another cryptohomed run (e.g. after a crash) are cleaned up in
  // CleanUpStaleMounts().
  bool CleanUpHiddenMounts();

  // Builds the PCR restrictions to be applied to the challenge-protected vault
  // keyset.
  void GetChallengeCredentialsPcrRestrictions(
      const std::string& obfuscated_username,
      std::vector<std::map<uint32_t, brillo::Blob>>* pcr_restrictions);

  // Safely removes the reference to the UserSession from. This method returns
  // true if as a result of the operation there is no reference to a session of
  // the given user (including if it was absent in the first place).
  bool RemoveUserSession(const std::string& username);

  // Calling this method will mount the home directory for guest users.
  // This is usually called by DoMount(). Note that this method is asynchronous,
  // and will call |on_done| exactly once to deliver the result regardless of
  // whether the operation is successful.
  void MountGuest(
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Performs the lazy part of the initialization that is required for
  // performing operations with challenge-response keys. Returns whether
  // succeeded.
  bool InitForChallengeResponseAuth(
      user_data_auth::CryptohomeErrorCode* error_code);

  // This is a utility function used by DoMount(). It is called if the request
  // mounting operation requires challenge response authentication. i.e. The key
  // for the storage is sealed.
  void DoChallengeResponseMount(
      const user_data_auth::MountRequest& request,
      const Mount::MountArgs& mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // This is a utility function used by DoChallengeResponseMount(), and is
  // called once we're done doing challenge response authentication.
  void OnChallengeResponseMountCredentialsObtained(
      const user_data_auth::MountRequest& request,
      const Mount::MountArgs mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done,
      std::unique_ptr<Credentials> credentials);

  // This is a utility function used by DoMount(). It is called either by
  // DoMount() (when using password for authentication.), or by
  // OnChallengeResponseMountCredentialsObtained() (when using challenge
  // response authentication.).
  void ContinueMountWithCredentials(
      const user_data_auth::MountRequest& request,
      std::unique_ptr<Credentials> credentials,
      const Mount::MountArgs& mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Called on Mount thread. This triggers the credentials verification steps
  // that are specific to challenge-response keys, going through
  // {TryLightweightChallengeResponseCheckKeyEx(),
  // OnLightweightChallengeResponseCheckKeyExDone()} and/or
  // {DoFullChallengeResponseCheckKeyEx(),
  // OnFullChallengeResponseCheckKeyExDone()}.
  void DoChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void TryLightweightChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void OnLightweightChallengeResponseCheckKeyDone(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
      bool is_key_valid);
  void DoFullChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void OnFullChallengeResponseCheckKeyDone(
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
      std::unique_ptr<Credentials> credentials);

  // ================ Fingerprint Auth Related Methods ==================

  // Called on Mount thread. This creates a dbus proxy for Biometrics Daemon
  // and connects to signals.
  void CreateFingerprintManager();

  // Called on Mount thread when fingerprint auth session starts or fails to
  // start.
  void OnFingerprintStartAuthSessionResp(
      base::OnceCallback<void(
          const user_data_auth::StartFingerprintAuthSessionReply&)> on_done,
      bool success);

  // Called on Mount thread. Scheduled by CheckKey(). Callback for one
  // fingerprint scan. Completes fingerprint CheckKey by running |on_done|.
  void CompleteFingerprintCheckKey(
      base::OnceCallback<void(const user_data_auth::CryptohomeErrorCode)>
          on_done,
      FingerprintScanStatus status);

  // =============== Periodic Maintenance Related Methods ===============

  // Called periodically on Mount thread to detect low disk space and emit a
  // signal if detected. This also does various cleanup task.
  void LowDiskCallback();

  // This is called periodically, and does some routine cleanups. Currently this
  // free disk space consumed by unused cryptohomes and reset the dictionary
  // attack mitigation.
  void DoAutoCleanup();

  // Does what its name suggests. Usually called by DoAutoCleanup().
  void ResetDictionaryAttackMitigation();

  // This method uploads the TPM Alerts data UMA.
  void UploadAlertsDataCallback();

  // This method takes entropy from the TPM and seeds it to /dev/urandom.
  void SeedUrandom();

  // =============== PKCS#11 Related Utilities ===============

  // This is called when TPM is enabled and owned, so that we can continue
  // the initialization of any PKCS#11 that was paused because TPM wasn't
  // ready.
  void ResumeAllPkcs11Initialization();

  // =============== Install Attributes Related Utilities ===============

  // Set whether this device is enterprise owned. Calling this method will have
  // effect on all currently mounted mounts. This can only be called on
  // mount_thread_.
  void SetEnterpriseOwned(bool enterprise_owned);

  // Detect whether this device is enterprise owned, and call
  // SetEnterpriseOwned(). This can only be called on origin thread.
  void DetectEnterpriseOwnership();

  // Call this method to initialize the install attributes functionality. This
  // can only be called on origin thread.
  void InitializeInstallAttributes();

  // Calling this method will finalize the install attributes if we current have
  // a non-guest mount mounted. This can only be called on mount thread.
  void FinalizeInstallAttributesIfMounted();

  // =============== Signal Related Utilities/Callback ===============

  // This is called whenever the signal handlers for signals on tpm_manager's
  // interface is registered/connected. This is only used by the signal handler
  // registration function in tpm_manager's generated D-Bus proxy.
  void OnTpmManagerSignalConnected(const std::string& interface,
                                   const std::string& signal,
                                   bool success);

  // This is called whenever the OwnershipTaken signal is emitted by
  // tpm_manager. This will notify |tpm_| about the emitted signal.
  void OnOwnershipTakenSignal();

  // =============== Stateful Recovery related Helpers ===============

  // This is a utility function for stateful recovery functionality to call when
  // it wants to mount a user's home directory. The user specified by
  // |username|'s home is mounted with |passkey|, and if successfully mounted,
  // the function returns true and set |out_home_path| to the mounted home.
  // Otherwise, return false. Note that this function must log any error itself,
  // no logging will be done by the caller.
  bool StatefulRecoveryMount(const std::string& username,
                             const std::string& passkey,
                             FilePath* out_home_path);

  // This is a utility function for stateful recovery to unmount all user's home
  // directories. It'll return true if all the user's home directories are
  // successfully unmounted. Otherwise, it'll return false. Note that this
  // function must log any error itself, no logging will be done by the caller.
  bool StatefulRecoveryUnmount();

  // This is a utility function for stateful recovery to check if a user is the
  // owner. It'll return true if the user specified by |username| is the owner,
  // and false otherwise.
  bool StatefulRecoveryIsOwner(const std::string& username);

  // Creates and initialized MountObject for user
  scoped_refptr<Mount> CreateMount(const std::string& username);

  // Ensures BootLockbox is finalized;
  void EnsureBootLockboxFinalized();

  // =============== Auth Session Related Helpers ===============

  void RemoveAuthSessionWithToken(const base::UnguessableToken& token);

  // =============== Threading Related Variables ===============

  // The task runner that belongs to the thread that created this UserDataAuth
  // object. Currently, this is required to be the same as the dbus thread's
  // task runner.
  scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner_;

  // The thread ID of the thread that created this UserDataAuth object.
  // Currently, this is required to be th esame as the dbus thread's task
  // runner.
  base::PlatformThreadId origin_thread_id_;

  // The thread for performing long running, or mount related operations
  std::unique_ptr<base::Thread> mount_thread_;

  // The task runner that belongs to the mount thread.
  scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner_;

  // This variable is used only for unit testing purpose. If set to true, it'll
  // disable the the threading mechanism in this class so that testing doesn't
  // fail. When threading is disabled, posting to origin or mount thread will
  // execute immediately, and all checks for whether we are on mount or origin
  // thread will result in true.
  bool disable_threading_;

  // This variable is used only for unit testing purpose. We could use this to
  // know current task is running on origin thread or mount thread.
  TestThreadId current_thread_id_for_test_ = TestThreadId::kOriginThread;

  // =============== Basic Utilities Related Variables ===============
  // The system salt that is used for obfuscating the username
  brillo::SecureBlob system_salt_;

  // The object for accessing the TPM
  // Note that TPM doesn't use the unique_ptr for default pattern, since the tpm
  // is a singleton - we don't want it getting destroyed when we are.
  Tpm* tpm_;

  // The default TPM init object.
  std::unique_ptr<TpmInit> default_tpm_init_;

  // The TPM init object. Note that |tpm_init_| and |default_tpm_init_| will be
  // removed at the end of the refactoring that's happening in cryptohome
  // (b/123679223).
  TpmInit* tpm_init_;

  // The default platform object for accessing platform related functionalities
  std::unique_ptr<cryptohome::Platform> default_platform_;

  // The actual platform object used by this class, usually set to
  // default_platform_, but can be overridden for testing
  cryptohome::Platform* platform_;

  // The default crypto object for performing cryptographic operations
  std::unique_ptr<cryptohome::Crypto> default_crypto_;

  // The actual crypto object used by this class, usually set to
  // default_crypto_, but can be overridden for testing
  cryptohome::Crypto* crypto_;

  // The default token manager client for accessing chapsd's PKCS#11 interface
  std::unique_ptr<chaps::TokenManagerClient> default_chaps_client_;

  // The actual token manager client used by this class, usually set to
  // default_chaps_client_, but can be overridden for testing.
  chaps::TokenManagerClient* chaps_client_;

  // A dbus connection, this is used by any code in this class that needs access
  // to the system DBus and accesses it on the origin thread.
  scoped_refptr<::dbus::Bus> bus_;

  // A dbus connection, this is used by any code in this class that needs access
  // to the system DBus and accesses it on the mount thread. Such as when
  // creating an instance of KeyChallengeService.
  scoped_refptr<::dbus::Bus> mount_thread_bus_;

  // The default PKCS#11 init object that is used to supply some PKCS#11 related
  // information.
  std::unique_ptr<Pkcs11Init> default_pkcs11_init_;

  // The actual PKCS#11 init object that is used by this class, but can be
  // overridden for testing.
  Pkcs11Init* pkcs11_init_;

  // The default Firmware Management Parameters object for accessing any
  // Firmware Management Parameters related functionalities.
  std::unique_ptr<FirmwareManagementParameters>
      default_firmware_management_params_;

  // The actual Firmware Management Parameters object that is used by this
  // class, but can be overridden for testing.
  FirmwareManagementParameters* firmware_management_parameters_;

  // The default Fingerprint Manager object for fingerprint authentication.
  std::unique_ptr<FingerprintManager> default_fingerprint_manager_;

  // The actual Fingerprint Manager object that is used by this class, but
  // can be overridden for testing.
  FingerprintManager* fingerprint_manager_;

  // The default BootLockbox object for finalizing it.
  std::unique_ptr<BootLockbox> default_boot_lockbox_;

  // The actual BootLockbox object for finalizing it, but can be overridden
  // for testing.
  BootLockbox* boot_lockbox_;

  // The amount of time in between each run of UploadAlertsDataCallback()
  int upload_alerts_period_ms_;

  // This is set to true iff OwnershipCallback has run.
  bool ownership_callback_has_run_;

  // =============== Install Attributes Related Variables ===============

  // The default install attributes object, for accessing install attributes
  // related functionality.
  std::unique_ptr<cryptohome::InstallAttributes> default_install_attrs_;

  // The actual install attributes object used by this class, usually set to
  // |default_install_attrs_|, but can be overridden for testing. This object
  // should only be accessed on the origin thread.
  cryptohome::InstallAttributes* install_attrs_;

  // Whether this device is an enterprise owned device. Write access should only
  // happen on mount thread.
  bool enterprise_owned_;

  // =============== Mount Related Variables ===============

  // Defines a type for tracking Mount objects for each user by username.
  typedef std::map<const std::string, scoped_refptr<UserSession>>
      UserSessionMap;

  // Records the UserSession objects associated with each username.
  // This and its content should only be accessed from the mount thread.
  // TODO(b/126022424): Verify that this access paradigm doesn't cause
  // measurable performance impact.
  UserSessionMap sessions_;

  // Note: In Service class (the class that this class is refactored from),
  // there is a mounts_lock_ lock for inserting/removal of mounts_ map. However,
  // in this class, all accesses to mounts_ should happen on the mount thread,
  // so no lock is needed.

  // This is an unused variable that's lifted over from service.cc. It is kept
  // here for the purpose of keeping the code in userdatauth.h/.cc as close as
  // possible to the version in service.cc.
  bool reported_pkcs11_init_fail_;

  // The keyset_management_ object in normal operation
  std::unique_ptr<KeysetManagement> default_keyset_management_;
  // This holds the object that records information about the
  // keyset_management. This is usually set to default_keyset_management_, but
  // can be overridden for testing. This is to be accessed from the mount thread
  // only because there's no guarantee on thread safety of the HomeDirs object.
  KeysetManagement* keyset_management_;

  // The homedirs_ object in normal operation
  std::unique_ptr<HomeDirs> default_homedirs_;

  // This holds the object that records informations about the homedirs.
  // This is usually set to default_homedirs_, but can be overridden for
  // testing.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  HomeDirs* homedirs_;

  // This holds a timestamp for each user that is the time that the user was
  // active.
  std::unique_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;

  // The default mount factory instance that is used for creating Mount objects.
  std::unique_ptr<cryptohome::MountFactory> default_mount_factory_;

  // The mount factory instance that is actually used by this class to create
  // Mount object. This is usually |default_mount_factory_|, but can be
  // overridden for testing.
  cryptohome::MountFactory* mount_factory_;

  // This holds the salt that is used to derive the passkey for public mounts.
  brillo::SecureBlob public_mount_salt_;

  // Default challenge credential helper utility object. This object is required
  // for doing a challenge response style login, and is only lazily created when
  // mounting a mount that requires challenge response login type is performed.
  std::unique_ptr<ChallengeCredentialsHelper>
      default_challenge_credentials_helper_;

  // Actual challenge credential helper utility object used by this class.
  // Usually set to |default_challenge_credentials_helper_|, but can be
  // overridden for testing.
  ChallengeCredentialsHelper* challenge_credentials_helper_ = nullptr;

  // Default factory of key challenge services. This object is required for
  // doing a challenge response style login.
  KeyChallengeServiceFactoryImpl default_key_challenge_service_factory_;

  // Actual factory of key challenge services that is used by this class.
  // Usually set to |default_key_challenge_service_factory_|, but can be
  // overridden for testing.
  KeyChallengeServiceFactory* key_challenge_service_factory_ =
      &default_key_challenge_service_factory_;

  // Guest user's username.
  std::string guest_user_;

  // Force the use of eCryptfs. If eCryptfs is not used, then dircrypto (the
  // ext4 directory encryption) is used.
  bool force_ecryptfs_;

  // Whether we are using legacy mount. See Mount::MountLegacyHome()'s comment
  // for more information.
  bool legacy_mount_;

  // Whether Downloads/ should be bind mounted.
  bool bind_mount_downloads_;

  // The default ARC Disk Quota object. This is used to provide Quota related
  // information function for ARC.
  std::unique_ptr<ArcDiskQuota> default_arc_disk_quota_;

  // The actual ARC Disk Quota object used by this class. Usually set to
  // default_arc_disk_quota_, but can be overridden for testing.
  ArcDiskQuota* arc_disk_quota_;

  // =============== Low Disk Space/Cleanup Related Variables ===============

  // The default disk cleanup service.
  std::unique_ptr<DiskCleanup> default_disk_cleanup_;

  // The actual disk cleaunp service. Usually set to default_disk_cleanup, but
  // can be overridden for testing.
  DiskCleanup* disk_cleanup_;

  // TODO(dlunev): This three variables are a hack to pass cleanup parameters
  // from main to the actual object. The reason it is done like this is that
  // the object is created in UserDataAuth::Initialize, which is called from the
  // deamonization function, but they are attempted to be set from the main,
  // before the daemonization. Once service.cc is gone, we shall refactor the
  // whole initialization process of UserDataAuth to avoid such hacks.
  uint64_t disk_cleanup_threshold_;
  uint64_t disk_cleanup_aggressive_threshold_;
  uint64_t disk_cleanup_target_free_space_;

  // The amount of time (in milliseconds) between each subsequent run of
  // LowDiskCallback(). This is usually set to a constant value that is
  // reasonably long, but will be overriden during testing.
  int low_disk_notification_period_ms_;

  // Records whether low_disk_space_callback_ was called (i.e. the signal was
  // emitted by the DBus adaptor) when LowDiskCallback() last run.
  bool low_disk_space_signal_was_emitted_;

  // The last time when DoAutoCleanup() is called by LowDiskCallback().
  base::Time last_auto_cleanup_time_;

  // The last time when LowDiskCallback() called
  // UpdateCurrentUserActivityTimestamp().
  base::Time last_user_activity_timestamp_time_;

  // The callback to call when we are running low on disk space. This is usually
  // connected to the DBus signal emitter, so calling this will emit the DBus
  // signal to notify various other services that we are low on disk space.
  base::Callback<void(uint64_t)> low_disk_space_callback_;

  // Defines a type for tracking Auth Sessions by token.
  typedef std::map<const base::UnguessableToken, std::unique_ptr<AuthSession>>
      AuthSessionMap;

  AuthSessionMap auth_sessions_;

  friend class UserDataAuthExTest;
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USERDATAAUTH_H_
