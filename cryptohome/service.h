// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_H_
#define CRYPTOHOME_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/atomic_sequence_num.h>
#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/task/task_observer.h>
#include <base/threading/thread.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/glib/abstract_dbus_service.h>
#include <brillo/glib/dbus.h>
#include <brillo/glib/object.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dbus-glib.h>

#include "cryptohome/arc_disk_quota.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/cryptohome_event_source.h"
#include "cryptohome/dbus_transition.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/mount.h"
#include "cryptohome/mount_factory.h"
#include "cryptohome/mount_task.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/tpm_init.h"

#include "rpc.pb.h"  // NOLINT(build/include_directory)

namespace chaps {
class TokenManagerClient;
}

namespace cryptohome {
namespace gobject {

struct Cryptohome;
}  // namespace gobject

class BootAttributes;
class BootLockbox;
class Credentials;
// Wrapper for all timers used by the cryptohome daemon.
class TimerCollection;

// Bridges between the MountTaskObserver callback model and the
// CryptohomeEventSource callback model. This class forwards MountTaskObserver
// events to a CryptohomeEventSource. An instance of this class is single-use
// (i.e., will be freed after it has observed one event).
class MountTaskObserverBridge : public MountTaskObserver {
 public:
  explicit MountTaskObserverBridge(cryptohome::Mount* mount,
                                   CryptohomeEventSource* source)
    : mount_(mount), source_(source) { }
  virtual ~MountTaskObserverBridge() { }
  virtual bool MountTaskObserve(const MountTaskResult& result) {
    auto r = std::make_unique<MountTaskResult>(result);
    r->set_mount(mount_);
    source_->AddEvent(std::move(r));
    return true;
  }

 protected:
  scoped_refptr<cryptohome::Mount> mount_;
  CryptohomeEventSource* source_;
};

// MountThreadObserver
class MountThreadObserver : public base::TaskObserver {
 public:
  // This method is called when post a task
  void PostTask();

  // This method is called before processing a task.
  void WillProcessTask(const base::PendingTask& pending_task) override;

  // This method is called after processing a task.
  void DidProcessTask(const base::PendingTask& pending_task) override;

  int GetParallelTaskCount() const;

