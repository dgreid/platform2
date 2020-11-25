// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_INTERFACE_H_
#define CRYPTOHOME_INTERFACE_H_

#include <stdlib.h>

#include <base/logging.h>
#include <brillo/glib/dbus.h>
#include <brillo/glib/object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib-object.h>

#include "cryptohome/service.h"

// Helpers for using GObjects until we can get a C++ wrapper going.
namespace cryptohome {
namespace gobject {  // Namespace hiding the GObject type data.

struct Cryptohome {
  GObject parent_instance;
  Service* service;  // pointer to implementing service.
};
struct CryptohomeClass {
  GObjectClass parent_class;
};

// cryptohome_get_type() is defined in interface.cc by the G_DEFINE_TYPE()
// macro.  This macro defines a number of other GLib class system specific
// functions and variables discussed in interface.cc.
GType cryptohome_get_type();  // defined by G_DEFINE_TYPE

// Interface function prototypes which wrap service.
gboolean cryptohome_check_key_ex(Cryptohome* self,
                                 GArray* identifier,
                                 GArray* authorization,
                                 GArray* request,
                                 DBusGMethodInvocation* resp);
gboolean cryptohome_remove_key_ex(Cryptohome* self,
                                  GArray* identifier,
                                  GArray* authorization,
                                  GArray* request,
                                  DBusGMethodInvocation* resp);
gboolean cryptohome_mass_remove_keys(Cryptohome* self,
                                     GArray* identifier,
                                     GArray* authorization,
                                     GArray* request,
                                     DBusGMethodInvocation* resp);
gboolean cryptohome_get_key_data_ex(Cryptohome* self,
                                    GArray* identifier,
                                    GArray* authorization,
                                    GArray* request,
                                    DBusGMethodInvocation* resp);
gboolean cryptohome_list_keys_ex(Cryptohome* self,
                                 GArray* identifier,
                                 GArray* authorization,
                                 GArray* request,
                                 DBusGMethodInvocation* resp);
gboolean cryptohome_migrate_key_ex(Cryptohome* self,
                                   GArray* account,
                                   GArray* auth_request,
                                   GArray* migrate_request,
                                   DBusGMethodInvocation* resp);
gboolean cryptohome_add_key_ex(Cryptohome* self,
                               GArray* id,
                               GArray* auth,
                               GArray* params,
                               DBusGMethodInvocation* resp);
gboolean cryptohome_add_data_restore_key(Cryptohome* self,
                                         GArray* id,
                                         GArray* auth,
                                         DBusGMethodInvocation* resp);
gboolean cryptohome_update_key_ex(Cryptohome* self,
                                  GArray* id,
                                  GArray* auth,
                                  GArray* params,
                                  DBusGMethodInvocation* resp);
gboolean cryptohome_remove_ex(Cryptohome* self,
                              GArray* userid,
                              DBusGMethodInvocation* resp);
gboolean cryptohome_rename_cryptohome(Cryptohome* self,
                                      GArray* cryptohome_id_from,
                                      GArray* cryptohome_id_to,
                                      DBusGMethodInvocation* resp);
gboolean cryptohome_get_account_disk_usage(Cryptohome* self,
                                           GArray* account_id,
                                           DBusGMethodInvocation* resp);
gboolean cryptohome_get_system_salt(Cryptohome* self,
                                    GArray** OUT_salt,
                                    GError** error);
gboolean cryptohome_get_sanitized_username(Cryptohome* self,
                                           gchar* username,
                                           gchar** OUT_sanitized,
                                           GError** error);
gboolean cryptohome_is_mounted(Cryptohome* self,
                               gboolean* OUT_is_mounted,
                               GError** error);
gboolean cryptohome_is_mounted_for_user(Cryptohome* self,
                                        gchar* userid,
                                        gboolean* OUT_is_mounted,
                                        gboolean* OUT_is_ephemeral_mount,
                                        GError** error);
gboolean cryptohome_mount_guest_ex(Cryptohome* self,
                                   GArray* request,
                                   DBusGMethodInvocation* resp);
gboolean cryptohome_mount_ex(Cryptohome* self,
                             GArray* id,
                             GArray* auth,
                             GArray* params,
                             DBusGMethodInvocation* resp);
gboolean cryptohome_unmount(Cryptohome* self,
                            gboolean* OUT_result,
                            GError** error);
gboolean cryptohome_unmount_ex(Cryptohome* self,
                               GArray* request,
                               DBusGMethodInvocation* resp);
gboolean cryptohome_remove_tracked_subdirectories(Cryptohome* self,
                                                  gboolean* OUT_result,
                                                  GError** error);
gboolean cryptohome_async_remove_tracked_subdirectories(Cryptohome* self,
                                                        gint* OUT_async_id,
                                                        GError** error);
gboolean cryptohome_update_current_user_activity_timestamp(Cryptohome* self,
                                                           gint time_shift_sec,
                                                           GError** error);
gboolean cryptohome_tpm_is_ready(Cryptohome* self,
                                 gboolean* OUT_ready,
                                 GError** error);
gboolean cryptohome_tpm_is_enabled(Cryptohome* self,
                                   gboolean* OUT_enabled,
                                   GError** error);
gboolean cryptohome_tpm_get_password(Cryptohome* self,
                                     gchar** OUT_password,
                                     GError** error);
gboolean cryptohome_tpm_is_owned(Cryptohome* self,
                                 gboolean* OUT_owned,
                                 GError** error);
gboolean cryptohome_tpm_is_being_owned(Cryptohome* self,
                                       gboolean* OUT_owning,
                                       GError** error);
gboolean cryptohome_tpm_can_attempt_ownership(Cryptohome* self, GError** error);
gboolean cryptohome_tpm_clear_stored_password(Cryptohome* self, GError** error);
gboolean cryptohome_tpm_is_attestation_prepared(Cryptohome* self,
                                                gboolean* OUT_prepared,
                                                GError** error);
gboolean cryptohome_tpm_attestation_get_enrollment_preparations_ex(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* resp);
gboolean cryptohome_tpm_verify_attestation_data(Cryptohome* self,
                                                gboolean is_cros_core,
                                                gboolean* OUT_verified,
                                                GError** error);
gboolean cryptohome_tpm_verify_ek(Cryptohome* self,
                                  gboolean is_cros_core,
                                  gboolean* OUT_verified,
                                  GError** error);
gboolean cryptohome_tpm_attestation_create_enroll_request(
    Cryptohome* self, gint pca_type, GArray** OUT_pca_request, GError** error);
gboolean cryptohome_async_tpm_attestation_create_enroll_request(
    Cryptohome* self, gint pca_type, gint* OUT_async_id, GError** error);
gboolean cryptohome_tpm_attestation_enroll(Cryptohome* self,
                                           gint pca_type,
                                           GArray* pca_response,
                                           gboolean* OUT_success,
                                           GError** error);
gboolean cryptohome_async_tpm_attestation_enroll(Cryptohome* self,
                                                 gint pca_type,
                                                 GArray* pca_response,
                                                 gint* OUT_async_id,
                                                 GError** error);
gboolean cryptohome_tpm_attestation_enroll_ex(Cryptohome* self,
                                              gint pca_type,
                                              gboolean forced,
                                              gboolean* OUT_success,
                                              GError** error);
gboolean cryptohome_async_tpm_attestation_enroll_ex(Cryptohome* self,
                                                    gint pca_type,
                                                    gboolean forced,
                                                    gint* OUT_async_id,
                                                    GError** error);
gboolean cryptohome_tpm_attestation_create_cert_request(
    Cryptohome* self,
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    GArray** OUT_pca_request,
    GError** error);
gboolean cryptohome_async_tpm_attestation_create_cert_request(
    Cryptohome* self,
    gint pca_type,
    gint certificate_profile,
    gchar* username,
    gchar* request_origin,
    gint* OUT_async_id,
    GError** error);
gboolean cryptohome_tpm_attestation_finish_cert_request(
    Cryptohome* self,
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray** OUT_cert,
    gboolean* OUT_success,
    GError** error);
gboolean cryptohome_async_tpm_attestation_finish_cert_request(
    Cryptohome* self,
    GArray* pca_response,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    gint* OUT_async_id,
    GError** error);
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
    GError** error);
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
    GError** error);
