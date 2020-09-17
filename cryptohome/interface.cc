// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "cryptohome/interface.h"

namespace cryptohome {
namespace gobject {

// Register with the glib type system.
// This macro automatically defines a number of functions and variables
// which are required to make cryptohome functional as a GObject:
// - cryptohome_parent_class
// - cryptohome_get_type()
// - dbus_glib_cryptohome_object_info
// It also ensures that the structs are setup so that the initialization
// functions are called in the appropriate way by g_object_new().
G_DEFINE_TYPE(Cryptohome, cryptohome, G_TYPE_OBJECT);

GObject* cryptohome_constructor(GType gtype,
                                guint n_properties,
                                GObjectConstructParam* properties) {
  GObject* obj;
  GObjectClass* parent_class;
  // Instantiate using the parent class, then extend for local properties.
  parent_class = G_OBJECT_CLASS(cryptohome_parent_class);
  obj = parent_class->constructor(gtype, n_properties, properties);

  Cryptohome* cryptohome = reinterpret_cast<Cryptohome*>(obj);
  cryptohome->service = NULL;

  // We don't have any thing we care to expose to the glib class system.
  return obj;
}

void cryptohome_class_init(CryptohomeClass* real_class) {
  // Called once to configure the "class" structure.
  GObjectClass* gobject_class = G_OBJECT_CLASS(real_class);
  gobject_class->constructor = cryptohome_constructor;
}

void cryptohome_init(Cryptohome* self) {}

// TODO(wad) add error messaging
#define CRYPTOHOME_WRAP_METHOD(_NAME, args...) \
  if (!self->service) {                        \
    return FALSE;                              \
  }                                            \
  return self->service->_NAME(args, error);

#define CRYPTOHOME_WRAP_METHOD_NO_ARGS(_NAME) \
  if (!self->service) {                       \
    return FALSE;                             \
  }                                           \
  return self->service->_NAME(error);

gboolean cryptohome_check_key_ex(Cryptohome* self,
                                 GArray* identifier,
                                 GArray* authorization,
                                 GArray* request,
                                 DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(CheckKeyEx, identifier, authorization, request);
}
gboolean cryptohome_remove_key_ex(Cryptohome* self,
                                  GArray* identifier,
                                  GArray* authorization,
                                  GArray* request,
                                  DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(RemoveKeyEx, identifier, authorization, request);
}
gboolean cryptohome_get_key_data_ex(Cryptohome* self,
                                    GArray* identifier,
                                    GArray* authorization,
                                    GArray* request,
                                    DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(GetKeyDataEx, identifier, authorization, request);
}
gboolean cryptohome_mass_remove_keys(Cryptohome* self,
                                     GArray* identifier,
                                     GArray* authorization,
                                     GArray* request,
                                     DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(MassRemoveKeys, identifier, authorization, request);
}
gboolean cryptohome_list_keys_ex(Cryptohome* self,
                                 GArray* identifier,
                                 GArray* authorization,
                                 GArray* request,
                                 DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(ListKeysEx, identifier, authorization, request);
}
gboolean cryptohome_migrate_key_ex(Cryptohome* self,
                                   GArray* account,
                                   GArray* auth_request,
                                   GArray* migrate_request,
                                   DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(MigrateKeyEx, account, auth_request, migrate_request);
}
gboolean cryptohome_add_key_ex(Cryptohome* self,
                               GArray* id,
                               GArray* auth,
                               GArray* params,
                               DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(AddKeyEx, id, auth, params);
}
gboolean cryptohome_add_data_restore_key(Cryptohome* self,
                                         GArray* id,
                                         GArray* auth,
                                         DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(AddDataRestoreKey, id, auth);
}
gboolean cryptohome_update_key_ex(Cryptohome* self,
                                  GArray* id,
                                  GArray* auth,
                                  GArray* params,
                                  DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(UpdateKeyEx, id, auth, params);
}
gboolean cryptohome_remove_ex(Cryptohome* self,
                              GArray* account,
                              DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(RemoveEx, account);
}
gboolean cryptohome_rename_cryptohome(Cryptohome* self,
                                      GArray* account_id_from,
                                      GArray* account_id_to,
                                      DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(RenameCryptohome, account_id_from, account_id_to);
}
gboolean cryptohome_get_account_disk_usage(Cryptohome* self,
                                           GArray* account_id,
                                           DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetAccountDiskUsage, account_id);
}
gboolean cryptohome_get_system_salt(Cryptohome* self,
                                    GArray** OUT_salt,
                                    GError** error) {
  CRYPTOHOME_WRAP_METHOD(GetSystemSalt, OUT_salt);
}
gboolean cryptohome_get_sanitized_username(Cryptohome* self,
                                           gchar* username,
                                           gchar** OUT_sanitized,
                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(GetSanitizedUsername, username, OUT_sanitized);
}
gboolean cryptohome_is_mounted(Cryptohome* self,
                               gboolean* OUT_is_mounted,
                               GError** error) {
  CRYPTOHOME_WRAP_METHOD(IsMounted, OUT_is_mounted);
}
gboolean cryptohome_is_mounted_for_user(Cryptohome* self,
                                        gchar* userid,
                                        gboolean* OUT_is_mounted,
                                        gboolean* OUT_is_ephemeral_mount,
                                        GError** error) {
  CRYPTOHOME_WRAP_METHOD(IsMountedForUser, userid, OUT_is_mounted,
                         OUT_is_ephemeral_mount);
}
gboolean cryptohome_mount_ex(Cryptohome* self,
                             GArray* id,
                             GArray* auth,
                             GArray* params,
                             DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(MountEx, id, auth, params);
}
gboolean cryptohome_mount_guest_ex(Cryptohome* self,
                                   GArray* request,
                                   DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(MountGuestEx, request);
}
gboolean cryptohome_unmount_ex(Cryptohome* self,
                               GArray* request,
                               DBusGMethodInvocation* error) {
  // Leave the response called error to reuse WRAP.
  CRYPTOHOME_WRAP_METHOD(UnmountEx, request);
}
gboolean cryptohome_update_current_user_activity_timestamp(Cryptohome* self,
                                                           gint time_shift_sec,
                                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(UpdateCurrentUserActivityTimestamp, time_shift_sec);
}
gboolean cryptohome_tpm_is_ready(Cryptohome* self,
                                 gboolean* OUT_ready,
                                 GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsReady, OUT_ready);
}
gboolean cryptohome_tpm_is_enabled(Cryptohome* self,
                                   gboolean* OUT_enabled,
                                   GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsEnabled, OUT_enabled);
}
gboolean cryptohome_tpm_get_password(Cryptohome* self,
                                     gchar** OUT_password,
                                     GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmGetPassword, OUT_password);
}
gboolean cryptohome_tpm_is_owned(Cryptohome* self,
                                 gboolean* OUT_owned,
                                 GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsOwned, OUT_owned);
}
gboolean cryptohome_tpm_is_being_owned(Cryptohome* self,
                                       gboolean* OUT_owning,
                                       GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsBeingOwned, OUT_owning);
}
gboolean cryptohome_tpm_can_attempt_ownership(Cryptohome* self,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD_NO_ARGS(TpmCanAttemptOwnership);
}
gboolean cryptohome_tpm_clear_stored_password(Cryptohome* self,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD_NO_ARGS(TpmClearStoredPassword);
}
gboolean cryptohome_tpm_is_attestation_prepared(Cryptohome* self,
                                                gboolean* OUT_prepared,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsAttestationPrepared, OUT_prepared);
}
gboolean cryptohome_tpm_attestation_get_enrollment_preparations_ex(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetEnrollmentPreparationsEx, request);
}
gboolean cryptohome_tpm_verify_attestation_data(Cryptohome* self,
                                                gboolean is_cros_core,
                                                gboolean* OUT_verified,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmVerifyAttestationData, is_cros_core, OUT_verified);
}
gboolean cryptohome_tpm_verify_ek(Cryptohome* self,
                                  gboolean is_cros_core,
                                  gboolean* OUT_verified,
                                  GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmVerifyEK, is_cros_core, OUT_verified);
}
gboolean cryptohome_tpm_attestation_create_enroll_request(
    Cryptohome* self, gint pca_type, GArray** OUT_pca_request, GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationCreateEnrollRequest, pca_type,
                         OUT_pca_request);
}
gboolean cryptohome_async_tpm_attestation_create_enroll_request(
    Cryptohome* self, gint pca_type, gint* OUT_async_id, GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationCreateEnrollRequest, pca_type,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_enroll(Cryptohome* self,
                                           gint pca_type,
                                           GArray* pca_response,
                                           gboolean* OUT_success,
                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationEnroll, pca_type, pca_response,
                         OUT_success);
}
gboolean cryptohome_async_tpm_attestation_enroll(Cryptohome* self,
                                                 gint pca_type,
                                                 GArray* pca_response,
                                                 gint* OUT_async_id,
                                                 GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationEnroll, pca_type, pca_response,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_enroll_ex(Cryptohome* self,
                                              gint pca_type,
                                              gboolean forced,
                                              gboolean* OUT_success,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationEnrollEx, pca_type, forced, OUT_success);
}
gboolean cryptohome_async_tpm_attestation_enroll_ex(Cryptohome* self,
                                                    gint pca_type,
                                                    gboolean forced,
                                                    gint* OUT_async_id,
                                                    GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationEnrollEx, pca_type, forced,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_create_cert_request(
    Cryptohome* self,
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    GArray** OUT_pca_request,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationCreateCertRequest, pca_type,
                         certificate_profile, username, request_origin,
                         OUT_pca_request);
}
gboolean cryptohome_async_tpm_attestation_create_cert_request(
    Cryptohome* self,
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationCreateCertRequest, pca_type,
                         certificate_profile, username, request_origin,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_finish_cert_request(
    Cryptohome* self,
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_cert,
    gboolean* OUT_success,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationFinishCertRequest, pca_response,
                         is_user_specific, username, key_name, OUT_cert,
                         OUT_success);
}
gboolean cryptohome_async_tpm_attestation_finish_cert_request(
    Cryptohome* self,
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationFinishCertRequest, pca_response,
                         is_user_specific, username, key_name, OUT_async_id);
}
gboolean cryptohome_tpm_attestation_get_certificate_ex(
    Cryptohome* self,
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
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetCertificateEx, certificate_profile,
                         username, request_origin, pca_type, key_type, key_name,
                         forced, shall_trigger_enrollment, OUT_certificate,
                         OUT_success);
}
gboolean cryptohome_async_tpm_attestation_get_certificate_ex(
    Cryptohome* self,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint pca_type,
    gint key_type,
    gchar* key_name,
    gboolean forced,
    gboolean shall_trigger_enrollment,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(AsyncTpmAttestationGetCertificateEx,
                         certificate_profile, username, request_origin,
                         pca_type, key_type, key_name, forced,
                         shall_trigger_enrollment, OUT_async_id);
}
gboolean cryptohome_tpm_is_attestation_enrolled(Cryptohome* self,
                                                gboolean* OUT_is_enrolled,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmIsAttestationEnrolled, OUT_is_enrolled);
}
gboolean cryptohome_tpm_attestation_does_key_exist(Cryptohome* self,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   gboolean* OUT_exists,
                                                   GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationDoesKeyExist, is_user_specific, username,
                         key_name, OUT_exists);
}
gboolean cryptohome_tpm_attestation_get_certificate(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray** OUT_certificate,
                                                    gboolean* OUT_success,
                                                    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetCertificate, is_user_specific,
                         username, key_name, OUT_certificate, OUT_success);
}
gboolean cryptohome_tpm_attestation_get_public_key(Cryptohome* self,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   GArray** OUT_public_key,
                                                   gboolean* OUT_success,
                                                   GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetPublicKey, is_user_specific, username,
                         key_name, OUT_public_key, OUT_success);
}
gboolean cryptohome_tpm_attestation_register_key(Cryptohome* self,
                                                 gboolean is_user_specific,
                                                 gchar* username,
                                                 gchar* key_name,
                                                 gint* OUT_async_id,
                                                 GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationRegisterKey, is_user_specific, username,
                         key_name, OUT_async_id);
}
gboolean cryptohome_tpm_attestation_sign_enterprise_challenge(
    Cryptohome* self,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gchar* domain,
    GArray* device_id,
    gboolean include_signed_public_key,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationSignEnterpriseChallenge,
                         is_user_specific, username, key_name, domain,
                         device_id, include_signed_public_key, challenge,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_sign_enterprise_va_challenge(
    Cryptohome* self,
    gint va_type,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gchar* domain,
    GArray* device_id,
    gboolean include_signed_public_key,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationSignEnterpriseVaChallenge, va_type,
                         is_user_specific, username, key_name, domain,
                         device_id, include_signed_public_key, challenge, NULL,
                         OUT_async_id);
}
gboolean cryptohome_tpm_attestation_sign_enterprise_va_challenge_v2(
    Cryptohome* self,
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
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationSignEnterpriseVaChallenge, va_type,
                         is_user_specific, username, key_name, domain,
                         device_id, include_signed_public_key, challenge,
                         key_name_for_spkac, OUT_async_id);
}
gboolean cryptohome_tpm_attestation_sign_simple_challenge(
    Cryptohome* self,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationSignSimpleChallenge, is_user_specific,
                         username, key_name, challenge, OUT_async_id);
}
gboolean cryptohome_tpm_attestation_get_key_payload(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray** OUT_payload,
                                                    gboolean* OUT_success,
                                                    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetKeyPayload, is_user_specific,
                         username, key_name, OUT_payload, OUT_success);
}
gboolean cryptohome_tpm_attestation_set_key_payload(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray* payload,
                                                    gboolean* OUT_success,
                                                    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationSetKeyPayload, is_user_specific,
                         username, key_name, payload, OUT_success);
}
gboolean cryptohome_tpm_attestation_delete_keys(Cryptohome* self,
                                                gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_prefix,
                                                gboolean* OUT_success,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationDeleteKeys, is_user_specific, username,
                         key_prefix, OUT_success);
}
gboolean cryptohome_tpm_attestation_delete_key(Cryptohome* self,
                                               gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               gboolean* OUT_success,
                                               GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationDeleteKey, is_user_specific, username,
                         key_name, OUT_success);
}
gboolean cryptohome_tpm_attestation_get_ek(Cryptohome* self,
                                           gchar** OUT_ek_info,
                                           gboolean* OUT_success,
                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetEK, OUT_ek_info, OUT_success);
}
gboolean cryptohome_tpm_attestation_reset_identity(Cryptohome* self,
                                                   gchar* reset_token,
                                                   GArray** OUT_reset_request,
                                                   gboolean* OUT_success,
                                                   GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationResetIdentity, reset_token,
                         OUT_reset_request, OUT_success);
}
gboolean cryptohome_tpm_get_version_structured(Cryptohome* self,
                                               guint32* OUT_family,
                                               guint64* OUT_spec_level,
                                               guint32* OUT_manufacturer,
                                               guint32* OUT_tpm_model,
                                               guint64* OUT_firmware_version,
                                               gchar** OUT_vendor_specific,
                                               GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmGetVersionStructured, OUT_family, OUT_spec_level,
                         OUT_manufacturer, OUT_tpm_model, OUT_firmware_version,
                         OUT_vendor_specific);
}
gboolean cryptohome_pkcs11_get_tpm_token_info(Cryptohome* self,
                                              gchar** OUT_label,
                                              gchar** OUT_user_pin,
                                              gint* OUT_slot,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(Pkcs11GetTpmTokenInfo, OUT_label, OUT_user_pin,
                         OUT_slot);
}
gboolean cryptohome_pkcs11_get_tpm_token_info_for_user(Cryptohome* self,
                                                       gchar* username,
                                                       gchar** OUT_label,
                                                       gchar** OUT_user_pin,
                                                       gint* OUT_slot,
                                                       GError** error) {
  CRYPTOHOME_WRAP_METHOD(Pkcs11GetTpmTokenInfoForUser, username, OUT_label,
                         OUT_user_pin, OUT_slot);
}
gboolean cryptohome_pkcs11_is_tpm_token_ready(Cryptohome* self,
                                              gboolean* OUT_ready,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(Pkcs11IsTpmTokenReady, OUT_ready);
}
gboolean cryptohome_pkcs11_terminate(Cryptohome* self,
                                     gchar* username,
                                     GError** error) {
  CRYPTOHOME_WRAP_METHOD(Pkcs11Terminate, username);
}
gboolean cryptohome_get_status_string(Cryptohome* self,
                                      gchar** OUT_status,
                                      GError** error) {
  CRYPTOHOME_WRAP_METHOD(GetStatusString, OUT_status);
}

