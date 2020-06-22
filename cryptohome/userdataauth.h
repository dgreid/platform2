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
#include <brillo/secure_blob.h>
#include <dbus/bus.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/arc_disk_quota.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/mount.h"
#include "cryptohome/mount_factory.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"

#include "UserDataAuth.pb.h"
// Note that cryptohome generates its own copy of UserDataAuth.pb.h, so we
// shouldn't include from the system's version.

namespace cryptohome {

class UserDataAuth {
 public:
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
  // for encryption. If eCryptfs is not used, then dircrypto -- the native ext4
  // directory encryption mechanism is used. Note that this is usually used in
  // main() because there's a command line switch for selecting dircrypto or
  // eCryptfs.
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  // Set the |legacy_mount_| variable. For more information on legacy_mount_,
  // see comment of Mount::MountLegacyHome(). Note that this is usually used in
  // main() because there's a command line switch for selecting this.
  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }

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

  // This will update the keyset specified by |request.account_id|, with the
  // changes being |request.changes| and authorized by
  // |request.authorization_request| and |request.authorization_signature|.
  // It'll return CRYPTOHOME_ERROR_NOT_SET if the operation is successful, and
  // other error code if it failed.
  user_data_auth::CryptohomeErrorCode UpdateKey(
      const user_data_auth::UpdateKeyRequest& request);

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

  // Return the current disk usage for an android uid (a shifted uid) in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcGid(uid_t android_gid);

  // =============== PKCS#11 Related Public Methods ===============

  // This initializes the PKCS#11 for a particular mount. Note that this is
  // used mostly internally, by Mount related functions to bring up the PKCS#11
  // functionalities after mounting.
  void InitializePkcs11(cryptohome::Mount* mount);

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

  // This is called whenever we try to create a Mount object. This callback is
  // used by |mount_factory_|.
  void PreMountCallback();

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
    return disable_threading_ ||
           base::PlatformThread::CurrentId() == origin_thread_id_;
  }

  // Returns true if we are currently running on the mount thread
  bool IsOnMountThread() {
    // GetThreadId blocks if the thread is not started yet.
    return disable_threading_ ||
           (mount_thread_.IsRunning() &&
            (base::PlatformThread::CurrentId() == mount_thread_.GetThreadId()));
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

  void set_tpm_ownership_proxy(
      org::chromium::TpmOwnershipProxyInterface* tpm_ownership_proxy) {
    tpm_ownership_proxy_ = tpm_ownership_proxy;
  }

  // Override |mount_factory_| for testing purpose
  void set_mount_factory(MountFactory* mount_factory) {
    mount_factory_ = mount_factory;
  }

  // Retrieve the mount associated with a given user, for testing purpose only.
  cryptohome::Mount* get_mount_for_user(const std::string& username) {
    if (mounts_.count(username) == 0)
      return nullptr;
    return mounts_[username].get();
  }

  // Associate a particular mount object |mount| with the username |username|
  // for testing purpose
  void set_mount_for_user(const std::string& username,
                          cryptohome::Mount* mount) {
    mounts_[username] = mount;
  }

  // Override the time between each LowDiskCallback() for testing. This is so
  // that we can finish the unit test in a shorter time.
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

 private:
  // Note: In Service class (the class that this class is refactored from),
  // there is a use_tpm_ member variable, but it is almost unused and always set
  // to true there, so in this class, if we are migrating any code from Service
  // class and use_tpm_ is used there, then we'll just assume it's true and not
  // have a use_tpm_ variable here.
  // The same is true for initialize_tpm_ variable, it is assumed to be true.

  // =============== Mount Related Utilities ===============
  // Returns the mount object associated with the given username
  scoped_refptr<cryptohome::Mount> GetMountForUser(const std::string& username);

  // Filters out active mounts from |mounts|, populating |active_mounts| set.
  // If |include_busy_mount| is false, then stale mounts with open files will be
  // treated as active mount, and be moved from |mounts| to |active_mounts|.
  // Otherwise,  all stale mounts are included in |mounts|. Returns true if
  // |include_busy_mount| is true and there's at least one stale mount with open
  // file(s) and treated as active mount during the process.
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

  // Does what its name suggests. The use of this method is to ensure that only
  // one Mount is ever created per username.
  scoped_refptr<cryptohome::Mount> GetOrCreateMountForUser(
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

  // Safely removes the MountMap reference for the Mount belonging to given
  // user. This method returns true if the mount belonging to the user is
  // successfully removed, including the case when there is no mounts associated
  // with the given username.
  bool RemoveMountForUser(const std::string& username);

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
  void OnOwnershipTakenSignal(const tpm_manager::OwnershipTakenSignal& signal);

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
  base::Thread mount_thread_;

  // This variable is used only for unit testing purpose. If set to true, it'll
  // disable the the threading mechanism in this class so that testing doesn't
  // fail. When threading is disabled, posting to origin or mount thread will
  // execute immediately, and all checks for whether we are on mount or origin
  // thread will result in true.
  bool disable_threading_;

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

  // The default D-Bus proxy for invoking any ownership related methods in
  // tpm_manager.
  std::unique_ptr<org::chromium::TpmOwnershipProxyInterface>
      default_tpm_ownership_proxy_;

  // The actual D-Bus proxy for invoking any ownership related methods in
  // tpm_manager, but can be overridden for testing.
  org::chromium::TpmOwnershipProxyInterface* tpm_ownership_proxy_;

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
  typedef std::map<const std::string, scoped_refptr<cryptohome::Mount>>
      MountMap;

  // Records the Mount objects associated with each username.
  // This and its content should only be accessed from the mount thread.
  // TODO(b/126022424): Verify that this access paradigm doesn't cause
  // measurable performance impact.
  MountMap mounts_;

  // Note: In Service class (the class that this class is refactored from),
  // there is a mounts_lock_ lock for inserting/removal of mounts_ map. However,
  // in this class, all accesses to mounts_ should happen on the mount thread,
  // so no lock is needed.

  // This is an unused variable that's lifted over from service.cc. It is kept
  // here for the purpose of keeping the code in userdatauth.h/.cc as close as
  // possible to the version in service.cc.
  bool reported_pkcs11_init_fail_;

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

  // Challenge credential helper utility class. This class is required for doing
  // a challenge response style login, and is only lazily created when mounting
  // a mount that requires challenge response login type is performed.
  std::unique_ptr<ChallengeCredentialsHelper> challenge_credentials_helper_;

  // Guest user's username.
  std::string guest_user_;

  // Force the use of eCryptfs. If eCryptfs is not used, then dircrypto -- the
  // native ext4 directory encryption is used.
  bool force_ecryptfs_;

  // Whether we are using legacy mount. See Mount::MountLegacyHome()'s comment
  // for more information.
  bool legacy_mount_;

  // The default ARC Disk Quota object. This is used to provide Quota related
  // information function for ARC.
  std::unique_ptr<ArcDiskQuota> default_arc_disk_quota_;

  // The actual ARC Disk Quota object used by this class. Usually set to
  // default_arc_disk_quota_, but can be overridden for testing.
  ArcDiskQuota* arc_disk_quota_;

  // =============== Low Disk Space/Cleanup Related Variables ===============

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
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USERDATAAUTH_H_