gboolean cryptohome_tpm_is_attestation_enrolled(Cryptohome* self,
                                                gboolean* OUT_is_enrolled,
                                                GError** error);
gboolean cryptohome_tpm_attestation_does_key_exist(Cryptohome* self,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   gboolean* OUT_exists,
                                                   GError** error);
gboolean cryptohome_tpm_attestation_get_certificate(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray** OUT_certificate,
                                                    gboolean* OUT_success,
                                                    GError** error);
gboolean cryptohome_tpm_attestation_get_public_key(Cryptohome* self,
                                                   gboolean is_user_specific,
                                                   gchar* username,
                                                   gchar* key_name,
                                                   GArray** OUT_public_key,
                                                   gboolean* OUT_success,
                                                   GError** error);
gboolean cryptohome_tpm_attestation_register_key(Cryptohome* self,
                                                 gboolean is_user_specific,
                                                 gchar* username,
                                                 gchar* key_name,
                                                 gint* OUT_async_id,
                                                 GError** error);
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
    GError** error);
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
    GError** error);
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
    GError** error);
gboolean cryptohome_tpm_attestation_sign_simple_challenge(
    Cryptohome* self,
    gboolean is_user_specific,
    gchar* username,
    gchar* key_name,
    GArray* challenge,
    gint* OUT_async_id,
    GError** error);