gboolean cryptohome_install_attributes_get(Cryptohome* self,
                                           gchar* name,
                                           GArray** OUT_value,
                                           gboolean* OUT_successful,
                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesGet, name, OUT_value, OUT_successful);
}

gboolean cryptohome_install_attributes_set(Cryptohome* self,
                                           gchar* name,
                                           GArray* value,
                                           gboolean* OUT_successful,
                                           GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesSet, name, value, OUT_successful);
}

gboolean cryptohome_install_attributes_finalize(Cryptohome* self,
                                                gboolean* OUT_successful,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesFinalize, OUT_successful);
}

gboolean cryptohome_install_attributes_count(Cryptohome* self,
                                             gint* OUT_count,
                                             GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesCount, OUT_count);
}

gboolean cryptohome_install_attributes_is_ready(Cryptohome* self,
                                                gboolean* OUT_is_ready,
                                                GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesIsReady, OUT_is_ready);
}

gboolean cryptohome_install_attributes_is_secure(Cryptohome* self,
                                                 gboolean* OUT_is_secure,
                                                 GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesIsSecure, OUT_is_secure);
}

gboolean cryptohome_install_attributes_is_invalid(Cryptohome* self,
                                                  gboolean* OUT_is_invalid,
                                                  GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesIsInvalid, OUT_is_invalid);
}