 private:
  std::atomic<int> parallel_task_count_;
};

// Service
// Provides a wrapper for exporting CryptohomeInterface to
// D-Bus and entering the glib run loop.
class Service : public brillo::dbus::AbstractDbusService,
                public CryptohomeEventSourceSink {
 public:
  Service();
  virtual ~Service();

  // Create the right Service based on command-line flags and TPM version.
  static Service* CreateDefault();

  // From brillo::dbus::AbstractDbusService
  // Setup the wrapped GObject and the GMainLoop
  virtual bool Initialize();
  virtual bool SeedUrandom();
  virtual void InitializeInstallAttributes();
  virtual void InitializePkcs11(cryptohome::Mount* mount);
  virtual void DoInitializePkcs11(cryptohome::Mount* mount);
  virtual bool Reset();

  // Used internally during registration to set the
  // proper service information.
  virtual const char *service_name() const {
    return kCryptohomeServiceName;
  }
  virtual const char *service_path() const {
    return kCryptohomeServicePath;
  }
  virtual const char *service_interface() const {
    return kCryptohomeInterface;
  }
  virtual GObject* service_object() const {
    return G_OBJECT(cryptohome_);
  }
  virtual void set_tpm(Tpm* tpm) {
    tpm_ = tpm;
  }
  virtual void set_tpm_init(TpmInit* tpm_init) {
    tpm_init_ = tpm_init;
  }
  virtual void set_initialize_tpm(bool value) {
    initialize_tpm_ = value;
  }
  virtual void set_install_attrs(InstallAttributes* install_attrs) {
    install_attrs_ = install_attrs;
  }
  virtual void set_mount_for_user(const std::string& username,
                                  cryptohome::Mount* m) {
    mounts_[username] = m;
  }
  virtual void set_crypto(Crypto* crypto) {
      crypto_ = crypto;
  }
  virtual void set_mount_factory(cryptohome::MountFactory* mf) {
    mount_factory_ = mf;
  }
  virtual void set_use_tpm(bool value) {
    use_tpm_ = value;
  }

  // Overrides the Platform implementation for Service.
  virtual void set_platform(cryptohome::Platform *platform) {
    platform_ = platform;
  }

  virtual void set_arc_disk_quota(cryptohome::ArcDiskQuota* arc_disk_quota) {
    arc_disk_quota_ = arc_disk_quota;
  }

  virtual cryptohome::Crypto* crypto() { return crypto_; }

  virtual void set_homedirs(cryptohome::HomeDirs* value) { homedirs_ = value; }

  virtual cryptohome::HomeDirs* homedirs() { return homedirs_; }

  virtual void set_chaps_client(chaps::TokenManagerClient* chaps_client) {
    chaps_client_ = chaps_client;
  }

  virtual void set_event_source_sink(CryptohomeEventSourceSink* sink) {
    event_source_sink_ = sink;
  }

  void set_challenge_credentials_helper(
      ChallengeCredentialsHelper* challenge_credentials_helper) {
    challenge_credentials_helper_ = challenge_credentials_helper;
  }

  void set_key_challenge_service_factory(
      KeyChallengeServiceFactory* key_challenge_service_factory) {
    key_challenge_service_factory_ = key_challenge_service_factory;
  }

  // Checks if the given user is the system owner.
  virtual bool IsOwner(const std::string &userid);

  // Returns the base directory of the eCryptfs destination, containing
  // the "user" and "root" directories.
  virtual bool GetMountPointForUser(const std::string& username,
                                    base::FilePath* path);

  // CryptohomeEventSourceSink
  virtual void NotifyEvent(CryptohomeEventBase* event);

  // Performs the work of resetting the TPM context.
  virtual void DoResetTPMContext(Mount* mount);

  // TpmInit::OwnershipCallback
  virtual void OwnershipCallback(bool status, bool took_ownership);

  // Finalize TPM initialization after taking ownership:
  // - initialize & finalize install attributes
  // - send TpmInitStatus event
  // - prepare for enrollment
  // Posted on mount_thread_ by OwnershipCallback.
  virtual void ConfigureOwnedTpm(bool status, bool took_ownership);

  // Called during initialization (and on mount events) to ensure old mounts
  // are marked for unmount when possible by the kernel.  Returns true if any
  // mounts were stale and not cleaned up (because of open files).
  //
  // Parameters
  // - force: if true, unmounts all existing shadow mounts.
  //          if false, unmounts shadows mounts with no open files.
  virtual bool CleanUpStaleMounts(bool force);

  // Called during mount requests to ensure old hidden mounts are unmount.
  // Note that this only cleans up |mounts_| entries which were mounted with
  // the hidden_mount=true parameter, as these are supposed to be temporary.
  // Old mounts from another cryptohomed run (e.g. after a crash) are cleaned up
  // in CleanUpStaleMounts.
  virtual bool CleanUpHiddenMounts();

  // Mount::PreMountCallback. Called before Mount starts to mount cryptohome.
  virtual void PreMountCallback();

  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  void set_cleanup_threshold(uint64_t cleanup_threshold);
  void set_aggressive_cleanup_threshold(uint64_t aggressive_cleanup_threshold);
  void set_target_free_space(uint64_t target_free_space);

  virtual void set_boot_lockbox(BootLockbox* boot_lockbox) {
    boot_lockbox_ = boot_lockbox;
  }

  virtual void set_boot_attributes(BootAttributes* boot_attributes) {
    boot_attributes_ = boot_attributes;
  }

  virtual void set_firmware_management_parameters(
      FirmwareManagementParameters* fwmp) {
    firmware_management_parameters_ = fwmp;
  }

  virtual void set_low_disk_notification_period_ms(int value) {
    low_disk_notification_period_ms_ = value;
  }

  virtual void set_upload_alerts_period_ms(int value) {
    upload_alerts_period_ms_ = value;
  }

  // Service implementation functions as wrapped in interface.cc
  // and defined in cryptohome.xml.
  virtual void DoMigrateKeyEx(AccountIdentifier* account,
                              AuthorizationRequest* auth_request,
                              MigrateKeyRequest* migrate_request,
                              DBusGMethodInvocation* context);
  virtual gboolean MigrateKeyEx(GArray* account,
                                GArray* auth_request,
                                GArray* migrate_request,
                                DBusGMethodInvocation* context);
  virtual void DoAddKeyEx(AccountIdentifier* account_id,
                          AuthorizationRequest*  authorization_request,
                          AddKeyRequest* add_key_request,
                          DBusGMethodInvocation* context);
  virtual gboolean AddKeyEx(GArray* account_id,
                            GArray* authorization_request,
                            GArray* add_key_request,
                            DBusGMethodInvocation* context);
  virtual void DoAddDataRestoreKey(AccountIdentifier* account_id,
                                   AuthorizationRequest*  authorization_request,
                                   DBusGMethodInvocation* context);
  virtual gboolean AddDataRestoreKey(GArray* account_id,
                                     GArray* authorization_request,
                                     DBusGMethodInvocation* context);
  virtual void DoUpdateKeyEx(AccountIdentifier* account_id,
                             AuthorizationRequest*  authorization_request,
                             UpdateKeyRequest* update_key_request,
                             DBusGMethodInvocation* context);
  virtual gboolean UpdateKeyEx(GArray *account_id,
                               GArray *authorization_request,
                               GArray *update_key_request,
                               DBusGMethodInvocation *response);
  virtual void DoCheckKeyEx(
      std::unique_ptr<AccountIdentifier> account_id,
      std::unique_ptr<AuthorizationRequest> authorization_request,
      std::unique_ptr<CheckKeyRequest> check_key_request,
      DBusGMethodInvocation* context);
  virtual gboolean CheckKeyEx(GArray *account_id,
                              GArray *authorization_request,
                              GArray *check_key_request,
                              DBusGMethodInvocation *context);
  virtual void DoRemoveKeyEx(AccountIdentifier* account_id,
                             AuthorizationRequest*  authorization_request,
                             RemoveKeyRequest* remove_key_request,
                             DBusGMethodInvocation* context);
  virtual gboolean RemoveKeyEx(GArray *account_id,
                               GArray *authorization_request,
                               GArray *remove_key_request,
                               DBusGMethodInvocation *context);
  virtual void DoMassRemoveKeys(AccountIdentifier* account_id,
                                AuthorizationRequest* authorization_request,
                                MassRemoveKeysRequest*
                                    mass_remove_keys_request,
                                DBusGMethodInvocation* context);
  virtual gboolean MassRemoveKeys(GArray *account_id,
                                  GArray *authorization_request,
                                  GArray *mass_remove_keys_request,
                                  DBusGMethodInvocation *context);
  virtual void DoGetKeyDataEx(AccountIdentifier* account_id,
                              AuthorizationRequest*  authorization_request,
                              GetKeyDataRequest* get_key_data_request,
                              DBusGMethodInvocation* context);
  virtual gboolean GetKeyDataEx(GArray *account_id,
                                GArray *authorization_request,
                                GArray *get_key_data_request,
                                DBusGMethodInvocation *context);
  virtual void DoListKeysEx(AccountIdentifier* account_id,
                            AuthorizationRequest*  authorization_request,
                            ListKeysRequest* list_keys_request,
                            DBusGMethodInvocation* context);
  virtual gboolean ListKeysEx(GArray *account_id,
                              GArray *authorization_request,
                              GArray *list_keys_request,
                              DBusGMethodInvocation *context);
  virtual void DoRemoveEx(AccountIdentifier* account_id,
                          DBusGMethodInvocation* context);
  virtual gboolean RemoveEx(GArray* account, DBusGMethodInvocation* context);
  virtual gboolean RenameCryptohome(const GArray* account_id_from,
                                    const GArray* account_id_to,
                                    DBusGMethodInvocation* response);
  virtual gboolean GetAccountDiskUsage(const GArray* account_id,
                                       DBusGMethodInvocation* response);
  virtual gboolean GetSystemSalt(GArray **OUT_salt, GError **error);
  virtual gboolean GetSanitizedUsername(gchar *username,
                                        gchar **OUT_sanitized,
                                        GError **error);
  virtual gboolean IsMountedForUser(gchar *user,
                                    gboolean *OUT_is_mounted,
                                    gboolean *OUT_is_ephemeral_mount,
                                    GError **error);
  virtual gboolean IsMounted(gboolean *OUT_is_mounted, GError **error);

  void DoUpdateTimestamp(scoped_refptr<Mount> mount);
  void DoMount(scoped_refptr<cryptohome::Mount> mount,
               const Credentials& credentials,
               const Mount::MountArgs& mount_args,
               base::WaitableEvent* event,
               MountError* return_code,
               bool* return_status);
  virtual gboolean Mount(const gchar *user,
                         const gchar *key,
                         gboolean create_if_missing,
                         gboolean ensure_ephemeral,
                         gint *OUT_error_code,
                         gboolean *OUT_result,
                         GError **error);

  virtual void DoMountEx(std::unique_ptr<AccountIdentifier> identifier,
                         std::unique_ptr<AuthorizationRequest> authorization,
                         std::unique_ptr<MountRequest> request,
                         DBusGMethodInvocation* response);
  virtual void DoRenameCryptohome(AccountIdentifier* id_from,
                                  AccountIdentifier* id_to,
                                  DBusGMethodInvocation* context);
  virtual void DoGetAccountDiskUsage(AccountIdentifier* id,
                                     DBusGMethodInvocation* context);
  virtual gboolean MountEx(
      const GArray *account_id,
      const GArray *authorization_request,
      const GArray *mount_request,
      DBusGMethodInvocation *response);
  virtual void DoMountGuestEx(scoped_refptr<cryptohome::Mount> guest_mount,
                              std::unique_ptr<MountGuestRequest> request_pb,
                              DBusGMethodInvocation* context);
  virtual gboolean MountGuestEx(GArray* request,
                                DBusGMethodInvocation* context);
  virtual gboolean Unmount(gboolean *OUT_result, GError **error);
  virtual void DoUnmountEx(std::unique_ptr<UnmountRequest> request_pb,
                           DBusGMethodInvocation* context);
  virtual gboolean UnmountEx(GArray* request, DBusGMethodInvocation* context);
  virtual gboolean UpdateCurrentUserActivityTimestamp(gint time_shift_sec,
                                                      GError **error);

  virtual gboolean TpmIsReady(gboolean* OUT_ready, GError** error);
  virtual gboolean TpmIsEnabled(gboolean* OUT_enabled, GError** error);
  virtual gboolean TpmGetPassword(gchar** OUT_password, GError** error);
  virtual gboolean TpmIsOwned(gboolean* OUT_owned, GError** error);
  virtual gboolean TpmIsBeingOwned(gboolean* OUT_owning, GError** error);
  virtual gboolean TpmCanAttemptOwnership(GError** error);
  virtual gboolean TpmClearStoredPassword(GError** error);

  virtual gboolean MigrateToDircrypto(const GArray* account_id,
                                      const GArray* migrate_request,
                                      GError** error);
  // Runs on the mount thread.
  virtual void DoMigrateToDircrypto(
      AccountIdentifier* identifier,
      MigrationType migration_type);

  virtual gboolean NeedsDircryptoMigration(const GArray* account_id,
                                           gboolean* OUT_needs_migration,
                                           GError** error);

  virtual gboolean GetSupportedKeyPolicies(const GArray* request,
                                           DBusGMethodInvocation* context);
  virtual void DoGetSupportedKeyPolicies(const std::string& request,
                                         DBusGMethodInvocation* context);

  // Remote Server Unlock related methods
  virtual void DoGetRsuDeviceId(DBusGMethodInvocation* context);
  virtual gboolean GetRsuDeviceId(const GArray* request,
                                  DBusGMethodInvocation* context);

  // Attestation functionality is implemented in descendant classes

  // Attestation-related hooks.
  // Called from Service::Initialize() before any other attestation calls.
  virtual void AttestationInitialize() = 0;
  // Called from Service::Initialize() if initialize_tpm_ is true.
  virtual void AttestationInitializeTpm() = 0;
  // Called from Service::OwnershipCallback().
  virtual void AttestationInitializeTpmComplete() = 0;
  // Called to fill attestation preparations. Returns |true| in case of
  // success, |false| otherwise.
  virtual bool AttestationGetEnrollmentPreparations(
      const AttestationGetEnrollmentPreparationsRequest& request,
      AttestationGetEnrollmentPreparationsReply* reply) = 0;
  // Called from Service::DoGetTpmStatus to fill attestation-related fields.
  virtual void AttestationGetTpmStatus(GetTpmStatusReply* reply) = 0;
  // Called from Service::ResetDictionaryAttackMitigation()
  // Provides the owner delegate credentials normally used for AIK activation.
  // Returns true on success.
  virtual bool AttestationGetDelegateCredentials(
      brillo::Blob* blob,
      brillo::Blob* secret,
      bool* has_reset_lock_permissions) = 0;

  // Attestation-related DBus calls.
  virtual gboolean TpmIsAttestationPrepared(gboolean* OUT_prepared,
                                            GError** error) = 0;
  virtual gboolean TpmVerifyAttestationData(gboolean is_cros_core,
                                            gboolean* OUT_verified,
                                            GError** error) = 0;
  virtual gboolean TpmVerifyEK(gboolean is_cros_core,
                               gboolean* OUT_verified,
                               GError** error) = 0;
  virtual gboolean TpmAttestationCreateEnrollRequest(gint pca_type,
                                                     GArray** OUT_pca_request,
                                                     GError** error) = 0;
  virtual gboolean AsyncTpmAttestationCreateEnrollRequest(gint pca_type,
                                                          gint* OUT_async_id,
                                                          GError** error) = 0;
  virtual gboolean TpmAttestationEnroll(gint pca_type,
                                        GArray* pca_response,
                                        gboolean* OUT_success,
                                        GError** error) = 0;
  virtual gboolean AsyncTpmAttestationEnroll(gint pca_type,
                                             GArray* pca_response,
                                             gint* OUT_async_id,
                                             GError** error) = 0;
  virtual gboolean TpmAttestationEnrollEx(gint pca_type,
                                          gboolean forced,
                                          gboolean* OUT_success,
                                          GError** error) = 0;
  virtual gboolean AsyncTpmAttestationEnrollEx(gint pca_type,
                                               gboolean forced,
                                               gint* OUT_async_id,
                                               GError** error) = 0;
  virtual gboolean TpmAttestationCreateCertRequest(
      gint pca_type,
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      GArray** OUT_pca_request,
      GError** error) = 0;
  virtual gboolean AsyncTpmAttestationCreateCertRequest(
      gint pca_type,
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationFinishCertRequest(GArray* pca_response,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   GArray** OUT_cert,
                                                   gboolean* OUT_success,
                                                   GError** error) = 0;
  virtual gboolean AsyncTpmAttestationFinishCertRequest(
      GArray* pca_response,
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationGetCertificateEx(
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      gint pca_type,
      gint key_type,
      gchar* key_name,
      gboolean forced,
      gboolean shall_trigger_enrollment,
      GArray** OUT_certificate,
      gboolean* OUT_success,
      GError** error) = 0;
  virtual gboolean AsyncTpmAttestationGetCertificateEx(
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      gint pca_type,
      gint key_type,
      gchar* key_name,
      gboolean forced,
      gboolean shall_trigger_enrollment,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmIsAttestationEnrolled(gboolean* OUT_is_enrolled,
                                            GError** error) = 0;
  virtual gboolean TpmAttestationDoesKeyExist(gboolean is_user_specific,
                                              gchar* username,
                                              gchar* key_name,
                                              gboolean *OUT_exists,
                                              GError** error) = 0;
  virtual gboolean TpmAttestationGetCertificate(gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_name,
                                                GArray **OUT_certificate,
                                                gboolean* OUT_success,
                                                GError** error) = 0;
  virtual gboolean TpmAttestationGetPublicKey(gboolean is_user_specific,
                                              gchar* username,
                                              gchar* key_name,
                                              GArray **OUT_public_key,
                                              gboolean* OUT_success,
                                              GError** error) = 0;
  virtual gboolean TpmAttestationRegisterKey(gboolean is_user_specific,
                                             gchar* username,
                                             gchar* key_name,
                                             gint *OUT_async_id,
                                             GError** error) = 0;
  virtual gboolean TpmAttestationSignEnterpriseChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationSignEnterpriseVaChallenge(
      gint va_type,
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gchar* key_name_for_spkac,
      gint* OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationSignSimpleChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      GArray* challenge,
      gint *OUT_async_id,
      GError** error) = 0;
  virtual gboolean TpmAttestationGetKeyPayload(gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               GArray** OUT_payload,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmAttestationSetKeyPayload(gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               GArray* payload,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmAttestationDeleteKeys(gboolean is_user_specific,
                                            gchar* username,
                                            gchar* key_prefix,
                                            gboolean* OUT_success,
                                            GError** error) = 0;
  virtual gboolean TpmAttestationDeleteKey(gboolean is_user_specific,
                                           gchar* username,
                                           gchar* key_name,
                                           gboolean* OUT_success,
                                           GError** error) = 0;
  virtual gboolean TpmAttestationGetEK(gchar** ek_info,
                                       gboolean* OUT_success,
                                       GError** error) = 0;
  virtual gboolean TpmAttestationResetIdentity(gchar* reset_token,
                                               GArray** OUT_reset_request,
                                               gboolean* OUT_success,
                                               GError** error) = 0;
  virtual gboolean TpmGetVersionStructured(guint32* OUT_family,
                                           guint64* OUT_spec_level,
                                           guint32* OUT_manufacturer,
                                           guint32* OUT_tpm_model,
                                           guint64* OUT_firmware_version,
                                           gchar** OUT_vendor_specific,
                                           GError** error);
  virtual gboolean GetEndorsementInfo(const GArray* request,
                                      DBusGMethodInvocation* context) = 0;
  virtual gboolean InitializeCastKey(const GArray* request,
                                     DBusGMethodInvocation* context) = 0;

  // Returns the label of the TPM token along with its user PIN.
  virtual gboolean Pkcs11GetTpmTokenInfo(gchar** OUT_label,
                                         gchar** OUT_user_pin,
                                         gint* OUT_slot,
                                         GError** error);
  // Returns the label of the TPM token along with its user PIN.
  virtual gboolean Pkcs11GetTpmTokenInfoForUser(gchar *username,
                                                gchar** OUT_label,
                                                gchar** OUT_user_pin,
                                                gint* OUT_slot,
                                                GError** error);

  // Returns in |OUT_ready| whether the TPM token is ready for use.
  virtual gboolean Pkcs11IsTpmTokenReady(gboolean* OUT_ready, GError** error);
  virtual gboolean Pkcs11Terminate(gchar* username, GError** error);
  virtual gboolean GetStatusString(gchar** OUT_status, GError** error);

  // Runs on the mount thread.
  virtual void CreateFingerprintManager();

  // InstallAttributes methods
  virtual gboolean InstallAttributesGet(gchar* name,
                              GArray** OUT_value,
                              gboolean* OUT_successful,
                              GError** error);
  virtual gboolean InstallAttributesSet(gchar* name,
                              GArray* value,
                              gboolean* OUT_successful,
                              GError** error);
  virtual gboolean InstallAttributesFinalize(gboolean* OUT_finalized,
                                             GError** error);
  virtual gboolean InstallAttributesCount(gint* OUT_count, GError** error);
  virtual gboolean InstallAttributesIsReady(gboolean* OUT_ready,
                                            GError** error);
  virtual gboolean InstallAttributesIsSecure(gboolean* OUT_secure,
                                             GError** error);
  virtual gboolean InstallAttributesIsInvalid(gboolean* OUT_invalid,
                                             GError** error);
virtual gboolean InstallAttributesIsFirstInstall(gboolean* OUT_first_install,
                                                 GError** error);

  // Runs on the mount thread.
  virtual void DoSignBootLockbox(const brillo::Blob& request,
                                 DBusGMethodInvocation* context);
  virtual gboolean SignBootLockbox(const GArray* request,
                                   DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoVerifyBootLockbox(const brillo::Blob& request,
                                   DBusGMethodInvocation* context);
  virtual gboolean VerifyBootLockbox(const GArray* request,
                                     DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoFinalizeBootLockbox(const brillo::Blob& request,
                                     DBusGMethodInvocation* context);
  virtual gboolean FinalizeBootLockbox(const GArray* request,
                                       DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoGetBootAttribute(const brillo::Blob& request,
                                  DBusGMethodInvocation* context);
  virtual gboolean GetBootAttribute(const GArray* request,
                                    DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoSetBootAttribute(const brillo::Blob& request,
                                  DBusGMethodInvocation* context);
  virtual gboolean SetBootAttribute(const GArray* request,
                                    DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoFlushAndSignBootAttributes(const brillo::Blob& request,
                                            DBusGMethodInvocation* context);
  virtual gboolean FlushAndSignBootAttributes(const GArray* request,
                                              DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetLoginStatus(const brillo::SecureBlob& request,
                                DBusGMethodInvocation* context);
  virtual gboolean GetLoginStatus(const GArray* request,
                                  DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoTpmAttestationGetEnrollmentPreparationsEx(
      const brillo::Blob& request,
      DBusGMethodInvocation* context);
  virtual gboolean TpmAttestationGetEnrollmentPreparationsEx(
      const GArray* request,
      DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetTpmStatus(const brillo::SecureBlob& request,
                              DBusGMethodInvocation* context);
  virtual gboolean GetTpmStatus(const GArray* request,
                                DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoStartFingerprintAuthSession(
      std::unique_ptr<AccountIdentifier> account_id,
      std::unique_ptr<StartFingerprintAuthSessionRequest> request,
      DBusGMethodInvocation* context);
  virtual gboolean StartFingerprintAuthSession(const GArray* account_id,
                                               const GArray* request,
                                               DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoEndFingerprintAuthSession(
      std::unique_ptr<EndFingerprintAuthSessionRequest> request,
      DBusGMethodInvocation* context);
  virtual gboolean EndFingerprintAuthSession(const GArray* request,
                                             DBusGMethodInvocation* context);
  // Runs on the mount thread.
  virtual void DoGetFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean GetFirmwareManagementParameters(const GArray* request,
                           DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoSetFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean SetFirmwareManagementParameters(const GArray* request,
                           DBusGMethodInvocation* context);

  // Runs on the mount thread.
  virtual void DoRemoveFirmwareManagementParameters(
      const brillo::SecureBlob& request,
      DBusGMethodInvocation* context);
  virtual gboolean RemoveFirmwareManagementParameters(const GArray* request,
                              DBusGMethodInvocation* context);

  virtual gboolean TpmAttestationGetEnrollmentId(gboolean ignore_cache,
                                                 GArray** OUT_enrollment_id,
                                                 gboolean* OUT_success,
                                                 GError** error) = 0;

  // Runs the event loop once. Only for testing.
  virtual void DispatchEventsForTesting();

  // Return the next sequence number.
  int NextSequence();

  // Whether or not quota-based disk usage stats is supported.
  virtual gboolean IsQuotaSupported(gboolean* OUT_quota_supported,
                                    GError** error);

  // Get the current disk space usage for the given uid.
  virtual gboolean GetCurrentSpaceForUid(guint32 uid,
                                         gint64* OUT_cur_space,
                                         GError** error);

  // Get the current disk space usage for the given gid.
  virtual gboolean GetCurrentSpaceForGid(guint gid,
                                         gint64* OUT_cur_space,
                                         GError** error);

  virtual gboolean LockToSingleUserMountUntilReboot(
      const GArray* request,
      DBusGMethodInvocation* context);

  virtual void DoLockToSingleUserMountUntilReboot(
      const std::string& obfuscated_username,
      DBusGMethodInvocation* context);

  virtual gboolean CheckHealth(const GArray* request,
                               DBusGMethodInvocation* context);

  void PostTaskToEventLoop(base::OnceClosure task);
 protected:
  FRIEND_TEST(ServiceTest, NoDeadlocksInInitializeTpmComplete);

  // Meta data for each imcoming dbus requests
  struct RequestTrackedInfo {
    std::string name;
    base::Time start_time;
  };

  bool use_tpm_;

  GMainLoop* loop_;
  // Can't use unique_ptr for cryptohome_ because memory is allocated by glib.
  gobject::Cryptohome* cryptohome_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<cryptohome::Platform> default_platform_;
  cryptohome::Platform* platform_;
  std::unique_ptr<cryptohome::Crypto> default_crypto_;
  cryptohome::Crypto* crypto_;
  // TPM doesn't use the unique_ptr for default pattern, since the tpm is a
  // singleton - we don't want it getting destroyed when we are.
  Tpm* tpm_;
  std::unique_ptr<TpmInit> default_tpm_init_;
  TpmInit* tpm_init_;
  std::unique_ptr<FingerprintManager> fingerprint_manager_;
  std::unique_ptr<Pkcs11Init> default_pkcs11_init_;
  Pkcs11Init* pkcs11_init_;
  bool initialize_tpm_;
  MountThreadObserver mount_thread_observer_;
  base::Thread mount_thread_;
  guint async_complete_signal_;
  // A completion signal for async calls that return data.
  guint async_data_complete_signal_;
  guint tpm_init_signal_;
  guint low_disk_space_signal_;
  guint dircrypto_migration_progress_signal_;
  bool low_disk_space_signal_was_emitted_;
  CryptohomeEventSource event_source_;
  CryptohomeEventSourceSink* event_source_sink_;
  base::Time last_auto_cleanup_time_;
  base::Time last_user_activity_timestamp_time_;
  std::unique_ptr<cryptohome::InstallAttributes> default_install_attrs_;
  cryptohome::InstallAttributes* install_attrs_;
  // Keeps track of whether a failure on PKCS#11 initialization was reported
  // during this user login. We use this not to report a same failure multiple
  // times.
  bool reported_pkcs11_init_fail_;
  // Keeps track of whether the device is enterprise-owned.
  bool enterprise_owned_;
  // Track the metadata of incoming dbus request, used for reporting UMA.
  std::map<int, RequestTrackedInfo> async_id_tracked_info_;

  // Should we skip the ownership taken signal connection. This value shouldn't
  // be true unless it's for testing purposes.
  //
  // TODO(garryxiao): remove this after we migrate to the new dbus librabry.
  bool skip_ownership_taken_signal_connection_;

  virtual GMainLoop *main_loop() { return loop_; }

  // Called periodically from LowDiskCallback to initiate automatic disk
  // cleanup if needed.
  virtual void DoAutoCleanup();
  // Called periodically from LowDiskCallback.
  // Update current user's activity timestamp every day.
  virtual void UpdateCurrentUserActivityTimestamp();
  // Called periodically on Mount thread to detect low disk space and emit a
  // signal if detected.
  virtual void LowDiskCallback();
  // Called periodically to fetch alerts data from TPM and upload it to UMA.
  virtual void UploadAlertsDataCallback();
  // Filters out active mounts from |mounts|, populating |active_mounts| set.
  // If |force| is false, it ignores stale mounts that that have open files
  // and mount points connected to children of the mount source.
  // Returns true if any stale mount filtered out because of open files.
  virtual bool FilterActiveMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts,
      std::multimap<const base::FilePath, const base::FilePath>* active_mounts,
      bool force);
  // Populates |mounts| with ephemeral cryptohome mount points.
  virtual void GetEphemeralLoopDevicesMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Checks if the machine is enterprise owned and report to mount_ then.
  virtual void DetectEnterpriseOwnership();

  virtual scoped_refptr<cryptohome::Mount> GetMountForUser(
      const std::string& username);

  // Ensures only one Mount is ever created per username.
  virtual scoped_refptr<cryptohome::Mount> GetOrCreateMountForUser(
      const std::string& username);

  // Safely removes the MountMap reference for the given Mount.
  virtual bool RemoveMountForUser(const std::string& username);

  // Safelt removes the given Mount from MountMap.
  virtual void RemoveMount(cryptohome::Mount* mount);

  // Safely empties the MountMap and may request unmounting. If |unmount| is
  // true, the return value will reflect if all mounts unmounted cleanly or not.
  virtual bool RemoveAllMounts(bool unmount);

  // Unload any pkcs11 tokens _not_ belonging to one of the mounts in |exclude|.
  // This is used to clean up any stale loaded tokens after a cryptohome crash.
  virtual bool UnloadPkcs11Tokens(const std::vector<base::FilePath>& exclude);

  // A wrapper for PostTask to mount_thread_ which also count some metrics
  virtual void PostTask(const base::Location& from_here,
                        base::OnceClosure task);

  // Posts a message back from the mount_thread_ to the main thread to
  // reply to a DBus message.  Only call from mount_thread_-based
  // functions!
  virtual void SendReply(DBusGMethodInvocation* context,
                         const BaseReply& reply);

  // Helper methods that post a message back to the main thread where
  // a DBus InvalidArgs GError is returned to the caller.
  // Only call from mount_thread_-based functions!
  virtual void SendDBusErrorReply(DBusGMethodInvocation* context,
                                  GQuark domain,
                                  gint code,
                                  const gchar* message);
  virtual void SendInvalidArgsReply(DBusGMethodInvocation* context,
                                    const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_INVALID_ARGS,
                       message);
  }
  virtual void SendFailureReply(DBusGMethodInvocation* context,
                                const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_FAILED,
                       message);
  }
  virtual void SendNotSupportedReply(DBusGMethodInvocation* context,
                                     const char* message) {
    SendDBusErrorReply(context,
                       DBUS_GERROR, DBUS_GERROR_NOT_SUPPORTED,
                       message);
  }

  // Returns a CryptohomeErrorCode for an internal Mount::MountError code.
  virtual CryptohomeErrorCode MountErrorToCryptohomeError(
      const MountError code) const;

  // Sends a signal for notifying the migration progress.
  // Runs on the mount thread.
  virtual void SendDircryptoMigrationProgressSignal(
      DircryptoMigrationStatus status,
      uint64_t current_bytes,
      uint64_t total_bytes);

  // This is a simple conversion that takes the protobuf version of the progress
  // callback and forward it to SendDircryptoMigrationProgressSignal() above.
  virtual void SendDircryptoMigrationProgressSignalProto(
      const user_data_auth::DircryptoMigrationProgress& progress);

  // Listens to the ownership taken signal sent from tpm manager.
  virtual void ConnectOwnershipTakenSignal() = 0;

  // Stop processing tasks on dbus and mount threads.
  // Must be called from derived destructors. Otherwise, after derived
  // destructor, all pure virtual functions from Service overloaded there and
  // all members defined for that class will be gone, while mount_thread_
  // will continue running tasks until stopped in ~Service.
  void StopTasks();

  // Store the information for |async_id| which is unique of dbus requests.
  // |name| and |start_time| will store and report to UMA when this |async_id|
  // is replied.
  void LogAsyncIdInfo(int async_id, std::string name, base::Time start_time);
  // Report the UMA of the running time of |async_id| and cleanup the stored
  // information.
  void SendAsyncIdInfoToUma(int async_id, base::Time finished_time);

  // Called on Mount thread. This method calls ReportDictionaryAttackResetStatus
  // exactly once (i.e. records one sample) with the status of the operation.
  void ResetDictionaryAttackMitigation();

 private:
  FRIEND_TEST(ServiceTest, GetPublicMountPassKey);

  std::unique_ptr<brillo::SecureBlob> GetAttestationBasedEnrollmentData();

  bool CreatePublicMountSaltIfNeeded();

  // Gets passkey for |public_mount_id|. Returns true if a passkey is generated
  // successfully. Otherwise, returns false.
  bool GetPublicMountPassKey(const std::string& public_mount_id,
                             std::string* public_mount_passkey);

  // Performs the lazy part of the initialization that is required for
  // performing operations with challenge-response keys. Returns whether
  // succeeded.
  bool InitForChallengeResponseAuth(CryptohomeErrorCode* error_code);

  // Called on Mount thread. This triggers the credentials verification steps
  // that are specific to challenge-response keys, going through
  // {TryLightweightChallengeResponseCheckKeyEx(),
  // OnLightweightChallengeResponseCheckKeyExDone()} and/or
  // {DoFullChallengeResponseCheckKeyEx(),
  // OnFullChallengeResponseCheckKeyExDone()}.
  void DoChallengeResponseCheckKeyEx(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      DBusGMethodInvocation* context);
  void TryLightweightChallengeResponseCheckKeyEx(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      DBusGMethodInvocation* context);
  void OnLightweightChallengeResponseCheckKeyExDone(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      DBusGMethodInvocation* context,
      bool is_key_valid);
  void DoFullChallengeResponseCheckKeyEx(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      DBusGMethodInvocation* context);
  void OnFullChallengeResponseCheckKeyExDone(
      DBusGMethodInvocation* context, std::unique_ptr<Credentials> credentials);

  // Called on Mount thread. This triggers the credentials generation steps that
  // are specific to challenge-response keys, before scheduling
  // ContinueMountExWithCredentials().
  void DoChallengeResponseMountEx(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      std::unique_ptr<MountRequest> request,
      const Mount::MountArgs& mount_args,
      DBusGMethodInvocation* context);
  // Called on Mount thread when the challenge-response credentials are
  // obtained.
  void OnChallengeResponseMountCredentialsObtained(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      std::unique_ptr<MountRequest> request,
      const Mount::MountArgs& mount_args,
      DBusGMethodInvocation* context,
      std::unique_ptr<Credentials> credentials);

  // Called on Mount thread. This method completes the MountEx request, given
  // the built Credentials object. It assumes that the input parameters went
  // through format sanity checks.
  void ContinueMountExWithCredentials(
      std::unique_ptr<AccountIdentifier> identifier,
      std::unique_ptr<AuthorizationRequest> authorization,
      std::unique_ptr<MountRequest> request,
      std::unique_ptr<Credentials> credentials,
      const Mount::MountArgs& mount_args,
      DBusGMethodInvocation* context);

  // Builds the PCR restrictions to be applied to the challenge-protected vault
  // keyset.
  void GetChallengeCredentialsPcrRestrictions(
      const std::string& obfuscated_username,
      std::vector<std::map<uint32_t, brillo::Blob>>* pcr_restrictions);

  // Determines whether the mount request should be ephemeral. On error, returns
  // false and sets the error code in |error|. Otherwise, returns true and fills
  // the result in |is_ephemeral|.
  bool GetShouldMountAsEphemeral(const std::string& account_id,
                                 bool is_ephemeral_mount_requested,
                                 bool has_create_request,
                                 bool* is_ephemeral,
                                 MountError* error) const;
  // Called on Mount thread when starting fingerprint auth session succeeds or
  // fails.
  void OnStartFingerprintAuthSessionDone(DBusGMethodInvocation* context,
                                         bool success);

  // Called on Mount thread. Scheduled by DoCheckKeyEx(). Callback for one
  // fingerprint scan. Completes fingerprint CheckKeyEx by replying to the
  // CheckKeyEx call in |context|. Sets the error if the scan has an error.
  void CompleteFingerprintCheckKeyEx(DBusGMethodInvocation* context,
                                     FingerprintScanStatus status);

  // This is a utility function for stateful recovery functionality to call when
  // it wants to mount a user's home directory. The user specified by
  // |username|'s home is mounted with |passkey|, and if successfully mounted,
  // returns true and set out_home_path to the mounted home. Otherwise, returns
  // false. Note that this function must log any error itself, no logging will
  // be done by the caller.
  bool StatefulRecoveryMount(const std::string& username,
                             const std::string& passkey,
                             FilePath* out_home_path);

  // This is a utility function for stateful recovery to unmount all user's home
  // directories. It'll return true if all the user's home directories are
  // successfully unmounted. Otherwise, it'll return false. Note that this
  // function must log any error itself, no logging will be done by the caller.
  bool StatefulRecoveryUnmount();

  brillo::DBusConnection system_dbus_connection_;

  // Tracks Mount objects for each user by username.
  typedef std::map<const std::string, scoped_refptr<cryptohome::Mount>>
      MountMap;
  MountMap mounts_;
  base::Lock mounts_lock_;  // Protects against parallel insertions only.
  std::unique_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;
  std::unique_ptr<cryptohome::MountFactory> default_mount_factory_;
  cryptohome::MountFactory* mount_factory_;

  std::unique_ptr<HomeDirs> default_homedirs_;
  HomeDirs* homedirs_;
  std::unique_ptr<ArcDiskQuota> default_arc_disk_quota_;
  ArcDiskQuota* arc_disk_quota_;
  std::string guest_user_;
  bool force_ecryptfs_;
  bool legacy_mount_;
  brillo::SecureBlob public_mount_salt_;
  std::unique_ptr<chaps::TokenManagerClient> default_chaps_client_;
  chaps::TokenManagerClient* chaps_client_;
  std::unique_ptr<BootLockbox> default_boot_lockbox_;
  // After construction, this should only be used on the mount thread.
  BootLockbox* boot_lockbox_;
  std::unique_ptr<BootAttributes> default_boot_attributes_;
  // After construction, this should only be used on the mount thread.
  BootAttributes* boot_attributes_;
  KeyChallengeServiceFactoryImpl default_key_challenge_service_factory_;
  KeyChallengeServiceFactory* key_challenge_service_factory_ =
      &default_key_challenge_service_factory_;
  std::unique_ptr<FirmwareManagementParameters>
      default_firmware_management_params_;
  FirmwareManagementParameters* firmware_management_parameters_;
  int low_disk_notification_period_ms_;
  int upload_alerts_period_ms_;
  std::unique_ptr<ChallengeCredentialsHelper>
      default_challenge_credentials_helper_;
  ChallengeCredentialsHelper* challenge_credentials_helper_ = nullptr;

  // An atomic incrementing sequence for setting asynchronous call ids.
  base::AtomicSequenceNumber sequence_holder_;

  // The signal callback to make GMainLoop quit gracefully.
  static gboolean ShutdownService(gpointer user_data);

  // This is set to true iff OwnershipCallback has run.
  bool ownership_callback_has_run_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_H_