gboolean cryptohome_tpm_attestation_get_key_payload(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray** OUT_payload,
                                                    gboolean* OUT_success,
                                                    GError** error);
gboolean cryptohome_tpm_attestation_set_key_payload(Cryptohome* self,
                                                    gboolean is_user_specific,
                                                    gchar* username,
                                                    gchar* key_name,
                                                    GArray* payload,
                                                    gboolean* OUT_success,
                                                    GError** error);
gboolean cryptohome_tpm_attestation_delete_keys(Cryptohome* self,
                                                gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_prefix,
                                                gboolean* OUT_success,
                                                GError** error);
gboolean cryptohome_tpm_attestation_delete_key(Cryptohome* self,
                                               gboolean is_user_specific,
                                               gchar* username,
                                               gchar* key_name,
                                               gboolean* OUT_success,
                                               GError** error);
gboolean cryptohome_tpm_attestation_get_ek(Cryptohome* self,
                                           gchar** OUT_ek_info,
                                           gboolean* OUT_success,
                                           GError** error);
gboolean cryptohome_tpm_attestation_reset_identity(Cryptohome* self,
                                                   gchar* reset_token,
                                                   GArray** OUT_reset_request,
                                                   gboolean* OUT_success,
                                                   GError** error);
gboolean cryptohome_tpm_get_version_structured(Cryptohome* self,
                                               guint32* OUT_family,
                                               guint64* OUT_spec_level,
                                               guint32* OUT_manufacturer,
                                               guint32* OUT_tpm_model,
                                               guint64* OUT_firmware_version,
                                               gchar** OUT_vendor_specific,
                                               GError** error);
gboolean cryptohome_pkcs11_get_tpm_token_info(Cryptohome* self,
                                              gchar** OUT_label,
                                              gchar** OUT_user_pin,
                                              gint* OUT_slot,
                                              GError** error);
gboolean cryptohome_pkcs11_get_tpm_token_info_for_user(Cryptohome* self,
                                                       gchar* username,
                                                       gchar** OUT_label,
                                                       gchar** OUT_user_pin,
                                                       gint* OUT_slot,
                                                       GError** error);
gboolean cryptohome_pkcs11_is_tpm_token_ready(Cryptohome* self,
                                              gboolean* OUT_ready,
                                              GError** error);
gboolean cryptohome_pkcs11_is_tpm_token_ready_for_user(Cryptohome* self,
                                                       gchar* username,
                                                       gboolean* OUT_ready,
                                                       GError** error);
gboolean cryptohome_pkcs11_terminate(Cryptohome* self,
                                     gchar* username,
                                     GError** error);
gboolean cryptohome_get_status_string(Cryptohome* self,
                                      gchar** OUT_status,
                                      GError** error);
gboolean cryptohome_install_attributes_get(Cryptohome* self,
                                           gchar* name,
                                           GArray** OUT_value,
                                           gboolean* OUT_successful,
                                           GError** error);
gboolean cryptohome_install_attributes_set(Cryptohome* self,
                                           gchar* name,
                                           GArray* value,
                                           gboolean* OUT_successful,
                                           GError** error);
gboolean cryptohome_install_attributes_finalize(Cryptohome* self,
                                                gboolean* OUT_successful,
                                                GError** error);
gboolean cryptohome_install_attributes_count(Cryptohome* self,
                                             gint* OUT_count,
                                             GError** error);
gboolean cryptohome_install_attributes_is_ready(Cryptohome* self,
                                                gboolean* OUT_is_ready,
                                                GError** error);
gboolean cryptohome_install_attributes_is_secure(Cryptohome* self,
                                                 gboolean* OUT_is_secure,
                                                 GError** error);
gboolean cryptohome_install_attributes_is_invalid(Cryptohome* self,
                                                  gboolean* OUT_is_invalid,
                                                  GError** error);
gboolean cryptohome_install_attributes_is_first_install(
    Cryptohome* self, gboolean* OUT_is_first_install, GError** error);