gboolean cryptohome_install_attributes_is_first_install(
    Cryptohome* self, gboolean* OUT_is_first_install, GError** error) {
  CRYPTOHOME_WRAP_METHOD(InstallAttributesIsFirstInstall, OUT_is_first_install);
}

gboolean cryptohome_sign_boot_lockbox(Cryptohome* self,
                                      GArray* request,
                                      DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(SignBootLockbox, request);
}

gboolean cryptohome_verify_boot_lockbox(Cryptohome* self,
                                        GArray* request,
                                        DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(VerifyBootLockbox, request);
}

gboolean cryptohome_finalize_boot_lockbox(Cryptohome* self,
                                          GArray* request,
                                          DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(FinalizeBootLockbox, request);
}

gboolean cryptohome_get_boot_attribute(Cryptohome* self,
                                       GArray* request,
                                       DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetBootAttribute, request);
}

gboolean cryptohome_set_boot_attribute(Cryptohome* self,
                                       GArray* request,
                                       DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(SetBootAttribute, request);
}

gboolean cryptohome_flush_and_sign_boot_attributes(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(FlushAndSignBootAttributes, request);
}

gboolean cryptohome_get_login_status(Cryptohome* self,
                                     GArray* request,
                                     DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetLoginStatus, request);
}

gboolean cryptohome_get_tpm_status(Cryptohome* self,
                                   GArray* request,
                                   DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetTpmStatus, request);
}

