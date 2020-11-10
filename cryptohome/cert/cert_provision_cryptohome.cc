// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Cryptohome interface classes for cert_provision library.

#include <chromeos/dbus/service_constants.h>

#include "bindings/cryptohome.dbusclient.h"
#include "cryptohome/cert/cert_provision_cryptohome.h"

namespace {

void SetSecureBlob(brillo::SecureBlob* blob, gchar* ptr, size_t len) {
  uint8_t* data = reinterpret_cast<uint8_t*>(ptr);
  brillo::SecureBlob tmp(data, data + len);
  blob->swap(tmp);
}

}  // namespace

namespace cert_provision {

// Utility class to wait for the status of the TpmAttestationRegisterKey or
// other similar requests, reported asynchronously by cryptohomed.
class AsyncStatus {
 public:
  explicit AsyncStatus(::DBusGProxy* gproxy);
  AsyncStatus(const AsyncStatus&) = delete;
  AsyncStatus& operator=(const AsyncStatus&) = delete;

  // Waits for AsyncCallStatus with the given |async_id|. Returns the
  // status reported in that AsyncCallStatus.
  bool StatusWait(int async_id);

 private:
  // Handles AsyncCallStatus signals.
  static void Callback(::DBusGProxy* proxy,
                       int async_id,
                       bool status,
                       int return_code,
                       gpointer userdata);
  void Process(int async_id, bool status);