gboolean cryptohome_sign_boot_lockbox(Cryptohome* self,
                                      GArray* request,
                                      DBusGMethodInvocation* resp);
gboolean cryptohome_verify_boot_lockbox(Cryptohome* self,
                                        GArray* request,
                                        DBusGMethodInvocation* resp);
gboolean cryptohome_finalize_boot_lockbox(Cryptohome* self,
                                          GArray* request,
                                          DBusGMethodInvocation* resp);
gboolean cryptohome_get_boot_attribute(Cryptohome* self,
                                       GArray* request,
                                       DBusGMethodInvocation* resp);
gboolean cryptohome_set_boot_attribute(Cryptohome* self,
                                       GArray* request,
                                       DBusGMethodInvocation* resp);
gboolean cryptohome_flush_and_sign_boot_attributes(Cryptohome* self,
                                                   GArray* request,
                                                   DBusGMethodInvocation* resp);
gboolean cryptohome_get_login_status(Cryptohome* self,
                                     GArray* request,
                                     DBusGMethodInvocation* resp);
gboolean cryptohome_get_tpm_status(Cryptohome* self,
                                   GArray* request,
                                   DBusGMethodInvocation* resp);
gboolean cryptohome_get_endorsement_info(Cryptohome* self,
                                         GArray* request,
                                         DBusGMethodInvocation* resp);
gboolean cryptohome_initialize_cast_key(Cryptohome* self,
                                        GArray* request,
                                        DBusGMethodInvocation* resp);

gboolean cryptohome_start_fingerprint_auth_session(Cryptohome* self,
                                                   GArray* id,
                                                   GArray* request,
                                                   DBusGMethodInvocation* resp);
gboolean cryptohome_end_fingerprint_auth_session(Cryptohome* self,
                                                 GArray* request,
                                                 DBusGMethodInvocation* resp);

gboolean cryptohome_get_web_authn_secret(Cryptohome* self,
                                         GArray* id,
                                         GArray* request,
                                         DBusGMethodInvocation* resp);

gboolean cryptohome_get_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* resp);
gboolean cryptohome_set_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* resp);
gboolean cryptohome_remove_firmware_management_parameters(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* resp);

gboolean cryptohome_migrate_to_dircrypto(Cryptohome* self,
                                         GArray* id,
                                         GArray* migrate_request,
                                         GError** error);
gboolean cryptohome_needs_dircrypto_migration(Cryptohome* self,
                                              GArray* identifier,
                                              gboolean* OUT_needs_migration,
                                              GError** error);

gboolean cryptohome_tpm_attestation_get_enrollment_id(
    Cryptohome* self,
    gboolean ignore_cache,
    GArray** OUT_enrollment_id,
    gboolean* OUT_success,
    GError** error);

gboolean cryptohome_get_supported_key_policies(Cryptohome* self,
                                               GArray* request,
                                               DBusGMethodInvocation* resp);

gboolean cryptohome_is_quota_supported(Cryptohome* self,
                                       gboolean* OUT_quota_supported,
                                       GError** error);

gboolean cryptohome_get_current_space_for_uid(Cryptohome* self,
                                              guint32 uid,
                                              gint64* OUT_cur_space,
                                              GError** error);

gboolean cryptohome_get_current_space_for_gid(Cryptohome* self,
                                              guint32 gid,
                                              gint64* OUT_cur_space,
                                              GError** error);

gboolean cryptohome_get_current_space_for_project_id(Cryptohome* self,
                                                     guint32 project_id,
                                                     gint64* OUT_cur_space,
                                                     GError** error);

gboolean cryptohome_set_project_id(Cryptohome* self,
                                   guint32 project_id,
                                   gint32 parent_path,
                                   gchar* child_path,
                                   GArray* account_id,
                                   gboolean* OUT_success,
                                   GError** error);

gboolean cryptohome_lock_to_single_user_mount_until_reboot(
    Cryptohome* self, GArray* request, DBusGMethodInvocation* resp);

gboolean cryptohome_get_rsu_device_id(Cryptohome* self,
                                      GArray* request,
                                      DBusGMethodInvocation* error);

gboolean cryptohome_check_health(Cryptohome* self,
                                 GArray* request,
                                 DBusGMethodInvocation* error);

gboolean cryptohome_start_auth_session(Cryptohome* self,
                                       GArray* id,
                                       GArray* request,
                                       DBusGMethodInvocation* error);

}  // namespace gobject
}  // namespace cryptohome
#endif  // CRYPTOHOME_INTERFACE_H_