gboolean cryptohome_get_endorsement_info(Cryptohome* self,
                                         GArray* request,
                                         DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetEndorsementInfo, request);
}

gboolean cryptohome_initialize_cast_key(Cryptohome* self,
                                        GArray* request,
                                        DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(InitializeCastKey, request);
}

gboolean cryptohome_start_fingerprint_auth_session(
    Cryptohome* self,
    GArray* id,
    GArray* request,
    DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(StartFingerprintAuthSession, id, request);
}

gboolean cryptohome_end_fingerprint_auth_session(Cryptohome* self,
                                                 GArray* request,
                                                 DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(EndFingerprintAuthSession, request);
}

gboolean cryptohome_get_web_authn_secret(Cryptohome* self,
                                         GArray* id,
                                         GArray* request,
                                         DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetWebAuthnSecret, id, request);
}

gboolean cryptohome_get_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetFirmwareManagementParameters, request);
}

gboolean cryptohome_set_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(SetFirmwareManagementParameters, request);
}

gboolean cryptohome_remove_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(RemoveFirmwareManagementParameters, request);
}

gboolean cryptohome_migrate_to_dircrypto(Cryptohome* self,
                                         GArray* id,
                                         GArray* migrate_request,
                                         GError** error) {
  CRYPTOHOME_WRAP_METHOD(MigrateToDircrypto, id, migrate_request);
}