  GMainLoop* loop_ = nullptr;
  int async_id_ = 0;
  gboolean status_ = 0;
  ::DBusGProxy* gproxy_;
};

CryptohomeProxy* CryptohomeProxy::subst_obj = nullptr;

Scoped<CryptohomeProxy> CryptohomeProxy::Create() {
  return subst_obj ? Scoped<CryptohomeProxy>(subst_obj)
                   : Scoped<CryptohomeProxy>(GetDefault());
}

std::unique_ptr<CryptohomeProxy> CryptohomeProxy::GetDefault() {
  return std::unique_ptr<CryptohomeProxy>(new CryptohomeProxyImpl());
}

CryptohomeProxyImpl::CryptohomeProxyImpl()
    : bus_(brillo::dbus::GetSystemBusConnection()),
      proxy_(bus_,
             cryptohome::kCryptohomeServiceName,
             cryptohome::kCryptohomeServicePath,
             cryptohome::kCryptohomeInterface),
      gproxy_(proxy_.gproxy()) {}

OpResult CryptohomeProxyImpl::Init() {
  if (!gproxy_) {
    return {Status::DBusError, "Failed to acquire dbus proxy."};
  }
  dbus_g_proxy_set_default_timeout(gproxy_, kDefaultTimeoutMs);
  return OpResult();
}

OpResult CryptohomeProxyImpl::CheckIfPrepared(bool* is_prepared) {
  brillo::glib::ScopedError error;
  gboolean result = FALSE;
  if (!org_chromium_CryptohomeInterface_tpm_is_attestation_prepared(
          gproxy_, &result, &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmIsAttestationPrepared", error.get());
  }
  *is_prepared = result;
  return OpResult();
}

OpResult CryptohomeProxyImpl::CheckIfEnrolled(bool* is_enrolled) {
  brillo::glib::ScopedError error;
  gboolean result = FALSE;
  if (!org_chromium_CryptohomeInterface_tpm_is_attestation_enrolled(
          gproxy_, &result, &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmIsAttestationEnrolled", error.get());
  }
  *is_enrolled = result;
  return OpResult();
}

OpResult CryptohomeProxyImpl::CreateEnrollRequest(PCAType pca_type,
                                                  brillo::SecureBlob* request) {
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray data;

  if (!org_chromium_CryptohomeInterface_tpm_attestation_create_enroll_request(
          gproxy_, pca_type, &brillo::Resetter(&data).lvalue(),
          &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationCreateEnrollRequest", error.get());
  }
  SetSecureBlob(request, data->data, data->len);
  return OpResult();
}

OpResult CryptohomeProxyImpl::ProcessEnrollResponse(
    PCAType pca_type, const brillo::SecureBlob& response) {
  brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
  g_array_append_vals(data.get(), response.data(), response.size());
  gboolean success = FALSE;
  brillo::glib::ScopedError error;

  if (!org_chromium_CryptohomeInterface_tpm_attestation_enroll(
          gproxy_, pca_type, data.get(), &success,
          &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationEnroll", error.get());
  }
  return OpResult();
}

OpResult CryptohomeProxyImpl::CreateCertRequest(PCAType pca_type,
                                                CertificateProfile cert_profile,
                                                brillo::SecureBlob* request) {
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray data;

  if (!org_chromium_CryptohomeInterface_tpm_attestation_create_cert_request(
          gproxy_, pca_type, cert_profile, "" /* username */,
          "" /* request_origin */, &brillo::Resetter(&data).lvalue(),
          &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationCreateCertRequest", error.get());
  }
  SetSecureBlob(request, data->data, data->len);
  return OpResult();
}

OpResult CryptohomeProxyImpl::ProcessCertResponse(
    const std::string& label,
    const brillo::SecureBlob& response,
    brillo::SecureBlob* cert) {
  brillo::glib::ScopedArray data(g_array_new(FALSE, FALSE, 1));
  g_array_append_vals(data.get(), response.data(), response.size());
  gboolean success = FALSE;
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray cert_data;

  if (!org_chromium_CryptohomeInterface_tpm_attestation_finish_cert_request(
          gproxy_, data.get(), false /* is_user_specific */,
          "" /* account_id */, label.c_str(),
          &brillo::Resetter(&cert_data).lvalue(), &success,
          &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationFinishCertRequest", error.get());
  }
  if (!success) {
    return {Status::CryptohomeError, "Attestation certificate request failed."};
  }
  if (cert) {
    SetSecureBlob(cert, cert_data->data, cert_data->len);
  }
  return OpResult();
}

OpResult CryptohomeProxyImpl::GetPublicKey(const std::string& label,
                                           brillo::SecureBlob* public_key) {
  gboolean success = FALSE;
  brillo::glib::ScopedError error;
  brillo::glib::ScopedArray public_key_data;
  if (!org_chromium_CryptohomeInterface_tpm_attestation_get_public_key(
          gproxy_, false /* is_user_specific */, "" /* account_id */,
          label.c_str(), &brillo::Resetter(&public_key_data).lvalue(), &success,
          &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationGetPublicKey", error.get());
  }
  if (!success) {
    return {Status::CryptohomeError,
            "Getting public key for the obtained certificate failed."};
  }
  SetSecureBlob(public_key, public_key_data->data, public_key_data->len);
  return OpResult();
}

OpResult CryptohomeProxyImpl::Register(const std::string& label) {
  AsyncStatus async_status(gproxy_);
  gint async_id = -1;
  brillo::glib::ScopedError error;
  if (!org_chromium_CryptohomeInterface_tpm_attestation_register_key(
          gproxy_, false /* is_user_specific */, "" /* username */,
          label.c_str(), &async_id, &brillo::Resetter(&error).lvalue())) {
    return DBusError("TpmAttestationRegisterKey", error.get());
  }
  // TODO(apronin): implement timeout waiting for the result.
  if (!async_status.StatusWait(async_id)) {
    return {Status::CryptohomeError, "Failed to register key."};
  }

  return OpResult();
}

AsyncStatus::AsyncStatus(::DBusGProxy* gproxy) : gproxy_(gproxy) {
  dbus_g_object_register_marshaller(g_cclosure_marshal_generic, G_TYPE_NONE,
                                    G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_INT,
                                    G_TYPE_INVALID);
  dbus_g_proxy_add_signal(gproxy_, "AsyncCallStatus", G_TYPE_INT,
                          G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(gproxy_, "AsyncCallStatus",
                              G_CALLBACK(AsyncStatus::Callback), this, NULL);
  loop_ = g_main_loop_new(NULL, TRUE);
}

void AsyncStatus::Process(int async_id, bool status) {
  if (async_id == async_id_) {
    status_ = status;
    g_main_loop_quit(loop_);
  }
}

void AsyncStatus::Callback(::DBusGProxy* /* proxy */,
                           int async_id,
                           bool status,
                           int /* return_code */,
                           gpointer userdata) {
  reinterpret_cast<AsyncStatus*>(userdata)->Process(async_id, status);
}

bool AsyncStatus::StatusWait(int async_id) {
  async_id_ = async_id;
  g_main_loop_run(loop_);
  return status_;
}

}  // namespace cert_provision