gboolean cryptohome_needs_dircrypto_migration(Cryptohome* self,
                                              GArray* identifier,
                                              gboolean* OUT_needs_migration,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(NeedsDircryptoMigration, identifier,
                         OUT_needs_migration);
}

gboolean cryptohome_tpm_attestation_get_enrollment_id(
    Cryptohome* self,
    gboolean ignore_cache,
    GArray** OUT_enrollment_id,
    gboolean* OUT_success,
    GError** error) {
  CRYPTOHOME_WRAP_METHOD(TpmAttestationGetEnrollmentId, ignore_cache,
                         OUT_enrollment_id, OUT_success);
}

gboolean cryptohome_get_supported_key_policies(Cryptohome* self,
                                               GArray* request,
                                               DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetSupportedKeyPolicies, request);
}

gboolean cryptohome_is_quota_supported(Cryptohome* self,
                                       gboolean* OUT_quota_supported,
                                       GError** error) {
  CRYPTOHOME_WRAP_METHOD(IsQuotaSupported, OUT_quota_supported);
}

gboolean cryptohome_get_current_space_for_uid(Cryptohome* self,
                                              guint32 uid,
                                              gint64* OUT_cur_space,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(GetCurrentSpaceForUid, uid, OUT_cur_space);
}

gboolean cryptohome_get_current_space_for_gid(Cryptohome* self,
                                              guint32 gid,
                                              gint64* OUT_cur_space,
                                              GError** error) {
  CRYPTOHOME_WRAP_METHOD(GetCurrentSpaceForGid, gid, OUT_cur_space);
}

gboolean cryptohome_lock_to_single_user_mount_until_reboot(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(LockToSingleUserMountUntilReboot, request);
}

gboolean cryptohome_get_rsu_device_id(Cryptohome* self,
                                      GArray* request,
                                      DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(GetRsuDeviceId, request);
}

gboolean cryptohome_check_health(Cryptohome* self,
                                 GArray* request,
                                 DBusGMethodInvocation* error) {
  CRYPTOHOME_WRAP_METHOD(CheckHealth, request);
}

#undef CRYPTOHOME_WRAP_METHOD

}  // namespace gobject
}  // namespace cryptohome
